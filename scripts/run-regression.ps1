<#
.SYNOPSIS
Builds this checkout's Windows player against an isolated copy of the DemoDisc project for regression runs.

.DESCRIPTION
Modes (exactly one is required):
  -BuildOnly  Builds the player from THIS checkout (the folder above this script) into <WorkRoot>\player against
              -ProjectCommit (default: DemoDisc HEAD) and prints PROJECT_COMMIT=<built commit>,
              PLAYER_SOURCE_ROOT=<checkout> and PLAYER=<exe path>. Note that -Verify does not build HEAD: it pins
              the project to the manifest's recorded projectCommit, so pass -ProjectCommit <that sha> to reproduce
              a Verify build.
  -Record     Builds, runs every scene twice (runs a and b), records each stable scene as a golden, writes
              manifest.json (run settings, scene status, host fingerprints, the idle scenario and provenance hashes)
              and records each test suite's failing-test set and executed-test count. Everything is written to
              <WorkRoot>\record-staging first and copied into regression\golden and regression\baselines only when
              the whole record had no FAIL.
  -Verify     Builds, runs every scene once, checks that the smoke scene is not blank, compares every stable scene
              with its golden and every scene's host fingerprints with the recorded ones, runs the idle-throttle,
              overlay and overlay+idle scenarios (see below), and compares each test suite's failing-test set and
              executed-test count with its baseline. Prints one line per check, then RESULT: PASS or RESULT: FAIL
              (<n> failing), and exits 0 only on PASS.

Host fingerprints are kept per window: every HOST_FINGERPRINT line of a run's startup log is collected into a map keyed
by its window=<tag> field (a run without a line, a line without a window tag or two lines for one window is a FAIL).
The manifest stores that map as "fingerprints": { "<window>": { <fields>, "elapsedMs": <n> } }, and Verify fails a
missing or an extra window by name before it compares every window's fields. A manifest entry in the older single
"fingerprint" shape is a FAIL asking for a re-record.

Record and Verify both run the opt-in idle-throttle scenario once on the smoke scene (30 frames with --idle-throttle on
--idle-after-ms 1 --idle-fps 10): the run must pass the regression tool's check-idle command (throttle on, no Present
failures, at least 25 idle frames, at least 2400 ms elapsed) and its capture must match the smoke scene's golden.
Record writes those minimums into the manifest's idle entry, and Verify checks the minimums recorded there (an entry
without them fails with a re-record request).

Record and Verify both run the opt-in overlay scenario on the smoke scene (30 frames with --window-mode overlay
--overlay-bounds profile --overlay-background transparent --hit-test-probe <x,y>). Record runs it once to record the
alpha-preserving overlay golden (<smoke scene>.overlay.png), finds one fully transparent and one fully opaque probe
pixel in that golden, then runs it once per probe. Verify runs it once per recorded probe. Every probe run must pass
check-premultiplied (1% transparent, 1% opaque), match the overlay golden with compare-rgba and log a HIT_TEST line
"clickThrough=on exStyle=0x<8 hex>" with WS_EX_TRANSPARENT (0x20) set (transparent probe) or "clickThrough=off
exStyle=0x<8 hex>" with it cleared (opaque probe). Record stores each probe's observed exStyle (transparentExStyle,
opaqueExStyle) and host fingerprints (fingerprintsByProbe.transparent, fingerprintsByProbe.opaque); Verify requires the
exact exStyle and compares each probe run's fingerprints with its own probe's record.

Record and Verify both run the opt-in overlay+idle scenario once on the smoke scene: the overlay flags plus the
idle-throttle flags and the transparent probe. It must pass check-idle with the idle entry's minimums,
check-premultiplied and compare-rgba against the overlay golden, and log HIT_TEST clickThrough=on with the transparent
probe's exStyle. Its fingerprints are recorded in the overlayIdle entry for reference but never compared.

Every scene run is limited to 120 seconds; a hung player is killed and reported as FAIL scene <id> timeout.
The work root is marked with a .helengine-regression-workroot file; a non-empty folder without it is refused.

See regression\README.md for the thresholds and for when and how to re-record deliberately.

Project pinning: -Verify always builds the project commit recorded as projectCommit in regression\golden\manifest.json
(a manifest without it, or a commit the project source does not have, stops the run with "re-record required" or
"recorded DemoDisc commit ... was not found"); it never follows the project's HEAD and only WARNs when HEAD differs.
-Record and -BuildOnly build -ProjectCommit <sha> (default: HEAD), and -Record records that commit as the new pin.

The script never modifies the helengine checkout or the project source. It extracts the selected project commit
(git archive) into <WorkRoot>\project, copies the git-ignored user_settings folder beside it, writes its own engine
user settings to <WorkRoot>\engine-user-settings (pointing the windows platform at this checkout), and points
HELENGINE_ENGINE_USER_SETTINGS_ROOT there only for the duration of the canonical build-platform.ps1 call.
Those engine user settings are generated from -PlatformsManifestPath (default: <HelengineRoot>\user_settings\platforms.json),
whose relative paths are made absolute against that file's folder. A helengine worktree has no user_settings\platforms.json
(it is git-ignored), so building against one passes the main checkout's file explicitly; see regression\README.md.
#>
param(
    [Parameter()]
    [switch]$Record,

    [Parameter()]
    [switch]$Verify,

    [Parameter()]
    [switch]$BuildOnly,

    [Parameter()]
    [string]$HelengineRoot = 'C:\dev\helworks\helengine',

    [Parameter()]
    [string]$ProjectSource = 'C:\dev\helprojs\demodisc',

    [Parameter()]
    [string]$WorkRoot = 'C:\dev\helworks\builds\helengine-windows\regression',

    [Parameter()]
    [string]$ProjectCommit = '',

    [Parameter()]
    [string]$PlatformsManifestPath = (Join-Path $HelengineRoot 'user_settings\platforms.json')
)

$ErrorActionPreference = 'Stop'

$selectedModeCount = 0
if ($Record) { $selectedModeCount++ }
if ($Verify) { $selectedModeCount++ }
if ($BuildOnly) { $selectedModeCount++ }
if ($selectedModeCount -ne 1) {
    throw "Specify exactly one of -Record, -Verify or -BuildOnly."
}

# The functions below read these script-level values: $RepoRoot, $WorkRoot, $utf8WithoutBom, $CheckResults,
# $playerOutputPath, $playerExecutablePath, $regressionToolPath, $idlePlayerArguments and $overlayPlayerArguments.
# Check lines and progress lines use Write-Host
# so that they never become part of a function's return value.

<#
.SYNOPSIS
Throws when the work root equals, contains or lies inside any of the protected source trees.
.DESCRIPTION
The build stage deletes and rewrites folders under the work root, so it must never overlap the project source, the
helengine checkout or this checkout. Paths are compared as normalized full paths with a trailing separator, ignoring
case (Windows paths are case-insensitive).
#>
function Assert-WorkRootIsolated {
    param(
        [Parameter(Mandatory = $true)]
        [string]$WorkRootPath,

        [Parameter(Mandatory = $true)]
        [string[]]$ProtectedRootPaths
    )

    $separator = [string][System.IO.Path]::DirectorySeparatorChar
    $normalizedWorkRootPath = [System.IO.Path]::GetFullPath($WorkRootPath).TrimEnd('\', '/') + $separator
    foreach ($protectedRootPath in $ProtectedRootPaths) {
        $normalizedProtectedRootPath = [System.IO.Path]::GetFullPath($protectedRootPath).TrimEnd('\', '/') + $separator
        $workRootIsInside = $normalizedWorkRootPath.StartsWith($normalizedProtectedRootPath, [System.StringComparison]::OrdinalIgnoreCase)
        $protectedRootIsInside = $normalizedProtectedRootPath.StartsWith($normalizedWorkRootPath, [System.StringComparison]::OrdinalIgnoreCase)
        if ($workRootIsInside -or $protectedRootIsInside) {
            throw "The work root '$WorkRootPath' overlaps '$protectedRootPath'. Choose a work root outside the project source, the helengine checkout and this checkout."
        }
    }
}

<#
.SYNOPSIS
Records one check's outcome ("<PASS|FAIL|SKIP|WARN> <kind> <name> <detail>") for the final summary and echoes it.
Only FAIL lines count toward RESULT: FAIL; a WARN is shown in the summary but never changes the result.
#>
function Add-CheckResult {
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet('PASS', 'FAIL', 'SKIP', 'WARN')]
        [string]$Status,

        [Parameter(Mandatory = $true)]
        [string]$Kind,

        [Parameter(Mandatory = $true)]
        [string]$Name,

        [Parameter(Mandatory = $true)]
        [string]$Detail
    )

    $checkLine = "$Status $Kind $Name $Detail"
    $script:CheckResults.Add($checkLine)
    Write-Host $checkLine
}

<#
.SYNOPSIS
Runs one regression tool command and returns its exit code and printed lines.
.OUTPUTS
A [pscustomobject] with ExitCode (0 pass, 1 check failed, 2 error) and Lines (the tool's output lines).
#>
function Invoke-RegressionTool {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$ToolArguments
    )

    $toolLines = @(& $script:regressionToolPath @ToolArguments)
    return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Lines = $toolLines }
}

<#
.SYNOPSIS
Launches the player on one scene through the canonical launcher and waits for it to exit.
.DESCRIPTION
Rewrites the player's profile.json first (a profile left by an earlier run would otherwise override the 640x360
window), creates the capture folder (the player's BMP writer does not create folders) and deletes any capture and
startup log left by an earlier run so that a stale file is never checked. The launcher kills a run that takes longer
than 120 seconds. On a timeout or a non-zero exit code it prints the tail of the startup log. After the run it reads
every HOST_FINGERPRINT line and the HIT_TEST line from the startup log. FrameCount (default '30', the frame every golden
is captured at) and ExtraArguments (default none; the idle scenario passes the idle-throttle flags and the overlay
scenarios the overlay window flags plus one --hit-test-probe) extend the player's fixed arguments.
.OUTPUTS
A [pscustomobject] with TimedOut (whether the launcher killed the run), ExitCode (the player's exit code; $null on a
timeout), CaptureExists (whether the capture file was written), FingerprintsByWindow (every HOST_FINGERPRINT line,
without the log prefix, keyed by its window tag; see Get-FingerprintsByWindow), FingerprintProblem ($null, or why the
fingerprint lines cannot be used) and HitTest (the HIT_TEST line of a --hit-test-probe run without the log prefix, or
$null when the log has none).
#>
function Invoke-PlayerScene {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SceneId,

        [Parameter(Mandatory = $true)]
        [string]$CapturePath,

        [Parameter()]
        [string]$FrameCount = '30',

        [Parameter()]
        [string[]]$ExtraArguments = @()
    )

    [System.IO.File]::WriteAllText((Join-Path $script:playerOutputPath 'profile.json'), '{"resolutionWidth":640,"resolutionHeight":360}', $script:utf8WithoutBom)
    New-Item -ItemType Directory -Path (Split-Path -Path $CapturePath -Parent) -Force | Out-Null
    if (Test-Path -LiteralPath $CapturePath) {
        Remove-Item -LiteralPath $CapturePath -Force
    }
    $startupLogPath = Join-Path $script:playerOutputPath 'helengine_windows.startup.log'
    if (Test-Path -LiteralPath $startupLogPath) {
        Remove-Item -LiteralPath $startupLogPath -Force
    }

    Write-Host "RUN $SceneId -> $CapturePath"
    $playerArguments = @('--scene', $SceneId, '--frames', $FrameCount, '--fixed-delta', '0.016666', '--capture', $CapturePath) + $ExtraArguments
    $launchLines = @(& "$script:RepoRoot\scripts\launch_in_emulator.ps1" -ArtifactPath $script:playerExecutablePath -ArgumentList $playerArguments -Wait -TimeoutSeconds 120)
    $playerExitCode = $null
    $timedOut = $false
    foreach ($launchLine in $launchLines) {
        if ("$launchLine" -match '^EXIT_CODE=timeout$') {
            $timedOut = $true
        }
        elseif ("$launchLine" -match '^EXIT_CODE=(-?\d+)$') {
            $playerExitCode = [int]$Matches[1]
        }
    }
    if (-not $timedOut -and $null -eq $playerExitCode) {
        throw "The launcher did not report EXIT_CODE= for scene '$SceneId'."
    }

    if ($timedOut -or $playerExitCode -ne 0) {
        if ($timedOut) {
            Write-Host "Player was killed after 120 seconds on scene '$SceneId'. Startup log tail ($startupLogPath):"
        }
        else {
            Write-Host "Player exited with code $playerExitCode on scene '$SceneId'. Startup log tail ($startupLogPath):"
        }
        if (Test-Path -LiteralPath $startupLogPath -PathType Leaf) {
            Get-Content -LiteralPath $startupLogPath -Tail 20 | ForEach-Object { Write-Host "  $_" }
        }
        else {
            Write-Host "  (no startup log was written)"
        }
    }

    $fingerprintLines = New-Object System.Collections.Generic.List[string]
    $hitTestLine = $null
    if (Test-Path -LiteralPath $startupLogPath -PathType Leaf) {
        foreach ($startupLogLine in (Get-Content -LiteralPath $startupLogPath)) {
            if ("$startupLogLine" -match '(HOST_FINGERPRINT .+)$') {
                $fingerprintLines.Add($Matches[1])
            }
            elseif ("$startupLogLine" -match '(HIT_TEST .+)$') {
                $hitTestLine = $Matches[1]
            }
        }
    }
    $fingerprintCollection = Get-FingerprintsByWindow -FingerprintLines $fingerprintLines.ToArray()

    return [pscustomobject]@{ TimedOut = $timedOut; ExitCode = $playerExitCode; CaptureExists = (Test-Path -LiteralPath $CapturePath -PathType Leaf); FingerprintsByWindow = $fingerprintCollection.FingerprintsByWindow; FingerprintProblem = $fingerprintCollection.Problem; HitTest = $hitTestLine }
}

<#
.SYNOPSIS
Collects a run's HOST_FINGERPRINT lines into an ordered map keyed by each line's window=<tag> field.
.DESCRIPTION
The player writes one HOST_FINGERPRINT line per window it presented (today only "main"). The map keeps the lines in
log order and compares window tags case-sensitively. The lines cannot be used, and Problem says why, when there is no
line at all, when a line has no window field (a player that predates per-window fingerprints) or when two lines carry
the same window tag.
.OUTPUTS
A [pscustomobject] with FingerprintsByWindow (an OrderedDictionary from window tag to the full HOST_FINGERPRINT line)
and Problem ($null when the lines are usable, otherwise the FAIL detail).
#>
function Get-FingerprintsByWindow {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [string[]]$FingerprintLines
    )

    $fingerprintsByWindow = New-Object System.Collections.Specialized.OrderedDictionary
    if ($FingerprintLines.Count -eq 0) {
        return [pscustomobject]@{ FingerprintsByWindow = $fingerprintsByWindow; Problem = 'missing: the startup log has no HOST_FINGERPRINT line' }
    }
    foreach ($fingerprintLine in $FingerprintLines) {
        if ($fingerprintLine -cnotmatch '(?:^| )window=(\S+)(?: |$)') {
            return [pscustomobject]@{ FingerprintsByWindow = $fingerprintsByWindow; Problem = "HOST_FINGERPRINT line has no window field (rebuild the player): $fingerprintLine" }
        }
        $windowName = $Matches[1]
        if ($fingerprintsByWindow.Contains($windowName)) {
            return [pscustomobject]@{ FingerprintsByWindow = $fingerprintsByWindow; Problem = "duplicate window '$windowName': the startup log has more than one HOST_FINGERPRINT line for it" }
        }
        $fingerprintsByWindow[$windowName] = $fingerprintLine
    }
    return [pscustomobject]@{ FingerprintsByWindow = $fingerprintsByWindow; Problem = $null }
}

<#
.SYNOPSIS
Records a FAIL for a scene run that timed out, did not exit cleanly, wrote no capture or logged no usable host
fingerprints (none at all, a line without a window tag or a duplicate window tag).
.OUTPUTS
$true when the run succeeded and its capture and fingerprints can be checked; otherwise $false, after recording a FAIL.
#>
function Test-PlayerRunSucceeded {
    param(
        [Parameter(Mandatory = $true)]
        [pscustomobject]$PlayerRun,

        [Parameter(Mandatory = $true)]
        [string]$Kind,

        [Parameter(Mandatory = $true)]
        [string]$SceneId,

        [Parameter(Mandatory = $true)]
        [string]$CapturePath
    )

    if ($PlayerRun.TimedOut) {
        Add-CheckResult -Status FAIL -Kind scene -Name $SceneId -Detail 'timeout'
        return $false
    }
    if ($PlayerRun.ExitCode -ne 0) {
        Add-CheckResult -Status FAIL -Kind $Kind -Name $SceneId -Detail "player exit code $($PlayerRun.ExitCode)"
        return $false
    }
    if (-not $PlayerRun.CaptureExists) {
        Add-CheckResult -Status FAIL -Kind $Kind -Name $SceneId -Detail "capture missing: $CapturePath"
        return $false
    }
    if ($null -ne $PlayerRun.FingerprintProblem) {
        Add-CheckResult -Status FAIL -Kind fingerprint -Name $SceneId -Detail $PlayerRun.FingerprintProblem
        return $false
    }
    return $true
}

<#
.SYNOPSIS
Runs one regression tool fingerprint command (check-fingerprint or compare-fingerprint) and records its findings.
.DESCRIPTION
Each "FAIL <kind> <scene> <detail>" or "WARN <kind> <scene> <detail>" line the tool prints becomes a check line as
is (for example "FAIL fingerprint axis_test buffers recorded=2 actual=3" or "WARN pacing axis_test recorded=483ms
actual=1600ms"). When the tool reports no failure, one PASS fingerprint line with PassDetail is recorded. A tool error
is a FAIL.
#>
function Invoke-FingerprintCheck {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SceneId,

        [Parameter(Mandatory = $true)]
        [string[]]$ToolArguments,

        [Parameter(Mandatory = $true)]
        [string]$PassDetail
    )

    $fingerprintRun = Invoke-RegressionTool -ToolArguments $ToolArguments
    if ($fingerprintRun.ExitCode -ne 0 -and $fingerprintRun.ExitCode -ne 1) {
        Add-CheckResult -Status FAIL -Kind fingerprint -Name $SceneId -Detail ($fingerprintRun.Lines -join ' ')
        return
    }
    foreach ($findingLine in ($fingerprintRun.Lines | Select-Object -Skip 1)) {
        if ("$findingLine" -notmatch '^(FAIL|WARN) (\S+) (\S+) (.+)$') {
            throw "The regression tool printed an unexpected fingerprint line for scene '$SceneId': $findingLine"
        }
        Add-CheckResult -Status $Matches[1] -Kind $Matches[2] -Name $Matches[3] -Detail $Matches[4]
    }
    if ($fingerprintRun.ExitCode -eq 0) {
        Add-CheckResult -Status PASS -Kind fingerprint -Name $SceneId -Detail $PassDetail
    }
}

<#
.SYNOPSIS
Checks every window's fingerprint of one run on its own with the regression tool's check-fingerprint command.
.DESCRIPTION
Each window is checked under the name <Name>@<window> (for example axis_test@main), so a finding always names the
window it belongs to. The run must have presented at least once per frame with no Present failures in every window.
#>
function Invoke-FingerprintHealthCheck {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,

        [Parameter(Mandatory = $true)]
        [System.Collections.Specialized.OrderedDictionary]$FingerprintsByWindow,

        [Parameter(Mandatory = $true)]
        [string]$PassDetail
    )

    foreach ($windowName in $FingerprintsByWindow.Keys) {
        Invoke-FingerprintCheck -SceneId "$Name@$windowName" -ToolArguments @('check-fingerprint', "$Name@$windowName", $FingerprintsByWindow[$windowName]) -PassDetail $PassDetail
    }
}

<#
.SYNOPSIS
Compares a run's per-window fingerprints with recorded ones: the window sets must be equal and every window's fields
must match.
.DESCRIPTION
A recorded window the run did not log is "FAIL fingerprint <Name>@<window> window missing ...", and a window the run
logged but the record does not have is "FAIL fingerprint <Name>@<window> window extra ...". Every window present on both
sides goes through compare-fingerprint under the name <Name>@<window>, which fails each differing field and keeps the
coarse pacing WARN on that window's elapsedMs.
#>
function Compare-FingerprintsByWindow {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,

        [Parameter(Mandatory = $true)]
        [System.Collections.Specialized.OrderedDictionary]$RecordedFingerprintsByWindow,

        [Parameter(Mandatory = $true)]
        [System.Collections.Specialized.OrderedDictionary]$ActualFingerprintsByWindow,

        [Parameter(Mandatory = $true)]
        [string]$PassDetail
    )

    foreach ($windowName in $RecordedFingerprintsByWindow.Keys) {
        if (-not $ActualFingerprintsByWindow.Contains($windowName)) {
            Add-CheckResult -Status FAIL -Kind fingerprint -Name "$Name@$windowName" -Detail "window missing: the record has window '$windowName' but the run logged no HOST_FINGERPRINT line for it"
        }
        else {
            Invoke-FingerprintCheck -SceneId "$Name@$windowName" -ToolArguments @('compare-fingerprint', "$Name@$windowName", $RecordedFingerprintsByWindow[$windowName], $ActualFingerprintsByWindow[$windowName]) -PassDetail $PassDetail
        }
    }
    foreach ($windowName in $ActualFingerprintsByWindow.Keys) {
        if (-not $RecordedFingerprintsByWindow.Contains($windowName)) {
            Add-CheckResult -Status FAIL -Kind fingerprint -Name "$Name@$windowName" -Detail "window extra: the run logged window '$windowName', which the record does not have"
        }
    }
}

<#
.SYNOPSIS
Runs the regression tool's check-idle command on every window's fingerprint of an idle-throttle run and records one
check line per window.
.DESCRIPTION
Each window must report the idle throttle on, no Present failures, at least MinimumIdleFrames idle frames and at least
MinimumElapsedMilliseconds elapsed. The check lines are "<PASS|FAIL> <Kind> <scene> window <window>: <detail>".
#>
function Test-IdleFingerprints {
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet('idle', 'overlayIdle')]
        [string]$Kind,

        [Parameter(Mandatory = $true)]
        [string]$SceneId,

        [Parameter(Mandatory = $true)]
        [System.Collections.Specialized.OrderedDictionary]$FingerprintsByWindow,

        [Parameter(Mandatory = $true)]
        [string]$MinimumIdleFrames,

        [Parameter(Mandatory = $true)]
        [string]$MinimumElapsedMilliseconds
    )

    foreach ($windowName in $FingerprintsByWindow.Keys) {
        $checkIdleRun = Invoke-RegressionTool -ToolArguments @('check-idle', $FingerprintsByWindow[$windowName], $MinimumIdleFrames, $MinimumElapsedMilliseconds)
        if ($checkIdleRun.ExitCode -eq 0 -and "$($checkIdleRun.Lines -join ' ')" -match '^PASS (.+)$') {
            Add-CheckResult -Status PASS -Kind $Kind -Name $SceneId -Detail "window ${windowName}: $($Matches[1])"
        }
        elseif ($checkIdleRun.ExitCode -eq 1) {
            foreach ($checkIdleLine in $checkIdleRun.Lines) {
                if ("$checkIdleLine" -notmatch '^FAIL (.+)$') {
                    throw "The regression tool printed an unexpected check-idle line for scene '$SceneId': $checkIdleLine"
                }
                Add-CheckResult -Status FAIL -Kind $Kind -Name $SceneId -Detail "window ${windowName}: $($Matches[1])"
            }
        }
        else {
            Add-CheckResult -Status FAIL -Kind $Kind -Name $SceneId -Detail "window ${windowName}: $($checkIdleRun.Lines -join ' ')"
        }
    }
}

<#
.SYNOPSIS
Compares a capture with a golden through the regression tool and records one check line.
.DESCRIPTION
CompareCommand is "compare" for opaque goldens (alpha is ignored) or "compare-rgba" for the alpha-preserving overlay
golden. A missing golden is a FAIL. The detail reads "<Description> matches <GoldenLabel>: <tool output>" or
"<Description> differs from <GoldenLabel>: <tool output>"; on a difference the tool writes DiffPath.
#>
function Test-CaptureMatchesGolden {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Kind,

        [Parameter(Mandatory = $true)]
        [string]$SceneId,

        [Parameter(Mandatory = $true)]
        [ValidateSet('compare', 'compare-rgba')]
        [string]$CompareCommand,

        [Parameter(Mandatory = $true)]
        [string]$CapturePath,

        [Parameter(Mandatory = $true)]
        [string]$GoldenPath,

        [Parameter(Mandatory = $true)]
        [string]$DiffPath,

        [Parameter(Mandatory = $true)]
        [string]$Description,

        [Parameter(Mandatory = $true)]
        [string]$GoldenLabel
    )

    if (-not (Test-Path -LiteralPath $GoldenPath -PathType Leaf)) {
        Add-CheckResult -Status FAIL -Kind $Kind -Name $SceneId -Detail "$GoldenLabel missing: $GoldenPath"
        return
    }
    $compareRun = Invoke-RegressionTool -ToolArguments @($CompareCommand, $CapturePath, $GoldenPath, $DiffPath)
    $compareDetail = $compareRun.Lines -join ' '
    if ($compareRun.ExitCode -eq 0) {
        Add-CheckResult -Status PASS -Kind $Kind -Name $SceneId -Detail "$Description matches ${GoldenLabel}: $compareDetail"
    }
    else {
        Add-CheckResult -Status FAIL -Kind $Kind -Name $SceneId -Detail "$Description differs from ${GoldenLabel}: $compareDetail"
    }
}

<#
.SYNOPSIS
Runs the opt-in idle-throttle scenario once on a scene and records its checks as "idle" check lines.
.DESCRIPTION
Launches the player with the fixed 30-frame arguments plus the idle-throttle flags ($script:idlePlayerArguments), then
runs the regression tool's check-idle command on every window's HOST_FINGERPRINT line (idle throttle on, no Present
failures, at least MinimumIdleFrames idle frames and MinimumElapsedMilliseconds elapsed) and
compares the capture with the scene's golden, because throttling must never change what a frame renders. The
fingerprints are deliberately not compared with recorded ones: their idle and active frame counts and their elapsed
time differ from a normal run by design. When GoldenPath is empty the scene has no golden (it was recorded unstable),
so the capture comparison is reported as SKIP.
#>
function Invoke-IdleScenario {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SceneId,

        [Parameter(Mandatory = $true)]
        [string]$CapturePath,

        [Parameter(Mandatory = $true)]
        [string]$DiffPath,

        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [string]$GoldenPath,

        [Parameter(Mandatory = $true)]
        [string]$MinimumIdleFrames,

        [Parameter(Mandatory = $true)]
        [string]$MinimumElapsedMilliseconds
    )

    # A diff image left by an earlier run must never be mistaken for this run's, so it is deleted before the run.
    if (Test-Path -LiteralPath $DiffPath -PathType Leaf) {
        Remove-Item -LiteralPath $DiffPath -Force
    }
    $idleRun = Invoke-PlayerScene -SceneId $SceneId -CapturePath $CapturePath -ExtraArguments $script:idlePlayerArguments
    if (-not (Test-PlayerRunSucceeded -PlayerRun $idleRun -Kind idle -SceneId $SceneId -CapturePath $CapturePath)) {
        return
    }

    Test-IdleFingerprints -Kind idle -SceneId $SceneId -FingerprintsByWindow $idleRun.FingerprintsByWindow -MinimumIdleFrames $MinimumIdleFrames -MinimumElapsedMilliseconds $MinimumElapsedMilliseconds

    if ($GoldenPath.Length -eq 0) {
        Add-CheckResult -Status SKIP -Kind idle -Name $SceneId -Detail 'capture not compared: the scene has no golden (unstable)'
        return
    }
    Test-CaptureMatchesGolden -Kind idle -SceneId $SceneId -CompareCommand compare -CapturePath $CapturePath -GoldenPath $GoldenPath -DiffPath $DiffPath -Description 'capture' -GoldenLabel 'golden'
}

<#
.SYNOPSIS
Splits a HOST_FINGERPRINT line into its name=value fields, in the order the player wrote them.
.OUTPUTS
An ordered dictionary from field name to value (strings), without the HOST_FINGERPRINT marker.
#>
function ConvertTo-FingerprintFields {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FingerprintLine
    )

    $fingerprintFields = [ordered]@{}
    foreach ($fingerprintToken in ($FingerprintLine.Split(' ') | Select-Object -Skip 1)) {
        $separatorIndex = $fingerprintToken.IndexOf('=')
        if ($separatorIndex -le 0) {
            throw "Host fingerprint token '$fingerprintToken' is not name=value in: $FingerprintLine"
        }
        $fingerprintFields[$fingerprintToken.Substring(0, $separatorIndex)] = $fingerprintToken.Substring($separatorIndex + 1)
    }
    return $fingerprintFields
}

<#
.SYNOPSIS
Turns a run's per-window fingerprint lines into the manifest's "fingerprints" value.
.DESCRIPTION
Each window becomes an object of its fields in the order the player wrote them, ending with elapsedMs, which is stored
as a number (it is only a pacing signal, never compared exactly). A line without elapsedMs cannot be recorded and
throws; Record's check-fingerprint has already failed such a line.
.OUTPUTS
An OrderedDictionary from window tag to that window's ordered fields.
#>
function ConvertTo-ManifestFingerprints {
    param(
        [Parameter(Mandatory = $true)]
        [System.Collections.Specialized.OrderedDictionary]$FingerprintsByWindow
    )

    $manifestFingerprints = New-Object System.Collections.Specialized.OrderedDictionary
    foreach ($windowName in $FingerprintsByWindow.Keys) {
        $windowFields = ConvertTo-FingerprintFields -FingerprintLine $FingerprintsByWindow[$windowName]
        if (-not $windowFields.Contains('elapsedMs')) {
            throw "Host fingerprint of window '$windowName' has no elapsedMs field: $($FingerprintsByWindow[$windowName])"
        }
        $windowFields['elapsedMs'] = [long]$windowFields['elapsedMs']
        $manifestFingerprints[$windowName] = $windowFields
    }
    return $manifestFingerprints
}

<#
.SYNOPSIS
Rebuilds the recorded HOST_FINGERPRINT line of every window from a manifest "fingerprints" value, so that the
regression tool's compare-fingerprint command can compare each one with a run's line.
.DESCRIPTION
Every window's fields are joined in their recorded order (elapsedMs, recorded last, included), as
"name=value name=value ... elapsedMs=<n>" without the HOST_FINGERPRINT marker.
.OUTPUTS
An OrderedDictionary from window tag to the rebuilt line, in the manifest's window order.
#>
function ConvertTo-RecordedFingerprintsByWindow {
    param(
        [Parameter(Mandatory = $true)]
        [pscustomobject]$RecordedFingerprints
    )

    $recordedFingerprintsByWindow = New-Object System.Collections.Specialized.OrderedDictionary
    foreach ($recordedWindow in $RecordedFingerprints.PSObject.Properties) {
        $recordedFingerprintTokens = New-Object System.Collections.Generic.List[string]
        foreach ($recordedFingerprintField in $recordedWindow.Value.PSObject.Properties) {
            $recordedFingerprintTokens.Add("$($recordedFingerprintField.Name)=$($recordedFingerprintField.Value)")
        }
        $recordedFingerprintsByWindow[$recordedWindow.Name] = ($recordedFingerprintTokens -join ' ')
    }
    return $recordedFingerprintsByWindow
}

<#
.SYNOPSIS
Checks that one window's HOST_FINGERPRINT line of an overlay run describes the overlay window and records one check
line of Kind (overlay or overlayIdle).
.DESCRIPTION
The overlay window must report windowMode=overlay, alpha=1 (DXGI_ALPHA_MODE_PREMULTIPLIED) and an exStyle that holds
WS_EX_NOREDIRECTIONBITMAP (0x00200000), WS_EX_LAYERED (0x00080000), WS_EX_TOOLWINDOW (0x00000080) and WS_EX_TOPMOST
(0x00000008). Record runs this on the record run and on every probe run before it stores their fingerprints (Verify
then compares every field exactly with the stored ones), and the overlay+idle scenario runs it on every window in both
Record and Verify, because its fingerprints are never compared field by field.
#>
function Test-OverlayFingerprintShape {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SceneId,

        [Parameter(Mandatory = $true)]
        [string]$FingerprintLine,

        [Parameter(Mandatory = $true)]
        [ValidateSet('overlay', 'overlayIdle')]
        [string]$Kind
    )

    $overlayExStyleMask = 0x00280088
    $fingerprintFields = ConvertTo-FingerprintFields -FingerprintLine $FingerprintLine
    $exStyleText = "$($fingerprintFields['exStyle'])"
    if ($exStyleText -notmatch '^0x[0-9A-Fa-f]+$') {
        Add-CheckResult -Status FAIL -Kind $Kind -Name $SceneId -Detail "fingerprint exStyle is not hexadecimal: $exStyleText"
        return
    }
    $exStyleValue = [System.Convert]::ToInt64($exStyleText.Substring(2), 16)
    $shapeDetail = "windowMode=$($fingerprintFields['windowMode']) alpha=$($fingerprintFields['alpha']) exStyle=$exStyleText"
    if ($fingerprintFields['windowMode'] -eq 'overlay' -and $fingerprintFields['alpha'] -eq '1' -and ($exStyleValue -band $overlayExStyleMask) -eq $overlayExStyleMask) {
        Add-CheckResult -Status PASS -Kind $Kind -Name $SceneId -Detail "overlay window fingerprint: $shapeDetail"
    }
    else {
        Add-CheckResult -Status FAIL -Kind $Kind -Name $SceneId -Detail "not an overlay window fingerprint (expected windowMode=overlay alpha=1 and exStyle bits 0x00280088): $shapeDetail"
    }
}

<#
.SYNOPSIS
Runs the regression tool's check-premultiplied command on an overlay capture and records one check line of Kind
(overlay or overlayIdle).
.DESCRIPTION
The capture is read with its real alpha: every pixel must have B, G and R at most A (valid premultiplied alpha), at
least 1% of the pixels must be fully transparent (the transparent background exists) and at least 1% fully opaque.
ProbeName (record, transparent or opaque) names the run in the check line.
#>
function Test-OverlayPremultiplied {
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet('overlay', 'overlayIdle')]
        [string]$Kind,

        [Parameter(Mandatory = $true)]
        [string]$SceneId,

        [Parameter(Mandatory = $true)]
        [string]$ProbeName,

        [Parameter(Mandatory = $true)]
        [string]$CapturePath
    )

    $premultipliedRun = Invoke-RegressionTool -ToolArguments @('check-premultiplied', $CapturePath, '0.01', '0.01')
    $premultipliedDetail = $premultipliedRun.Lines -join ' '
    if ($premultipliedRun.ExitCode -eq 0) {
        Add-CheckResult -Status PASS -Kind $Kind -Name $SceneId -Detail "$ProbeName run premultiplied: $premultipliedDetail"
    }
    else {
        Add-CheckResult -Status FAIL -Kind $Kind -Name $SceneId -Detail "$ProbeName run premultiplied: $premultipliedDetail"
    }
}

<#
.SYNOPSIS
Checks an overlay probe run's HIT_TEST line exactly, including the window's extended style, and records one check line
of Kind (overlay or overlayIdle).
.DESCRIPTION
The player logs "HIT_TEST x=<x> y=<y> alpha=<a> clickThrough=<on|off> exStyle=0x<8 upper-case hex digits>" after the
last frame of a --hit-test-probe run, where exStyle is GWL_EXSTYLE read back after the probe's click-through toggle.
The line must name the probe's coordinates and report the expected click-through state: on for the transparent probe
(the sampled alpha is below 8) and off for the opaque probe. WS_EX_TRANSPARENT (0x20) must be set in exStyle for
clickThrough=on and cleared for clickThrough=off, which proves the toggle really changed the window style.
Without -RecordMode (Verify, and the overlay+idle run) exStyle must also equal ExpectedExStyle exactly, and an
ExpectedExStyle that is not 0x<8 upper-case hex digits throws: the caller validates recorded values first, so an
invalid one here is a bug, never a silent downgrade to the bit rule. With -RecordMode (Record's probe runs, before any
exStyle is recorded) ExpectedExStyle must be empty, and any exStyle that passes the bit rule is accepted and returned
for the manifest. A missing line (an empty HitTestLine) is a FAIL, and so is a Probe that is not "x,y" with base-10
coordinates (re-record required); the coordinates are regex-escaped in the expected pattern.
.OUTPUTS
The observed exStyle text (for example 0x002800A8) when the check passed; otherwise $null, after recording a FAIL.
#>
function Test-OverlayHitTest {
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet('overlay', 'overlayIdle')]
        [string]$Kind,

        [Parameter(Mandatory = $true)]
        [string]$SceneId,

        [Parameter(Mandatory = $true)]
        [string]$ProbeName,

        [Parameter(Mandatory = $true)]
        [string]$Probe,

        [Parameter(Mandatory = $true)]
        [ValidateSet('on', 'off')]
        [string]$ExpectedClickThrough,

        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [string]$ExpectedExStyle,

        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [string]$HitTestLine,

        [Parameter()]
        [switch]$RecordMode
    )

    if ($RecordMode -and $ExpectedExStyle.Length -gt 0) {
        throw "Test-OverlayHitTest -RecordMode takes no ExpectedExStyle (got '$ExpectedExStyle'): Record accepts the observed value."
    }
    if (-not $RecordMode -and $ExpectedExStyle -cnotmatch '^0x[0-9A-F]{8}$') {
        throw "Test-OverlayHitTest requires ExpectedExStyle as 0x<8 upper-case hex digits outside -RecordMode (got '$ExpectedExStyle'); validate the recorded value before calling it."
    }
    if ($HitTestLine.Length -eq 0) {
        Add-CheckResult -Status FAIL -Kind $Kind -Name $SceneId -Detail "$ProbeName probe ${Probe}: the startup log has no HIT_TEST line"
        return $null
    }
    # The probe text goes into the expected pattern, so it must be two base-10 coordinates; the coordinates are also
    # escaped so that the pattern can only ever match them literally.
    if ($Probe -notmatch '^\d+,\d+$') {
        Add-CheckResult -Status FAIL -Kind $Kind -Name $SceneId -Detail "$ProbeName probe '$Probe' is not x,y (re-record required)"
        return $null
    }
    $probeCoordinates = $Probe.Split(',')
    $expectedHitTestPattern = "^HIT_TEST x=$([regex]::Escape($probeCoordinates[0])) y=$([regex]::Escape($probeCoordinates[1])) alpha=\d+ clickThrough=$ExpectedClickThrough exStyle=(0x[0-9A-F]{8})$"
    if ($HitTestLine -cnotmatch $expectedHitTestPattern) {
        Add-CheckResult -Status FAIL -Kind $Kind -Name $SceneId -Detail "$ProbeName probe ${Probe} expected clickThrough=${ExpectedClickThrough} exStyle=0x<8 hex digits>: $HitTestLine"
        return $null
    }
    $observedExStyle = $Matches[1]
    $transparentBitSet = ([System.Convert]::ToInt64($observedExStyle.Substring(2), 16) -band 0x20) -ne 0
    if ($transparentBitSet -ne ($ExpectedClickThrough -eq 'on')) {
        Add-CheckResult -Status FAIL -Kind $Kind -Name $SceneId -Detail "$ProbeName probe clickThrough=${ExpectedClickThrough} requires WS_EX_TRANSPARENT (0x20) to be $(if ($ExpectedClickThrough -eq 'on') { 'set' } else { 'cleared' }): $HitTestLine"
        return $null
    }
    if (-not $RecordMode -and $observedExStyle -cne $ExpectedExStyle) {
        Add-CheckResult -Status FAIL -Kind $Kind -Name $SceneId -Detail "$ProbeName probe exStyle recorded=$ExpectedExStyle actual=${observedExStyle}: $HitTestLine"
        return $null
    }
    Add-CheckResult -Status PASS -Kind $Kind -Name $SceneId -Detail "$ProbeName probe: $HitTestLine"
    return $observedExStyle
}

<#
.SYNOPSIS
Launches one overlay probe run of the smoke scene and checks that it ran: the launch shared by the overlay and
overlay+idle scenarios.
.DESCRIPTION
Deletes a diff image left by an earlier run (so it is never mistaken for this run's), validates the probe and launches
the player with the fixed 30-frame arguments plus the overlay window flags ($script:overlayPlayerArguments),
ExtraArguments (none for the overlay scenario, the idle-throttle flags for the overlay+idle scenario) and
--hit-test-probe <Probe>. A Probe that is not "x,y" with base-10 coordinates is a FAIL (re-record required) and the
player is not launched. A run that timed out, exited non-zero, wrote no capture or logged no usable fingerprints is a
FAIL (see Test-PlayerRunSucceeded). All check lines use Kind.
.OUTPUTS
The run (see Invoke-PlayerScene) when it can be checked; otherwise $null, after recording a FAIL.
#>
function Invoke-OverlayProbeRun {
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet('overlay', 'overlayIdle')]
        [string]$Kind,

        [Parameter(Mandatory = $true)]
        [string]$SceneId,

        [Parameter(Mandatory = $true)]
        [ValidateSet('transparent', 'opaque')]
        [string]$ProbeName,

        [Parameter(Mandatory = $true)]
        [string]$Probe,

        [Parameter(Mandatory = $true)]
        [string]$CapturePath,

        [Parameter(Mandatory = $true)]
        [string]$DiffPath,

        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [string[]]$ExtraArguments
    )

    # A diff image left by an earlier run must never be mistaken for this run's, so it is deleted before the run.
    if (Test-Path -LiteralPath $DiffPath -PathType Leaf) {
        Remove-Item -LiteralPath $DiffPath -Force
    }
    # The probe comes from the manifest (Verify) or find-probes (Record) and is passed to the player as
    # --hit-test-probe, so anything but two base-10 coordinates means the record is damaged: a FAIL, never a launch.
    if ($Probe -notmatch '^\d+,\d+$') {
        Add-CheckResult -Status FAIL -Kind $Kind -Name $SceneId -Detail "$ProbeName probe '$Probe' is not x,y (re-record required)"
        return $null
    }
    $overlayRun = Invoke-PlayerScene -SceneId $SceneId -CapturePath $CapturePath -ExtraArguments ($script:overlayPlayerArguments + $ExtraArguments + @('--hit-test-probe', $Probe))
    if (-not (Test-PlayerRunSucceeded -PlayerRun $overlayRun -Kind $Kind -SceneId $SceneId -CapturePath $CapturePath)) {
        return $null
    }
    return $overlayRun
}

<#
.SYNOPSIS
Runs the opt-in overlay scenario once on a scene with one hit-test probe and records its checks as "overlay" check lines.
.DESCRIPTION
Launches the player through Invoke-OverlayProbeRun (overlay window flags and --hit-test-probe <Probe>). The run's
HIT_TEST line must report ExpectedClickThrough at the probe with the matching WS_EX_TRANSPARENT bit (and, when
ExpectedExStyle is given, exactly that exStyle; see Test-OverlayHitTest); its capture, read with alpha, must pass
check-premultiplied and match the overlay golden with compare-rgba (the four-channel comparison; the opaque-forcing
compare is never used on an overlay capture). Fingerprint lines are reported under the name
<scene>.overlay.<probe name>@<window>, so that they are never confused with the scene's normal-run fingerprint lines.
With -RecordMode (Record's probe runs, before anything is recorded for the probe) every window's fingerprint must be
healthy and describe the overlay window (Test-OverlayFingerprintShape); otherwise the run's fingerprints must match
RecordedFingerprintsByWindow, this probe's recorded fingerprints, window for window and field for field.
.OUTPUTS
A [pscustomobject] with FingerprintsByWindow (the run's fingerprints) and ExStyle (the observed HIT_TEST exStyle) when
the run and its HIT_TEST check passed, for Record to store; otherwise $null.
#>
function Invoke-OverlayScenario {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SceneId,

        [Parameter(Mandatory = $true)]
        [ValidateSet('transparent', 'opaque')]
        [string]$ProbeName,

        [Parameter(Mandatory = $true)]
        [string]$Probe,

        [Parameter(Mandatory = $true)]
        [ValidateSet('on', 'off')]
        [string]$ExpectedClickThrough,

        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [string]$ExpectedExStyle,

        [Parameter(Mandatory = $true)]
        [string]$CapturePath,

        [Parameter(Mandatory = $true)]
        [string]$DiffPath,

        [Parameter(Mandatory = $true)]
        [string]$GoldenPath,

        [Parameter()]
        [switch]$RecordMode,

        [Parameter()]
        [System.Collections.Specialized.OrderedDictionary]$RecordedFingerprintsByWindow
    )

    if ($RecordMode -and $null -ne $RecordedFingerprintsByWindow) {
        throw "Invoke-OverlayScenario takes either -RecordMode or -RecordedFingerprintsByWindow, not both."
    }
    if (-not $RecordMode -and $null -eq $RecordedFingerprintsByWindow) {
        throw "Invoke-OverlayScenario requires -RecordedFingerprintsByWindow outside -RecordMode."
    }
    # Checked before the launch, so a bad call never costs a player run; Test-OverlayHitTest enforces the same rule.
    if ($RecordMode -and $ExpectedExStyle.Length -gt 0) {
        throw "Invoke-OverlayScenario -RecordMode takes no ExpectedExStyle (got '$ExpectedExStyle')."
    }
    if (-not $RecordMode -and $ExpectedExStyle -cnotmatch '^0x[0-9A-F]{8}$') {
        throw "Invoke-OverlayScenario requires ExpectedExStyle as 0x<8 upper-case hex digits outside -RecordMode (got '$ExpectedExStyle')."
    }

    $overlayRun = Invoke-OverlayProbeRun -Kind overlay -SceneId $SceneId -ProbeName $ProbeName -Probe $Probe -CapturePath $CapturePath -DiffPath $DiffPath -ExtraArguments @()
    if ($null -eq $overlayRun) {
        return $null
    }

    $observedExStyle = Test-OverlayHitTest -Kind overlay -SceneId $SceneId -ProbeName $ProbeName -Probe $Probe -ExpectedClickThrough $ExpectedClickThrough -ExpectedExStyle $ExpectedExStyle -HitTestLine "$($overlayRun.HitTest)" -RecordMode:$RecordMode
    Test-OverlayPremultiplied -Kind overlay -SceneId $SceneId -ProbeName $ProbeName -CapturePath $CapturePath
    Test-CaptureMatchesGolden -Kind overlay -SceneId $SceneId -CompareCommand compare-rgba -CapturePath $CapturePath -GoldenPath $GoldenPath -DiffPath $DiffPath -Description "$ProbeName run capture" -GoldenLabel 'overlay golden'

    $fingerprintName = "$SceneId.overlay.$ProbeName"
    if ($RecordMode) {
        Invoke-FingerprintHealthCheck -Name $fingerprintName -FingerprintsByWindow $overlayRun.FingerprintsByWindow -PassDetail "$ProbeName probe run healthy"
        foreach ($overlayWindowName in $overlayRun.FingerprintsByWindow.Keys) {
            Test-OverlayFingerprintShape -SceneId $SceneId -FingerprintLine $overlayRun.FingerprintsByWindow[$overlayWindowName] -Kind overlay
        }
    }
    else {
        Compare-FingerprintsByWindow -Name $fingerprintName -RecordedFingerprintsByWindow $RecordedFingerprintsByWindow -ActualFingerprintsByWindow $overlayRun.FingerprintsByWindow -PassDetail 'matches record'
    }

    if ($null -eq $observedExStyle) {
        return $null
    }
    return [pscustomobject]@{ FingerprintsByWindow = $overlayRun.FingerprintsByWindow; ExStyle = $observedExStyle }
}

<#
.SYNOPSIS
Runs the opt-in overlay+idle scenario once on a scene with the transparent probe and records its checks as
"overlayIdle" check lines.
.DESCRIPTION
Launches the player through Invoke-OverlayProbeRun with the overlay window flags plus the idle-throttle flags
($script:idlePlayerArguments) and --hit-test-probe <Probe>, the recorded transparent probe. The run must exit 0 and:
log HIT_TEST clickThrough=on with WS_EX_TRANSPARENT (0x20) set and exactly ExpectedExStyle, the transparent probe's
recorded exStyle; pass check-idle on every window with MinimumIdleFrames and MinimumElapsedMilliseconds (the idle
entry's minimums); describe the overlay window in every window's fingerprint (Test-OverlayFingerprintShape:
windowMode=overlay, alpha=1, the overlay exStyle bits); pass check-premultiplied; and match the overlay golden with compare-rgba, because neither the idle
throttle nor the overlay window may change what a frame renders. Its fingerprints are never compared with recorded
ones: the idle and active frame counts and the elapsed time differ from a normal run by design, and check-idle already
checks them.
.OUTPUTS
The run's FingerprintsByWindow (Record stores them in the overlayIdle entry for reference); $null when the run failed.
#>
function Invoke-OverlayIdleScenario {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SceneId,

        [Parameter(Mandatory = $true)]
        [string]$Probe,

        [Parameter(Mandatory = $true)]
        [string]$ExpectedExStyle,

        [Parameter(Mandatory = $true)]
        [string]$CapturePath,

        [Parameter(Mandatory = $true)]
        [string]$DiffPath,

        [Parameter(Mandatory = $true)]
        [string]$GoldenPath,

        [Parameter(Mandatory = $true)]
        [string]$MinimumIdleFrames,

        [Parameter(Mandatory = $true)]
        [string]$MinimumElapsedMilliseconds
    )

    # The caller validates the recorded value; an invalid one here is a bug, so it throws before any launch.
    if ($ExpectedExStyle -cnotmatch '^0x[0-9A-F]{8}$') {
        throw "Invoke-OverlayIdleScenario requires ExpectedExStyle as 0x<8 upper-case hex digits (got '$ExpectedExStyle')."
    }
    $overlayIdleRun = Invoke-OverlayProbeRun -Kind overlayIdle -SceneId $SceneId -ProbeName transparent -Probe $Probe -CapturePath $CapturePath -DiffPath $DiffPath -ExtraArguments $script:idlePlayerArguments
    if ($null -eq $overlayIdleRun) {
        return $null
    }

    $null = Test-OverlayHitTest -Kind overlayIdle -SceneId $SceneId -ProbeName transparent -Probe $Probe -ExpectedClickThrough on -ExpectedExStyle $ExpectedExStyle -HitTestLine "$($overlayIdleRun.HitTest)"
    Test-IdleFingerprints -Kind overlayIdle -SceneId $SceneId -FingerprintsByWindow $overlayIdleRun.FingerprintsByWindow -MinimumIdleFrames $MinimumIdleFrames -MinimumElapsedMilliseconds $MinimumElapsedMilliseconds
    # The fingerprints are never compared field by field, so every window's overlay shape is checked on its own.
    foreach ($overlayIdleWindowName in $overlayIdleRun.FingerprintsByWindow.Keys) {
        Test-OverlayFingerprintShape -SceneId $SceneId -FingerprintLine $overlayIdleRun.FingerprintsByWindow[$overlayIdleWindowName] -Kind overlayIdle
    }
    Test-OverlayPremultiplied -Kind overlayIdle -SceneId $SceneId -ProbeName transparent -CapturePath $CapturePath
    Test-CaptureMatchesGolden -Kind overlayIdle -SceneId $SceneId -CompareCommand compare-rgba -CapturePath $CapturePath -GoldenPath $GoldenPath -DiffPath $DiffPath -Description 'overlay idle run capture' -GoldenLabel 'overlay golden'
    return $overlayIdleRun.FingerprintsByWindow
}

<#
.SYNOPSIS
Returns the upper-case hexadecimal SHA-256 of a text's UTF-8 bytes.
#>
function Get-TextSha256 {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [string]$Text
    )

    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try {
        $hashBytes = $sha256.ComputeHash($script:utf8WithoutBom.GetBytes($Text))
    }
    finally {
        $sha256.Dispose()
    }
    return ([System.BitConverter]::ToString($hashBytes)).Replace('-', '')
}

<#
.SYNOPSIS
Returns one SHA-256 for a whole folder tree: every file's forward-slash relative path and SHA-256, one per line,
sorted ordinally and then hashed.
.DESCRIPTION
Files under any bin or obj folder are skipped, matching the robocopy /XD bin obj used to copy the tree.
#>
function Get-TreeSha256 {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RootPath
    )

    $normalizedRootPath = [System.IO.Path]::GetFullPath($RootPath).TrimEnd('\', '/') + '\'
    $treeEntries = New-Object System.Collections.Generic.List[string]
    foreach ($treeFile in (Get-ChildItem -LiteralPath $RootPath -Recurse -File -Force)) {
        $relativePath = $treeFile.FullName.Substring($normalizedRootPath.Length).Replace('\', '/')
        if ($relativePath -match '(^|/)(bin|obj)/') {
            continue
        }
        $treeEntries.Add($relativePath + ' ' + (Get-FileHash -LiteralPath $treeFile.FullName -Algorithm SHA256).Hash)
    }
    $treeEntries.Sort([System.StringComparer]::Ordinal)
    return Get-TextSha256 -Text ($treeEntries -join "`n")
}

<#
.SYNOPSIS
Copies a passed record from the staging folder into regression\golden and regression\baselines.
.DESCRIPTION
The committed goldens (*.png) and manifest.json are replaced as a whole, so a scene that no longer gets a golden
leaves no stale file behind. Baseline files are overwritten one by one; files that Record never writes (the
hand-maintained <suite>.flaky.txt lists) are kept.
#>
function Publish-RecordStaging {
    param(
        [Parameter(Mandatory = $true)]
        [string]$StagingGoldenRootPath,

        [Parameter(Mandatory = $true)]
        [string]$StagingBaselineRootPath,

        [Parameter(Mandatory = $true)]
        [string]$GoldenRootPath,

        [Parameter(Mandatory = $true)]
        [string]$BaselineRootPath
    )

    New-Item -ItemType Directory -Path $GoldenRootPath -Force | Out-Null
    New-Item -ItemType Directory -Path $BaselineRootPath -Force | Out-Null
    Get-ChildItem -LiteralPath $GoldenRootPath -Filter '*.png' | Remove-Item -Force
    $committedManifestPath = Join-Path $GoldenRootPath 'manifest.json'
    if (Test-Path -LiteralPath $committedManifestPath) {
        Remove-Item -LiteralPath $committedManifestPath -Force
    }
    Get-ChildItem -LiteralPath $StagingGoldenRootPath -File | Copy-Item -Destination $GoldenRootPath -Force
    Get-ChildItem -LiteralPath $StagingBaselineRootPath -File | Copy-Item -Destination $BaselineRootPath -Force
}

<#
.SYNOPSIS
Checks that a scene's capture is not a blank (stuck) frame and records the smoke check's outcome.
#>
function Test-SmokeCapture {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SceneId,

        [Parameter(Mandatory = $true)]
        [string]$CapturePath
    )

    $blankCheckRun = Invoke-RegressionTool -ToolArguments @('blank-check', $CapturePath)
    $blankCheckDetail = $blankCheckRun.Lines -join ' '
    if ($blankCheckRun.ExitCode -eq 0 -and $blankCheckDetail -eq 'NOT_BLANK') {
        Add-CheckResult -Status PASS -Kind smoke -Name $SceneId -Detail $blankCheckDetail
    }
    else {
        Add-CheckResult -Status FAIL -Kind smoke -Name $SceneId -Detail $blankCheckDetail
    }
}

<#
.SYNOPSIS
Runs one dotnet test suite with a TRX logger and writes its sorted failing-test names to <WorkRoot>\trx\<suite>.failing.txt.
.DESCRIPTION
The suite's console output goes to <WorkRoot>\trx\<suite>.log. dotnet test returns non-zero whenever a test fails,
which is expected here (known failures live in the baseline), so only a missing TRX file is treated as an error.
.OUTPUTS
A [pscustomobject] with FailingListPath (the written failing-test list) and TrxPath (the suite's TRX file, whose
ResultSummary is checked for aborted or truncated runs).
#>
function Invoke-TestSuite {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SuiteName,

        [Parameter(Mandatory = $true)]
        [string]$ProjectPath,

        [Parameter()]
        [string[]]$ExtraArguments = @()
    )

    $trxRootPath = Join-Path $script:WorkRoot 'trx'
    New-Item -ItemType Directory -Path $trxRootPath -Force | Out-Null
    $trxPath = Join-Path $trxRootPath "$SuiteName.trx"
    $suiteLogPath = Join-Path $trxRootPath "$SuiteName.log"
    $failingListPath = Join-Path $trxRootPath "$SuiteName.failing.txt"
    foreach ($stalePath in @($trxPath, $failingListPath)) {
        if (Test-Path -LiteralPath $stalePath) {
            Remove-Item -LiteralPath $stalePath -Force
        }
    }

    Write-Host "TEST $SuiteName (log: $suiteLogPath)"
    $testArguments = @('test', $ProjectPath, '--logger', "trx;LogFileName=$SuiteName.trx", '--results-directory', $trxRootPath, '-m:1') + $ExtraArguments
    # Native stderr lines must not become terminating errors while the output is redirected into the log.
    $ErrorActionPreference = 'Continue'
    & dotnet @testArguments 2>&1 | ForEach-Object { "$_" } | Set-Content -LiteralPath $suiteLogPath -Encoding UTF8
    $testExitCode = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'

    if (-not (Test-Path -LiteralPath $trxPath -PathType Leaf)) {
        throw "dotnet test produced no TRX file for $SuiteName (exit code $testExitCode): $trxPath. See $suiteLogPath."
    }

    $trxFailingRun = Invoke-RegressionTool -ToolArguments @('trx-failing', $trxPath, $failingListPath)
    if ($trxFailingRun.ExitCode -ne 0) {
        throw "Reading the failing tests of $SuiteName failed: $($trxFailingRun.Lines -join ' ')"
    }
    return [pscustomobject]@{ FailingListPath = $failingListPath; TrxPath = $trxPath }
}

<#
.SYNOPSIS
Resolves a commit of the project source to its full SHA and throws when the project source does not have it.
.DESCRIPTION
The commit is checked with "git cat-file -e <commit>^{commit}" and resolved with "git rev-parse --verify". Git's own
error output is suppressed; the thrown message names the commit, the project source and what the commit was for
(Purpose), for example "recorded DemoDisc commit".
.OUTPUTS
The full 40-hex SHA of the commit.
#>
function Resolve-ProjectCommit {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectSourcePath,

        [Parameter(Mandatory = $true)]
        [string]$CommitText,

        [Parameter(Mandatory = $true)]
        [string]$Purpose
    )

    # Native stderr must not become a terminating error here; the exit code decides.
    $ErrorActionPreference = 'Continue'
    & git -C $ProjectSourcePath cat-file -e "$CommitText^{commit}" 2>$null
    $commitExitCode = $LASTEXITCODE
    $resolvedCommit = $null
    if ($commitExitCode -eq 0) {
        $resolvedCommit = (& git -C $ProjectSourcePath rev-parse --verify "$CommitText^{commit}" 2>$null)
        $commitExitCode = $LASTEXITCODE
    }
    $ErrorActionPreference = 'Stop'
    if ($commitExitCode -ne 0 -or $null -eq $resolvedCommit) {
        throw "The $Purpose '$CommitText' was not found in the project source $ProjectSourcePath (git cat-file -e failed)."
    }
    return "$resolvedCommit".Trim()
}

$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path
$HelengineRoot = [System.IO.Path]::GetFullPath($HelengineRoot)
$ProjectSource = [System.IO.Path]::GetFullPath($ProjectSource)
$WorkRoot = [System.IO.Path]::GetFullPath($WorkRoot)
$PlatformsManifestPath = [System.IO.Path]::GetFullPath($PlatformsManifestPath)
Assert-WorkRootIsolated -WorkRootPath $WorkRoot -ProtectedRootPaths @($ProjectSource, $HelengineRoot, $RepoRoot)
$utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)

# The build stage deletes and rewrites folders under the work root, so only a folder this script created (it carries
# the marker file) or an empty or missing one may be used.
$workRootMarkerPath = Join-Path $WorkRoot '.helengine-regression-workroot'
if (Test-Path -LiteralPath $WorkRoot) {
    if (-not (Test-Path -LiteralPath $WorkRoot -PathType Container)) {
        throw "The work root '$WorkRoot' is a file, not a folder."
    }
    $workRootHasEntries = @(Get-ChildItem -LiteralPath $WorkRoot -Force | Select-Object -First 1).Count -gt 0
    if ($workRootHasEntries -and -not (Test-Path -LiteralPath $workRootMarkerPath -PathType Leaf)) {
        throw "The work root '$WorkRoot': refusing to use a non-empty folder that was not created by run-regression.ps1 (it has no .helengine-regression-workroot marker)."
    }
}
New-Item -ItemType Directory -Path $WorkRoot -Force | Out-Null
if (-not (Test-Path -LiteralPath $workRootMarkerPath -PathType Leaf)) {
    [System.IO.File]::WriteAllText($workRootMarkerPath, "Work root of scripts\run-regression.ps1 (helengine-windows). Its contents are rebuilt on every run.`n", $utf8WithoutBom)
}
$CheckResults = New-Object System.Collections.Generic.List[string]
$tarExecutablePath = Join-Path $env:SystemRoot 'System32\tar.exe'

$buildPlatformScriptPath = Join-Path $HelengineRoot 'scripts\build-platform.ps1'
if (-not (Test-Path -LiteralPath $buildPlatformScriptPath -PathType Leaf)) {
    throw "The helengine build script was not found: $buildPlatformScriptPath"
}
if (-not (Test-Path -LiteralPath (Join-Path $ProjectSource 'project.heproj') -PathType Leaf)) {
    throw "The project source has no project.heproj: $ProjectSource"
}
if (-not (Test-Path -LiteralPath (Join-Path $ProjectSource 'user_settings\build_config.json') -PathType Leaf)) {
    throw "The project source has no user_settings\build_config.json: $ProjectSource"
}
if (-not (Test-Path -LiteralPath $tarExecutablePath -PathType Leaf)) {
    throw "Windows tar was not found: $tarExecutablePath"
}
if (-not (Test-Path -LiteralPath $PlatformsManifestPath -PathType Leaf)) {
    throw "The platform manifest was not found: $PlatformsManifestPath"
}

# 1. Pick the project commit to build, extract it into the work root, then add the git-ignored user_settings folder.
#    Verify is pinned: it always builds the projectCommit recorded in the committed manifest and never follows the
#    project's HEAD, so a project that moved on cannot change what the player is tested against. Record and BuildOnly
#    build -ProjectCommit (default: the project's HEAD); Record writes it into the manifest as the new pin.
$projectHeadCommit = (& git -C $ProjectSource rev-parse HEAD)
if ($LASTEXITCODE -ne 0) {
    throw "Reading the project source's HEAD commit failed with exit code $LASTEXITCODE."
}
$projectHeadCommit = $projectHeadCommit.Trim()
$goldenRootPath = Join-Path $RepoRoot 'regression\golden'
$manifestPath = Join-Path $goldenRootPath 'manifest.json'
if ($Verify) {
    if ($ProjectCommit.Length -gt 0) {
        throw "-ProjectCommit cannot be used with -Verify: Verify always builds the projectCommit recorded in $manifestPath."
    }
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "Verify builds the project commit recorded in the manifest, but the manifest is missing (re-record required): $manifestPath"
    }
    $pinnedManifest = [System.IO.File]::ReadAllText($manifestPath) | ConvertFrom-Json
    if ([string]::IsNullOrWhiteSpace("$($pinnedManifest.projectCommit)")) {
        throw "The manifest has no projectCommit to pin the project to (re-record required): $manifestPath"
    }
    $builtProjectCommit = Resolve-ProjectCommit -ProjectSourcePath $ProjectSource -CommitText "$($pinnedManifest.projectCommit)" -Purpose 'recorded DemoDisc commit'
}
elseif ($ProjectCommit.Length -gt 0) {
    $builtProjectCommit = Resolve-ProjectCommit -ProjectSourcePath $ProjectSource -CommitText $ProjectCommit -Purpose '-ProjectCommit'
}
else {
    $builtProjectCommit = $projectHeadCommit
}
Write-Output ("PROJECT_HEAD=" + $projectHeadCommit)

New-Item -ItemType Directory -Path $WorkRoot -Force | Out-Null
$projectRootPath = Join-Path $WorkRoot 'project'
if (Test-Path -LiteralPath $projectRootPath) {
    Remove-Item -LiteralPath $projectRootPath -Recurse -Force
}
New-Item -ItemType Directory -Path $projectRootPath -Force | Out-Null

$projectArchivePath = Join-Path $WorkRoot 'project-source.tar'
& git -C $ProjectSource archive --format=tar -o $projectArchivePath $builtProjectCommit
if ($LASTEXITCODE -ne 0) {
    throw "Archiving the project source's commit $builtProjectCommit failed with exit code $LASTEXITCODE."
}
& $tarExecutablePath -xf $projectArchivePath -C $projectRootPath
if ($LASTEXITCODE -ne 0) {
    throw "Extracting the project archive failed with exit code $LASTEXITCODE."
}
Remove-Item -LiteralPath $projectArchivePath -Force

# git archive exports Git LFS pointer files instead of the real content, so a project that uses LFS cannot be built
# from the archive.
$copiedGitAttributesPath = Join-Path $projectRootPath '.gitattributes'
if ((Test-Path -LiteralPath $copiedGitAttributesPath -PathType Leaf) -and ([System.IO.File]::ReadAllText($copiedGitAttributesPath) -match 'filter=lfs')) {
    throw "The project's .gitattributes uses Git LFS (filter=lfs); git archive would export LFS pointer files instead of the assets: $copiedGitAttributesPath"
}

# Hash the project's own user_settings inputs before the copy's build_config.json is overridden below. The copy leaves
# out bin and obj folders, and so does the generated code hash.
$sourceGeneratedCodeRootPath = Join-Path $ProjectSource 'user_settings\generated_code'
if (-not (Test-Path -LiteralPath $sourceGeneratedCodeRootPath -PathType Container)) {
    throw "The project source has no user_settings\generated_code folder: $ProjectSource"
}
$buildConfigSourceHash = (Get-FileHash -LiteralPath (Join-Path $ProjectSource 'user_settings\build_config.json') -Algorithm SHA256).Hash
$generatedCodeHash = Get-TreeSha256 -RootPath $sourceGeneratedCodeRootPath

& robocopy (Join-Path $ProjectSource 'user_settings') (Join-Path $projectRootPath 'user_settings') /E /XD bin obj /NFL /NDL /NJH /NJS
if ($LASTEXITCODE -ge 8) {
    throw "Copying the project's user_settings failed with robocopy exit code $LASTEXITCODE."
}
$global:LASTEXITCODE = 0

# 2. Write isolated engine user settings whose windows entry points at this checkout. They are generated from
#    -PlatformsManifestPath (default: the helengine root's user_settings\platforms.json); its relative paths are made
#    absolute against the folder of that manifest file.
$platformsManifestRootPath = [System.IO.Path]::GetDirectoryName($PlatformsManifestPath)
$platformsSource = [System.IO.File]::ReadAllText($PlatformsManifestPath)
$platformsDocument = $platformsSource | ConvertFrom-Json
$windowsPlatform = $null
foreach ($platform in $platformsDocument.platforms) {
    if ($platform.platformId -eq 'windows') {
        $windowsPlatform = $platform
    }
}
if ($null -eq $windowsPlatform) {
    throw "No 'windows' platform entry was found in $PlatformsManifestPath."
}

$pathPropertyNames = @('builderAssemblyPath', 'playerSourceRootPath', 'generatedCoreCppRootPath', 'codegenToolPath', 'pluginManifestPath')
foreach ($platform in $platformsDocument.platforms) {
    foreach ($pathPropertyName in $pathPropertyNames) {
        $pathProperty = $platform.PSObject.Properties[$pathPropertyName]
        if ($null -ne $pathProperty -and -not [string]::IsNullOrWhiteSpace($pathProperty.Value) -and -not [System.IO.Path]::IsPathRooted($pathProperty.Value)) {
            $pathProperty.Value = [System.IO.Path]::GetFullPath((Join-Path $platformsManifestRootPath $pathProperty.Value))
        }
    }
}

# A build against another helengine root (for example a worktree) reads the main checkout's manifest, whose generated
# output paths point into that checkout. Move every output path under the manifest's helengine root to the same relative
# path under -HelengineRoot, so the build writes nothing into the checkout the manifest came from. The engine treats a
# missing generated-core folder as "not installed", so the moved folder is created. A default run (the manifest belongs
# to -HelengineRoot) changes nothing.
$outputPathPropertyNames = @('generatedCoreCppRootPath')
$platformsManifestHelengineRootPath = [System.IO.Path]::GetDirectoryName($platformsManifestRootPath).TrimEnd('\')
if (-not [string]::Equals($platformsManifestHelengineRootPath, $HelengineRoot.TrimEnd('\'), [System.StringComparison]::OrdinalIgnoreCase)) {
    foreach ($platform in $platformsDocument.platforms) {
        foreach ($outputPathPropertyName in $outputPathPropertyNames) {
            $outputPathProperty = $platform.PSObject.Properties[$outputPathPropertyName]
            if ($null -ne $outputPathProperty -and -not [string]::IsNullOrWhiteSpace($outputPathProperty.Value) -and $outputPathProperty.Value.StartsWith($platformsManifestHelengineRootPath + '\', [System.StringComparison]::OrdinalIgnoreCase) -and -not $outputPathProperty.Value.StartsWith($HelengineRoot.TrimEnd('\') + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
                $outputPathRelativePath = $outputPathProperty.Value.Substring($platformsManifestHelengineRootPath.Length + 1)
                $outputPathProperty.Value = [System.IO.Path]::GetFullPath((Join-Path $HelengineRoot $outputPathRelativePath))
                New-Item -ItemType Directory -Path $outputPathProperty.Value -Force | Out-Null
            }
        }
    }
}
$windowsPlatform.builderAssemblyPath = Join-Path $RepoRoot 'builder\bin\Debug\net9.0\helengine.windows.builder.dll'
$windowsPlatform.playerSourceRootPath = $RepoRoot

$engineUserSettingsRootPath = Join-Path $WorkRoot 'engine-user-settings'
New-Item -ItemType Directory -Path $engineUserSettingsRootPath -Force | Out-Null
[System.IO.File]::WriteAllText((Join-Path $engineUserSettingsRootPath 'platforms.json'), ($platformsDocument | ConvertTo-Json -Depth 20), $utf8WithoutBom)
Write-Output ("GENERATED_CORE_ROOT=" + $windowsPlatform.generatedCoreCppRootPath)

# 3. Align the copied project's engine version with the windows platform entry.
$projectFilePath = Join-Path $projectRootPath 'project.heproj'
$projectDocument = [System.IO.File]::ReadAllText($projectFilePath) | ConvertFrom-Json
$projectDocument.requiredEngineVersion = $windowsPlatform.engineVersion
[System.IO.File]::WriteAllText($projectFilePath, ($projectDocument | ConvertTo-Json -Depth 20), $utf8WithoutBom)

# 4. Write the copied project's windows build config: DemoDisc's own rendering scenes (ordinal by path; the first is
#    the smoke scene) as a local scene override, and a 640x360 window.
#    Why rendering scenes only: this net guards the Windows player host, not DemoDisc gameplay. The editor transpiles
#    only the code modules that the cooked scenes reference, so leaving out the menu and game scenes keeps unrelated
#    gameplay modules (for example TiltPlay) out of the build.
#    The references are copied verbatim from the project's committed windows scene package (settings\build_config.json):
#    the editor requires file references to carry the scene's 32-hex asset id, and it silently discards a local build
#    config that fails to load, falling back to the project's full scene package.
$buildConfigFilePath = Join-Path $projectRootPath 'user_settings\build_config.json'
$buildConfigDocument = [System.IO.File]::ReadAllText($buildConfigFilePath) | ConvertFrom-Json
$windowsBuildConfig = $null
foreach ($platformConfig in $buildConfigDocument.platforms) {
    if ($platformConfig.platformId -eq 'windows') {
        $windowsBuildConfig = $platformConfig
    }
}
if ($null -eq $windowsBuildConfig) {
    throw "No 'windows' platform entry was found in the project's user_settings\build_config.json."
}

$projectBuildConfigFilePath = Join-Path $projectRootPath 'settings\build_config.json'
$projectBuildConfigDocument = [System.IO.File]::ReadAllText($projectBuildConfigFilePath) | ConvertFrom-Json
$windowsScenePackage = $null
foreach ($platformConfig in $projectBuildConfigDocument.platforms) {
    if ($platformConfig.platformId -eq 'windows') {
        $windowsScenePackage = $platformConfig
    }
}
if ($null -eq $windowsScenePackage) {
    throw "No 'windows' scene package was found in the project's settings\build_config.json."
}

$renderingSceneReferencesByPath = New-Object 'System.Collections.Generic.SortedDictionary[string,object]' ([System.StringComparer]::Ordinal)
foreach ($sceneReference in $windowsScenePackage.selectedSceneReferences) {
    if ($sceneReference.relativePath.StartsWith('scenes/rendering/', [System.StringComparison]::Ordinal)) {
        if ($sceneReference.assetId -cnotmatch '^[0-9a-f]{32}$') {
            throw "The project's windows scene reference '$($sceneReference.relativePath)' has no valid asset id."
        }
        $renderingSceneReferencesByPath[$sceneReference.relativePath] = $sceneReference
    }
}
if ($renderingSceneReferencesByPath.Count -eq 0) {
    throw "The project's windows scene package selects no scenes under scenes/rendering/."
}

$selectedSceneReferences = New-Object System.Collections.Generic.List[object]
$sceneOrders = New-Object System.Collections.Generic.List[object]
foreach ($sceneReference in $renderingSceneReferencesByPath.Values) {
    $selectedSceneReferences.Add($sceneReference)
    $sceneOrders.Add([pscustomobject][ordered]@{ sceneReference = $sceneReference; orderNumber = $selectedSceneReferences.Count })
}

$playerOutputPath = Join-Path $WorkRoot 'player'
$windowsBuildConfig.selectedSceneReferences = $selectedSceneReferences.ToArray()
$windowsBuildConfig.sceneOrders = $sceneOrders.ToArray()
$windowsBuildConfig.overridesProjectScenes = $true
$windowsBuildConfig.outputDirectoryPath = $playerOutputPath.Replace('\', '/')
$graphicsOptionValues = $windowsBuildConfig.selectedGraphicsOptionValues
if ($null -eq $graphicsOptionValues) {
    throw "The project's windows build config has no selectedGraphicsOptionValues."
}
# Only the window size is overridden; every other graphics option keeps the project's windows value.
$graphicsOptionValues | Add-Member -NotePropertyName 'default-width' -NotePropertyValue '640' -Force
$graphicsOptionValues | Add-Member -NotePropertyName 'default-height' -NotePropertyValue '360' -Force
[System.IO.File]::WriteAllText($buildConfigFilePath, ($buildConfigDocument | ConvertTo-Json -Depth 20), $utf8WithoutBom)

# 5. Build this checkout's builder so the isolated platform entry's builderAssemblyPath exists and is current.
& dotnet build "$RepoRoot\builder\helengine.windows.builder.csproj" -c Debug "-p:HelEngineRoot=$HelengineRoot"
if ($LASTEXITCODE -ne 0) {
    throw "Building the Windows builder failed with exit code $LASTEXITCODE."
}

# 6. Build the player through the canonical helengine build script, with the isolated engine user settings.
$buildStartedAt = Get-Date
$previousEngineUserSettingsRoot = $env:HELENGINE_ENGINE_USER_SETTINGS_ROOT
$env:HELENGINE_ENGINE_USER_SETTINGS_ROOT = $engineUserSettingsRootPath
try {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $buildPlatformScriptPath -Project $projectFilePath -Platform windows -Output $playerOutputPath -Configuration Debug
    $buildExitCode = $LASTEXITCODE
}
finally {
    $env:HELENGINE_ENGINE_USER_SETTINGS_ROOT = $previousEngineUserSettingsRoot
}
if ($buildExitCode -ne 0) {
    throw "build-platform.ps1 failed with exit code $buildExitCode."
}

# 7. Confirm the build produced a fresh player executable.
$playerExecutablePath = Join-Path $playerOutputPath 'helengine_windows.exe'
if (-not (Test-Path -LiteralPath $playerExecutablePath -PathType Leaf)) {
    throw "The build did not produce the player executable: $playerExecutablePath"
}
if ((Get-Item -LiteralPath $playerExecutablePath).LastWriteTime -lt $buildStartedAt) {
    throw "The player executable was not rewritten by this build: $playerExecutablePath"
}

Write-Output ("PROJECT_COMMIT=" + $builtProjectCommit)
Write-Output ("PLAYER_SOURCE_ROOT=" + $RepoRoot)
Write-Output ("PLAYER=" + $playerExecutablePath)

if ($BuildOnly) {
    exit 0
}

# 8. Derive the scene ids in build_config.json order. The player's runtime catalog names each scene by its file stem
#    (for example scenes/rendering/cube_test.helen is 'cube_test'), and --scene must match that id exactly.
#    The first scene is also the smoke scene.
$sceneIds = New-Object System.Collections.Generic.List[string]
foreach ($sceneReference in $selectedSceneReferences) {
    $sceneId = [System.IO.Path]::GetFileNameWithoutExtension($sceneReference.relativePath)
    if ($sceneIds.Contains($sceneId)) {
        throw "Two selected scenes share the catalog id '$sceneId'."
    }
    $sceneIds.Add($sceneId)
}
$smokeSceneId = $sceneIds[0]

# 9. Build the regression tool once and run it by its executable path.
& dotnet build "$RepoRoot\tools\regression\helengine.windows.regression.csproj" -c Release
if ($LASTEXITCODE -ne 0) {
    throw "Building the regression tool failed with exit code $LASTEXITCODE."
}
$regressionToolPath = Join-Path $RepoRoot 'tools\regression\bin\Release\net9.0-windows\helengine.windows.regression.exe'
if (-not (Test-Path -LiteralPath $regressionToolPath -PathType Leaf)) {
    throw "The regression tool executable was not found: $regressionToolPath"
}

# 10. Read the provenance of this run: the project commit (already read) and the helengine checkout's HEAD and
#     dirty state, since the editor test suites run against that checkout as it is on disk.
$helengineCommit = (& git -C $HelengineRoot rev-parse HEAD)
if ($LASTEXITCODE -ne 0) {
    throw "Reading the helengine checkout's HEAD commit failed with exit code $LASTEXITCODE."
}
$helengineCommit = $helengineCommit.Trim()
$helengineStatusLines = @(& git -C $HelengineRoot status --porcelain)
if ($LASTEXITCODE -ne 0) {
    throw "Reading the helengine checkout's status failed with exit code $LASTEXITCODE."
}
$helengineDirty = ($helengineStatusLines.Count -gt 0)
# The dirty flag cannot tell two different uncommitted states apart, so the uncommitted work itself is hashed too: the
# tracked changes against HEAD plus the sorted list of untracked (not ignored) files.
$helengineDiffLines = @(& git -C $HelengineRoot diff HEAD --no-ext-diff --no-color)
if ($LASTEXITCODE -ne 0) {
    throw "Reading the helengine checkout's diff against HEAD failed with exit code $LASTEXITCODE."
}
$helengineUntrackedPaths = New-Object System.Collections.Generic.List[string]
foreach ($untrackedPath in @(& git -C $HelengineRoot ls-files --others --exclude-standard)) {
    $helengineUntrackedPaths.Add("$untrackedPath")
}
if ($LASTEXITCODE -ne 0) {
    throw "Listing the helengine checkout's untracked files failed with exit code $LASTEXITCODE."
}
$helengineUntrackedPaths.Sort([System.StringComparer]::Ordinal)
$helengineWorkingTreeHash = Get-TextSha256 -Text (($helengineDiffLines -join "`n") + "`n--untracked--`n" + ($helengineUntrackedPaths -join "`n"))
Write-Output ("HELENGINE_COMMIT=" + $helengineCommit)
Write-Output ("HELENGINE_DIRTY=" + $helengineDirty)

$baselineRootPath = Join-Path $RepoRoot 'regression\baselines'
$recordStagingRootPath = Join-Path $WorkRoot 'record-staging'
$stagingGoldenRootPath = Join-Path $recordStagingRootPath 'golden'
$stagingBaselineRootPath = Join-Path $recordStagingRootPath 'baselines'
$capturesRootPath = Join-Path $WorkRoot 'captures'
# The opt-in idle-throttle scenario: the smoke scene runs its usual 30 frames with the idle throttle on, going idle 1 ms
# after the last activity and pacing idle frames at 10 fps (about 100 ms each), so the run takes about 3 s.
$idleCapturePath = Join-Path $capturesRootPath "idle\$($smokeSceneId.Replace('/', '__')).bmp"
# Record writes the idle diff beside the idle capture; Verify redirects it into its wiped diffs folder.
$idleDiffPath = Join-Path $capturesRootPath "idle\$($smokeSceneId.Replace('/', '__')).diff.png"
$idlePlayerArguments = @('--idle-throttle', 'on', '--idle-after-ms', '1', '--idle-fps', '10')
$idleMinimumIdleFrames = '25'
$idleMinimumElapsedMilliseconds = '2400'
# The opt-in overlay scenario: the smoke scene runs its usual 30 frames in a transparent overlay window sized by the
# profile (640x360), with one --hit-test-probe per run. Its golden keeps the capture's alpha and sits beside the normal
# goldens as <smoke scene>.overlay.png. Record's first run uses $overlayRecordProbe, an arbitrary in-bounds point (the
# probe never changes the capture), because the real probes are only known once that run's golden exists.
$overlayPlayerArguments = @('--window-mode', 'overlay', '--overlay-bounds', 'profile', '--overlay-background', 'transparent')
$overlayRecordProbe = '2,2'
$overlayGoldenFileName = "$($smokeSceneId.Replace('/', '__')).overlay.png"
$overlayCaptureRootPath = Join-Path $capturesRootPath 'overlay'
$overlayTransparentCapturePath = Join-Path $overlayCaptureRootPath "$($smokeSceneId.Replace('/', '__')).transparent.bmp"
$overlayOpaqueCapturePath = Join-Path $overlayCaptureRootPath "$($smokeSceneId.Replace('/', '__')).opaque.bmp"
# The opt-in overlay+idle scenario: the overlay flags plus the idle-throttle flags and the transparent probe. Record
# writes its diff beside its capture; Verify redirects it into its wiped diffs folder.
$overlayIdleCapturePath = Join-Path $capturesRootPath "overlayIdle\$($smokeSceneId.Replace('/', '__')).bmp"
$overlayIdleDiffPath = Join-Path $capturesRootPath "overlayIdle\$($smokeSceneId.Replace('/', '__')).diff.png"
$testSuites = @(
    [pscustomobject]@{ Name = 'helengine.editor.tests'; ProjectPath = "$HelengineRoot\engine\helengine.editor.tests\helengine.editor.tests.csproj"; ExtraArguments = @() },
    [pscustomobject]@{ Name = 'helengine.render.validation.tests'; ProjectPath = "$HelengineRoot\engine\helengine.render.validation.tests\helengine.render.validation.tests.csproj"; ExtraArguments = @() },
    [pscustomobject]@{ Name = 'helengine.windows.builder.tests'; ProjectPath = "$RepoRoot\builder.tests\helengine.windows.builder.tests.csproj"; ExtraArguments = @("-p:HelEngineRoot=$HelengineRoot") }
)

if ($Record) {
    # 11R. Record into <WorkRoot>\record-staging; the committed goldens and baselines are only replaced at the end,
    #      when the whole record had no FAIL. Run every scene twice; a scene whose two captures are not stable is
    #      marked unstable and never becomes a golden. Each scene's per-window host fingerprints (run a) go into the
    #      manifest; every window of run a must be healthy and run b must log the same windows with matching fields.
    if (Test-Path -LiteralPath $recordStagingRootPath) {
        Remove-Item -LiteralPath $recordStagingRootPath -Recurse -Force
    }
    New-Item -ItemType Directory -Path $stagingGoldenRootPath -Force | Out-Null
    New-Item -ItemType Directory -Path $stagingBaselineRootPath -Force | Out-Null
    Write-Output ("RECORD_STAGING=" + $recordStagingRootPath)

    $manifestScenes = New-Object System.Collections.Generic.List[object]
    $unstableSceneIds = New-Object System.Collections.Generic.List[string]
    foreach ($sceneId in $sceneIds) {
        $sanitizedSceneId = $sceneId.Replace('/', '__')
        $captureAPath = Join-Path $capturesRootPath "a\$sanitizedSceneId.bmp"
        $captureBPath = Join-Path $capturesRootPath "b\$sanitizedSceneId.bmp"
        $runA = Invoke-PlayerScene -SceneId $sceneId -CapturePath $captureAPath
        $runB = Invoke-PlayerScene -SceneId $sceneId -CapturePath $captureBPath
        $runASucceeded = Test-PlayerRunSucceeded -PlayerRun $runA -Kind golden -SceneId $sceneId -CapturePath $captureAPath
        $runBSucceeded = Test-PlayerRunSucceeded -PlayerRun $runB -Kind golden -SceneId $sceneId -CapturePath $captureBPath
        if (-not ($runASucceeded -and $runBSucceeded)) {
            continue
        }

        Invoke-FingerprintHealthCheck -Name $sceneId -FingerprintsByWindow $runA.FingerprintsByWindow -PassDetail 'run a healthy'
        Compare-FingerprintsByWindow -Name $sceneId -RecordedFingerprintsByWindow $runA.FingerprintsByWindow -ActualFingerprintsByWindow $runB.FingerprintsByWindow -PassDetail 'run b matches run a'

        if ($sceneId -eq $smokeSceneId) {
            Test-SmokeCapture -SceneId $sceneId -CapturePath $captureAPath
        }

        $stableRun = Invoke-RegressionTool -ToolArguments @('stable', $captureAPath, $captureBPath)
        $stableDetail = $stableRun.Lines -join ' '
        $sceneStatus = $null
        if ($stableRun.ExitCode -eq 0) {
            $goldenPath = Join-Path $stagingGoldenRootPath "$sanitizedSceneId.png"
            $recordRun = Invoke-RegressionTool -ToolArguments @('record-golden', $captureAPath, $goldenPath)
            if ($recordRun.ExitCode -ne 0) {
                Add-CheckResult -Status FAIL -Kind golden -Name $sceneId -Detail ($recordRun.Lines -join ' ')
                continue
            }
            Add-CheckResult -Status PASS -Kind golden -Name $sceneId -Detail ("recorded " + $goldenPath)
            $sceneStatus = 'stable'
        }
        elseif ($stableRun.ExitCode -eq 1) {
            Add-CheckResult -Status SKIP -Kind golden -Name $sceneId -Detail $stableDetail
            $unstableSceneIds.Add($sceneId)
            $sceneStatus = 'unstable'
        }
        else {
            Add-CheckResult -Status FAIL -Kind golden -Name $sceneId -Detail $stableDetail
            continue
        }

        if ($sceneId -eq $smokeSceneId) {
            $manifestScenes.Add([ordered]@{ id = $sceneId; kind = 'smoke'; status = $sceneStatus })
        }
        $manifestScenes.Add([ordered]@{ id = $sceneId; kind = 'golden'; status = $sceneStatus; fingerprints = (ConvertTo-ManifestFingerprints -FingerprintsByWindow $runA.FingerprintsByWindow) })
    }

    # 11R-idle. Run the idle-throttle scenario once on the smoke scene and compare its capture with the smoke golden
    #           just staged. The manifest records the scenario and its minimums; its fingerprint is not recorded,
    #           because its idle and active frame counts and elapsed time are only checked against those minimums.
    $idleGoldenPath = Join-Path $stagingGoldenRootPath "$($smokeSceneId.Replace('/', '__')).png"
    if ($unstableSceneIds.Contains($smokeSceneId)) {
        $idleGoldenPath = ''
    }
    Invoke-IdleScenario -SceneId $smokeSceneId -CapturePath $idleCapturePath -DiffPath $idleDiffPath -GoldenPath $idleGoldenPath -MinimumIdleFrames $idleMinimumIdleFrames -MinimumElapsedMilliseconds $idleMinimumElapsedMilliseconds
    $manifestScenes.Add([ordered]@{ id = $smokeSceneId; kind = 'idle'; minIdleFrames = [int]$idleMinimumIdleFrames; minElapsedMs = [int]$idleMinimumElapsedMilliseconds })

    # 11R-overlay. Run the overlay scenario on the smoke scene: one record run records the alpha-preserving overlay
    #              golden into the staging folder (after its fingerprint and premultiplied checks), find-probes picks
    #              a fully transparent and a fully opaque probe pixel from that golden, and one run per probe must
    #              report clickThrough=on (WS_EX_TRANSPARENT set) and clickThrough=off (cleared), match the staged
    #              golden and have healthy overlay fingerprints. Because the probe's click-through toggle changes the
    #              window's exStyle, each probe run's fingerprints are recorded for that probe. The manifest's overlay
    #              entry stores both probes, both observed HIT_TEST exStyle values and both probes' fingerprints.
    $overlayRecordCapturePath = Join-Path $overlayCaptureRootPath "$($smokeSceneId.Replace('/', '__')).record.bmp"
    $overlayStagingGoldenPath = Join-Path $stagingGoldenRootPath $overlayGoldenFileName
    $overlayRecordRun = Invoke-PlayerScene -SceneId $smokeSceneId -CapturePath $overlayRecordCapturePath -ExtraArguments ($overlayPlayerArguments + @('--hit-test-probe', $overlayRecordProbe))
    if (Test-PlayerRunSucceeded -PlayerRun $overlayRecordRun -Kind overlay -SceneId $smokeSceneId -CapturePath $overlayRecordCapturePath) {
        Invoke-FingerprintHealthCheck -Name "$smokeSceneId.overlay" -FingerprintsByWindow $overlayRecordRun.FingerprintsByWindow -PassDetail 'overlay record run healthy'
        foreach ($overlayWindowName in $overlayRecordRun.FingerprintsByWindow.Keys) {
            Test-OverlayFingerprintShape -SceneId $smokeSceneId -FingerprintLine $overlayRecordRun.FingerprintsByWindow[$overlayWindowName] -Kind overlay
        }
        Test-OverlayPremultiplied -Kind overlay -SceneId $smokeSceneId -ProbeName record -CapturePath $overlayRecordCapturePath
        $overlayRecordGoldenRun = Invoke-RegressionTool -ToolArguments @('record-golden-rgba', $overlayRecordCapturePath, $overlayStagingGoldenPath)
        if ($overlayRecordGoldenRun.ExitCode -ne 0) {
            Add-CheckResult -Status FAIL -Kind overlay -Name $smokeSceneId -Detail ($overlayRecordGoldenRun.Lines -join ' ')
        }
        else {
            Add-CheckResult -Status PASS -Kind overlay -Name $smokeSceneId -Detail ("recorded " + $overlayStagingGoldenPath)
            $overlayTransparentProbe = $null
            $overlayOpaqueProbe = $null
            $findProbesRun = Invoke-RegressionTool -ToolArguments @('find-probes', $overlayStagingGoldenPath)
            foreach ($findProbesLine in $findProbesRun.Lines) {
                if ("$findProbesLine" -match '^TRANSPARENT (\d+,\d+)$') {
                    $overlayTransparentProbe = $Matches[1]
                }
                elseif ("$findProbesLine" -match '^OPAQUE (\d+,\d+)$') {
                    $overlayOpaqueProbe = $Matches[1]
                }
            }
            if ($findProbesRun.ExitCode -ne 0 -or $null -eq $overlayTransparentProbe -or $null -eq $overlayOpaqueProbe) {
                Add-CheckResult -Status FAIL -Kind overlay -Name $smokeSceneId -Detail ("find-probes: " + ($findProbesRun.Lines -join ' '))
            }
            else {
                Add-CheckResult -Status PASS -Kind overlay -Name $smokeSceneId -Detail "probes transparent=$overlayTransparentProbe opaque=$overlayOpaqueProbe"
                $overlayTransparentResult = Invoke-OverlayScenario -SceneId $smokeSceneId -ProbeName transparent -Probe $overlayTransparentProbe -ExpectedClickThrough on -ExpectedExStyle '' -RecordMode -CapturePath $overlayTransparentCapturePath -DiffPath (Join-Path $overlayCaptureRootPath "$($smokeSceneId.Replace('/', '__')).transparent.diff.png") -GoldenPath $overlayStagingGoldenPath
                $overlayOpaqueResult = Invoke-OverlayScenario -SceneId $smokeSceneId -ProbeName opaque -Probe $overlayOpaqueProbe -ExpectedClickThrough off -ExpectedExStyle '' -RecordMode -CapturePath $overlayOpaqueCapturePath -DiffPath (Join-Path $overlayCaptureRootPath "$($smokeSceneId.Replace('/', '__')).opaque.diff.png") -GoldenPath $overlayStagingGoldenPath
                if ($null -eq $overlayTransparentResult -or $null -eq $overlayOpaqueResult) {
                    Add-CheckResult -Status FAIL -Kind overlayIdle -Name $smokeSceneId -Detail 'not run: the overlay probe runs did not both pass, so no transparent exStyle was recorded'
                }
                else {
                    $overlayFingerprintsByProbe = [ordered]@{
                        transparent = (ConvertTo-ManifestFingerprints -FingerprintsByWindow $overlayTransparentResult.FingerprintsByWindow)
                        opaque = (ConvertTo-ManifestFingerprints -FingerprintsByWindow $overlayOpaqueResult.FingerprintsByWindow)
                    }
                    $manifestScenes.Add([ordered]@{ id = $smokeSceneId; kind = 'overlay'; transparentProbe = $overlayTransparentProbe; opaqueProbe = $overlayOpaqueProbe; transparentExStyle = $overlayTransparentResult.ExStyle; opaqueExStyle = $overlayOpaqueResult.ExStyle; fingerprintsByProbe = $overlayFingerprintsByProbe })

                    # 11R-overlayIdle. Run the overlay+idle scenario with the transparent probe just recorded; its
                    #                  HIT_TEST must report exactly the transparent probe's exStyle. Its fingerprints
                    #                  are stored for reference only; Verify checks them with check-idle.
                    $overlayIdleFingerprintsByWindow = Invoke-OverlayIdleScenario -SceneId $smokeSceneId -Probe $overlayTransparentProbe -ExpectedExStyle $overlayTransparentResult.ExStyle -CapturePath $overlayIdleCapturePath -DiffPath $overlayIdleDiffPath -GoldenPath $overlayStagingGoldenPath -MinimumIdleFrames $idleMinimumIdleFrames -MinimumElapsedMilliseconds $idleMinimumElapsedMilliseconds
                    if ($null -ne $overlayIdleFingerprintsByWindow) {
                        $manifestScenes.Add([ordered]@{ id = $smokeSceneId; kind = 'overlayIdle'; fingerprints = (ConvertTo-ManifestFingerprints -FingerprintsByWindow $overlayIdleFingerprintsByWindow) })
                    }
                }
            }
        }
    }

    # 12R. Write the staged manifest: run settings, each scene's check kind, status and host fingerprints, and the
    #      provenance of the record (commits and the hashes of every other input that changes the output).
    $manifestDocument = [ordered]@{
        frames = 30
        fixedDelta = 0.016666
        width = 640
        height = 360
        projectSource = $ProjectSource
        projectCommit = $builtProjectCommit
        buildConfigSourceHash = $buildConfigSourceHash
        generatedCodeHash = $generatedCodeHash
        helengineCommit = $helengineCommit
        helengineDirty = $helengineDirty
        helengineWorkingTreeHash = $helengineWorkingTreeHash
        scenes = $manifestScenes.ToArray()
    }
    [System.IO.File]::WriteAllText((Join-Path $stagingGoldenRootPath 'manifest.json'), ($manifestDocument | ConvertTo-Json -Depth 10), $utf8WithoutBom)
    if ($unstableSceneIds.Count -gt 0) {
        Write-Output ("UNSTABLE scenes (no golden recorded): " + ($unstableSceneIds -join ', '))
    }

    # 13R. Stage each test suite's failing-test set and executed-test count. A suite run that errored or aborted is a
    #      FAIL and is never staged as a baseline.
    foreach ($testSuite in $testSuites) {
        $suiteRun = Invoke-TestSuite -SuiteName $testSuite.Name -ProjectPath $testSuite.ProjectPath -ExtraArguments $testSuite.ExtraArguments
        $executedBaselinePath = Join-Path $stagingBaselineRootPath "$($testSuite.Name).executed.txt"
        $recordExecutedRun = Invoke-RegressionTool -ToolArguments @('record-executed', $suiteRun.TrxPath, $executedBaselinePath)
        if ($recordExecutedRun.ExitCode -ne 0) {
            Add-CheckResult -Status FAIL -Kind suite -Name $testSuite.Name -Detail ($recordExecutedRun.Lines -join ' ')
            continue
        }
        $baselinePath = Join-Path $stagingBaselineRootPath "$($testSuite.Name).failing.txt"
        Copy-Item -LiteralPath $suiteRun.FailingListPath -Destination $baselinePath -Force
        $failingCount = @(Get-Content -LiteralPath $baselinePath | Where-Object { $_.Length -gt 0 }).Count
        $executedCount = [System.IO.File]::ReadAllText($executedBaselinePath).Trim()
        Add-CheckResult -Status PASS -Kind suite -Name $testSuite.Name -Detail "baseline staged ($failingCount failing of $executedCount executed): $baselinePath"
    }
}
else {
    # 11V. Read the manifest, warn when the project or helengine has moved since the record, and check that it was
    #      recorded with the same run settings this script uses.
    $unstableSceneIds = New-Object System.Collections.Generic.List[string]
    $recordedGoldenSceneIds = New-Object System.Collections.Generic.List[string]
    $recordedGoldenScenesById = @{}
    $recordedIdleScene = $null
    $recordedOverlayScene = $null
    $recordedOverlayIdleScene = $null
    # Step 1 already stopped the run when the manifest is missing, because Verify builds the commit it pins.
    $manifest = [System.IO.File]::ReadAllText($manifestPath) | ConvertFrom-Json
    # The build above used the pinned (recorded) commit, so a moved HEAD only means the net is not testing the
    # project's latest state; move the pin deliberately with -Record (optionally -ProjectCommit <sha>).
    if ($projectHeadCommit -ne $builtProjectCommit) {
        Write-Output "WARN project changed since record: HEAD is $projectHeadCommit, but Verify built the pinned recorded commit $builtProjectCommit"
    }
    if ($manifest.helengineCommit -ne $helengineCommit -or [bool]$manifest.helengineDirty -ne $helengineDirty) {
        Write-Output "WARN helengine changed since record: $($manifest.helengineCommit) (dirty=$($manifest.helengineDirty)) -> $helengineCommit (dirty=$helengineDirty)"
    }
    if ($manifest.buildConfigSourceHash -ne $buildConfigSourceHash) {
        Write-Output "WARN buildConfigSourceHash changed since record (the project's user_settings\build_config.json): $($manifest.buildConfigSourceHash) -> $buildConfigSourceHash"
    }
    if ($manifest.generatedCodeHash -ne $generatedCodeHash) {
        Write-Output "WARN generatedCodeHash changed since record (the project's user_settings\generated_code): $($manifest.generatedCodeHash) -> $generatedCodeHash"
    }
    if ($manifest.helengineWorkingTreeHash -ne $helengineWorkingTreeHash) {
        Write-Output "WARN helengineWorkingTreeHash changed since record (helengine's uncommitted changes and untracked files): $($manifest.helengineWorkingTreeHash) -> $helengineWorkingTreeHash"
    }
    if ($manifest.frames -ne 30 -or [double]$manifest.fixedDelta -ne 0.016666 -or $manifest.width -ne 640 -or $manifest.height -ne 360) {
        Add-CheckResult -Status FAIL -Kind manifest -Name manifest.json -Detail "recorded with frames=$($manifest.frames) fixedDelta=$($manifest.fixedDelta) size=$($manifest.width)x$($manifest.height); expected frames=30 fixedDelta=0.016666 size=640x360"
    }
    foreach ($manifestScene in $manifest.scenes) {
        if ($manifestScene.kind -eq 'golden') {
            $recordedGoldenSceneIds.Add($manifestScene.id)
            $recordedGoldenScenesById[$manifestScene.id] = $manifestScene
            if ($manifestScene.status -eq 'unstable') {
                $unstableSceneIds.Add($manifestScene.id)
            }
        }
        elseif ($manifestScene.kind -eq 'idle') {
            $recordedIdleScene = $manifestScene
        }
        elseif ($manifestScene.kind -eq 'overlay') {
            $recordedOverlayScene = $manifestScene
        }
        elseif ($manifestScene.kind -eq 'overlayIdle') {
            $recordedOverlayIdleScene = $manifestScene
        }
    }
    foreach ($recordedSceneId in $recordedGoldenSceneIds) {
        if (-not $sceneIds.Contains($recordedSceneId)) {
            Add-CheckResult -Status FAIL -Kind golden -Name $recordedSceneId -Detail "recorded in the manifest but no longer built"
        }
    }

    # 12V. Run every scene once: host fingerprint comparison on every scene, smoke check on the first scene, golden
    #      comparison on every stable scene.
    $diffsRootPath = Join-Path $WorkRoot 'diffs'
    if (Test-Path -LiteralPath $diffsRootPath) {
        Remove-Item -LiteralPath $diffsRootPath -Recurse -Force
    }
    New-Item -ItemType Directory -Path $diffsRootPath -Force | Out-Null
    foreach ($sceneId in $sceneIds) {
        $sanitizedSceneId = $sceneId.Replace('/', '__')
        $capturePath = Join-Path $capturesRootPath "verify\$sanitizedSceneId.bmp"
        $checkKind = 'golden'
        if ($sceneId -eq $smokeSceneId) {
            $checkKind = 'smoke'
        }
        $verifyRun = Invoke-PlayerScene -SceneId $sceneId -CapturePath $capturePath
        if (-not (Test-PlayerRunSucceeded -PlayerRun $verifyRun -Kind $checkKind -SceneId $sceneId -CapturePath $capturePath)) {
            continue
        }

        # Each window's recorded fingerprint line is rebuilt from the manifest's fields, in their recorded order. An entry
        # from before per-window fingerprints (a single "fingerprint" object) can only be fixed by a re-record.
        $recordedScene = $recordedGoldenScenesById[$sceneId]
        if ($null -eq $recordedScene) {
            Add-CheckResult -Status FAIL -Kind fingerprint -Name $sceneId -Detail "not recorded in the manifest: $manifestPath"
        }
        elseif ($null -ne $recordedScene.PSObject.Properties['fingerprint'] -or $null -eq $recordedScene.fingerprints) {
            Add-CheckResult -Status FAIL -Kind fingerprint -Name $sceneId -Detail "manifest entry uses the old single 'fingerprint' shape or has no 'fingerprints' (re-record required): $manifestPath"
        }
        else {
            Compare-FingerprintsByWindow -Name $sceneId -RecordedFingerprintsByWindow (ConvertTo-RecordedFingerprintsByWindow -RecordedFingerprints $recordedScene.fingerprints) -ActualFingerprintsByWindow $verifyRun.FingerprintsByWindow -PassDetail 'matches record'
        }

        if ($sceneId -eq $smokeSceneId) {
            Test-SmokeCapture -SceneId $sceneId -CapturePath $capturePath
        }

        if ($unstableSceneIds.Contains($sceneId)) {
            Add-CheckResult -Status SKIP -Kind golden -Name $sceneId -Detail "unstable"
            continue
        }
        $goldenPath = Join-Path $goldenRootPath "$sanitizedSceneId.png"
        if (-not (Test-Path -LiteralPath $goldenPath -PathType Leaf)) {
            Add-CheckResult -Status FAIL -Kind golden -Name $sceneId -Detail "golden missing: $goldenPath"
            continue
        }
        $diffPath = Join-Path $diffsRootPath "$sanitizedSceneId.diff.png"
        $compareRun = Invoke-RegressionTool -ToolArguments @('compare', $capturePath, $goldenPath, $diffPath)
        $compareDetail = $compareRun.Lines -join ' '
        if ($compareRun.ExitCode -eq 0) {
            Add-CheckResult -Status PASS -Kind golden -Name $sceneId -Detail $compareDetail
        }
        else {
            Add-CheckResult -Status FAIL -Kind golden -Name $sceneId -Detail $compareDetail
        }
    }

    # 12V-idle. Run the idle-throttle scenario once on the smoke scene. A manifest without an idle entry predates the
    #          scenario (or was recorded for another smoke scene) and must be re-recorded, so that is a FAIL rather than a
    #          silent skip.
    $idleEntryComplete = $false
    if ($null -eq $recordedIdleScene -or $recordedIdleScene.id -ne $smokeSceneId) {
        Add-CheckResult -Status FAIL -Kind idle -Name $smokeSceneId -Detail "idle entry missing from manifest (re-record required): $manifestPath"
    }
    elseif ($null -eq $recordedIdleScene.minIdleFrames -or $null -eq $recordedIdleScene.minElapsedMs) {
        Add-CheckResult -Status FAIL -Kind idle -Name $smokeSceneId -Detail "idle entry lacks minIdleFrames or minElapsedMs (re-record required): $manifestPath"
    }
    else {
        $idleEntryComplete = $true
        # The idle diff goes into the diffs folder wiped above, beside the scene diffs.
        $idleDiffPath = Join-Path $diffsRootPath "$($smokeSceneId.Replace('/', '__')).idle.diff.png"
        $idleGoldenPath = Join-Path $goldenRootPath "$($smokeSceneId.Replace('/', '__')).png"
        if ($unstableSceneIds.Contains($smokeSceneId)) {
            $idleGoldenPath = ''
        }
        # The minimums come from the manifest's idle entry, so a verify checks what was recorded, not the script's
        # current constants (Record writes those constants into the entry).
        Invoke-IdleScenario -SceneId $smokeSceneId -CapturePath $idleCapturePath -DiffPath $idleDiffPath -GoldenPath $idleGoldenPath -MinimumIdleFrames "$($recordedIdleScene.minIdleFrames)" -MinimumElapsedMilliseconds "$($recordedIdleScene.minElapsedMs)"
    }

    # 12V-overlay. Run the overlay scenario on the smoke scene once per recorded probe: the transparent probe must
    #              report clickThrough=on and the opaque probe clickThrough=off, each with exactly its recorded exStyle,
    #              and both runs must match the committed overlay golden and their own probe's recorded fingerprints.
    #              A manifest without a complete overlay entry (or with the old single "fingerprint" shape) must be
    #              re-recorded, so that is a FAIL rather than a silent skip.
    $overlayEntryComplete = $false
    if ($null -eq $recordedOverlayScene -or $recordedOverlayScene.id -ne $smokeSceneId) {
        Add-CheckResult -Status FAIL -Kind overlay -Name $smokeSceneId -Detail "overlay entry missing from manifest (re-record required): $manifestPath"
    }
    elseif ($null -ne $recordedOverlayScene.PSObject.Properties['fingerprint']) {
        Add-CheckResult -Status FAIL -Kind overlay -Name $smokeSceneId -Detail "overlay entry uses the old single 'fingerprint' shape (re-record required): $manifestPath"
    }
    elseif ($null -eq $recordedOverlayScene.transparentProbe -or $null -eq $recordedOverlayScene.opaqueProbe -or $null -eq $recordedOverlayScene.transparentExStyle -or $null -eq $recordedOverlayScene.opaqueExStyle -or $null -eq $recordedOverlayScene.fingerprintsByProbe.transparent -or $null -eq $recordedOverlayScene.fingerprintsByProbe.opaque) {
        Add-CheckResult -Status FAIL -Kind overlay -Name $smokeSceneId -Detail "overlay entry lacks transparentProbe, opaqueProbe, transparentExStyle, opaqueExStyle or fingerprintsByProbe.transparent/opaque (re-record required): $manifestPath"
    }
    elseif ("$($recordedOverlayScene.transparentExStyle)" -cnotmatch '^0x[0-9A-F]{8}$' -or "$($recordedOverlayScene.opaqueExStyle)" -cnotmatch '^0x[0-9A-F]{8}$') {
        Add-CheckResult -Status FAIL -Kind overlay -Name $smokeSceneId -Detail "overlay entry's transparentExStyle '$($recordedOverlayScene.transparentExStyle)' or opaqueExStyle '$($recordedOverlayScene.opaqueExStyle)' is not 0x<8 upper-case hex digits> (re-record required): $manifestPath"
    }
    else {
        $overlayEntryComplete = $true
        # The overlay diffs go into the diffs folder wiped above, beside the scene diffs.
        $overlayTransparentDiffPath = Join-Path $diffsRootPath "$($smokeSceneId.Replace('/', '__')).overlay.transparent.diff.png"
        $overlayOpaqueDiffPath = Join-Path $diffsRootPath "$($smokeSceneId.Replace('/', '__')).overlay.opaque.diff.png"
        $null = Invoke-OverlayScenario -SceneId $smokeSceneId -ProbeName transparent -Probe "$($recordedOverlayScene.transparentProbe)" -ExpectedClickThrough on -ExpectedExStyle "$($recordedOverlayScene.transparentExStyle)" -CapturePath $overlayTransparentCapturePath -DiffPath $overlayTransparentDiffPath -GoldenPath (Join-Path $goldenRootPath $overlayGoldenFileName) -RecordedFingerprintsByWindow (ConvertTo-RecordedFingerprintsByWindow -RecordedFingerprints $recordedOverlayScene.fingerprintsByProbe.transparent)
        $null = Invoke-OverlayScenario -SceneId $smokeSceneId -ProbeName opaque -Probe "$($recordedOverlayScene.opaqueProbe)" -ExpectedClickThrough off -ExpectedExStyle "$($recordedOverlayScene.opaqueExStyle)" -CapturePath $overlayOpaqueCapturePath -DiffPath $overlayOpaqueDiffPath -GoldenPath (Join-Path $goldenRootPath $overlayGoldenFileName) -RecordedFingerprintsByWindow (ConvertTo-RecordedFingerprintsByWindow -RecordedFingerprints $recordedOverlayScene.fingerprintsByProbe.opaque)
    }

    # 12V-overlayIdle. Run the overlay+idle scenario once with the recorded transparent probe. It needs the overlay
    #                  entry (probe and transparent exStyle) and the idle entry (check-idle minimums); its own entry's
    #                  fingerprints are reference only and never compared. A missing entry is a FAIL.
    if ($null -eq $recordedOverlayIdleScene -or $recordedOverlayIdleScene.id -ne $smokeSceneId) {
        Add-CheckResult -Status FAIL -Kind overlayIdle -Name $smokeSceneId -Detail "overlayIdle entry missing from manifest (re-record required): $manifestPath"
    }
    elseif (-not $overlayEntryComplete) {
        Add-CheckResult -Status FAIL -Kind overlayIdle -Name $smokeSceneId -Detail 'not run: it needs a complete overlay entry (see the overlay check lines)'
    }
    elseif (-not $idleEntryComplete) {
        Add-CheckResult -Status FAIL -Kind overlayIdle -Name $smokeSceneId -Detail 'not run: it needs a complete idle entry for its check-idle minimums (see the idle check lines)'
    }
    else {
        # The overlayIdle diff goes into the diffs folder wiped above, beside the scene diffs.
        $overlayIdleDiffPath = Join-Path $diffsRootPath "$($smokeSceneId.Replace('/', '__')).overlayIdle.diff.png"
        $null = Invoke-OverlayIdleScenario -SceneId $smokeSceneId -Probe "$($recordedOverlayScene.transparentProbe)" -ExpectedExStyle "$($recordedOverlayScene.transparentExStyle)" -CapturePath $overlayIdleCapturePath -DiffPath $overlayIdleDiffPath -GoldenPath (Join-Path $goldenRootPath $overlayGoldenFileName) -MinimumIdleFrames "$($recordedIdleScene.minIdleFrames)" -MinimumElapsedMilliseconds "$($recordedIdleScene.minElapsedMs)"
    }

    # 13V. Check each suite run's outcome and executed-test count against the recorded count (an errored, aborted or
    #      truncated run fails), then compare its failing-test set with its recorded baseline. When the suite has a
    #      committed known-flaky list (regression\baselines\<suite>.flaky.txt), a new failure on that list is only a
    #      WARN.
    foreach ($testSuite in $testSuites) {
        $suiteRun = Invoke-TestSuite -SuiteName $testSuite.Name -ProjectPath $testSuite.ProjectPath -ExtraArguments $testSuite.ExtraArguments
        $failingListPath = $suiteRun.FailingListPath
        $executedBaselinePath = Join-Path $baselineRootPath "$($testSuite.Name).executed.txt"
        if (-not (Test-Path -LiteralPath $executedBaselinePath -PathType Leaf)) {
            Add-CheckResult -Status FAIL -Kind suite -Name $testSuite.Name -Detail "executed-count baseline missing: $executedBaselinePath"
        }
        else {
            $checkExecutedRun = Invoke-RegressionTool -ToolArguments @('check-executed', $suiteRun.TrxPath, $executedBaselinePath)
            $checkExecutedLine = $checkExecutedRun.Lines -join ' '
            if ($checkExecutedRun.ExitCode -le 1 -and $checkExecutedLine -match '^(PASS|WARN|FAIL) (.+)$') {
                Add-CheckResult -Status $Matches[1] -Kind suite -Name $testSuite.Name -Detail $Matches[2]
            }
            else {
                Add-CheckResult -Status FAIL -Kind suite -Name $testSuite.Name -Detail $checkExecutedLine
            }
        }
        $baselinePath = Join-Path $baselineRootPath "$($testSuite.Name).failing.txt"
        if (-not (Test-Path -LiteralPath $baselinePath -PathType Leaf)) {
            Add-CheckResult -Status FAIL -Kind suite -Name $testSuite.Name -Detail "baseline missing: $baselinePath"
            continue
        }
        $compareFailingArguments = @('compare-failing', $baselinePath, $failingListPath)
        $flakyListPath = Join-Path $baselineRootPath "$($testSuite.Name).flaky.txt"
        if (Test-Path -LiteralPath $flakyListPath -PathType Leaf) {
            $compareFailingArguments += $flakyListPath
        }
        $compareFailingRun = Invoke-RegressionTool -ToolArguments $compareFailingArguments
        foreach ($compareFailingLine in ($compareFailingRun.Lines | Select-Object -Skip 1)) {
            Write-Output "  $($testSuite.Name): $compareFailingLine"
            if ("$compareFailingLine" -match '^WARN flaky (.+)$') {
                Add-CheckResult -Status WARN -Kind suite -Name $testSuite.Name -Detail "known-flaky test failed: $($Matches[1])"
            }
        }
        $newFailureCount = @($compareFailingRun.Lines | Where-Object { "$_".StartsWith('NEW ') }).Count
        $fixedFailureCount = @($compareFailingRun.Lines | Where-Object { "$_".StartsWith('FIXED ') }).Count
        if ($compareFailingRun.ExitCode -eq 0) {
            Add-CheckResult -Status PASS -Kind suite -Name $testSuite.Name -Detail "no new failures ($fixedFailureCount fixed)"
        }
        elseif ($compareFailingRun.ExitCode -eq 1) {
            Add-CheckResult -Status FAIL -Kind suite -Name $testSuite.Name -Detail "$newFailureCount new failure(s), $fixedFailureCount fixed"
        }
        else {
            Add-CheckResult -Status FAIL -Kind suite -Name $testSuite.Name -Detail ($compareFailingRun.Lines -join ' ')
        }
    }
}

# 14. Record only: publish the staged record into regression\golden and regression\baselines when no check failed;
#     otherwise leave the committed files untouched and point at the staged output.
$failingCheckCount = @($CheckResults | Where-Object { $_.StartsWith('FAIL ') }).Count
if ($Record) {
    if ($failingCheckCount -eq 0) {
        Publish-RecordStaging -StagingGoldenRootPath $stagingGoldenRootPath -StagingBaselineRootPath $stagingBaselineRootPath -GoldenRootPath $goldenRootPath -BaselineRootPath $baselineRootPath
        Write-Output ("MANIFEST=" + $manifestPath)
        Write-Output "Record published into $goldenRootPath and $baselineRootPath."
    }
    else {
        Write-Output "Record had $failingCheckCount FAIL check(s): the committed goldens and baselines were left untouched. Staged output: $recordStagingRootPath"
    }
}

# 15. Summary: one line per check, then the overall result.
Write-Output ""
Write-Output "SUMMARY"
foreach ($checkLine in $CheckResults) {
    Write-Output $checkLine
}
if ($failingCheckCount -eq 0) {
    Write-Output "RESULT: PASS"
    exit 0
}
Write-Output "RESULT: FAIL ($failingCheckCount failing)"
exit 1
