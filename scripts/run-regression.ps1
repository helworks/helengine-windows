<#
.SYNOPSIS
Builds this checkout's Windows player against an isolated copy of the DemoDisc project for regression runs.

.DESCRIPTION
Modes (exactly one is required):
  -BuildOnly  Builds the player from THIS checkout (the folder above this script) into <WorkRoot>\player and prints
              PROJECT_COMMIT=<DemoDisc HEAD>, PLAYER_SOURCE_ROOT=<checkout> and PLAYER=<exe path>.
  -Record     Builds, runs every scene twice (runs a and b), records each stable scene as a golden, writes
              manifest.json (run settings, scene status, host fingerprints, the idle scenario and provenance hashes)
              and records each test suite's failing-test set and executed-test count. Everything is written to
              <WorkRoot>\record-staging first and copied into regression\golden and regression\baselines only when
              the whole record had no FAIL.
  -Verify     Builds, runs every scene once, checks that the smoke scene is not blank, compares every stable scene
              with its golden and every scene's host fingerprint with the recorded one, runs the idle-throttle
              scenario (see below), and compares each test suite's failing-test set and executed-test count with its
              baseline. Prints one line per check, then RESULT: PASS or RESULT: FAIL (<n> failing), and exits 0 only
              on PASS.

Record and Verify both run the opt-in idle-throttle scenario once on the smoke scene (30 frames with --idle-throttle on
--idle-after-ms 1 --idle-fps 10): the run must pass the regression tool's check-idle command (throttle on, no Present
failures, at least 25 idle frames, at least 2400 ms elapsed) and its capture must match the smoke scene's golden.

Every scene run is limited to 120 seconds; a hung player is killed and reported as FAIL scene <id> timeout.
The work root is marked with a .helengine-regression-workroot file; a non-empty folder without it is refused.

See regression\README.md for the thresholds and for when and how to re-record deliberately.

The script never modifies the helengine checkout or the project source. It extracts the project's committed HEAD
(git archive) into <WorkRoot>\project, copies the git-ignored user_settings folder beside it, writes its own engine
user settings to <WorkRoot>\engine-user-settings (pointing the windows platform at this checkout), and points
HELENGINE_ENGINE_USER_SETTINGS_ROOT there only for the duration of the canonical build-platform.ps1 call.
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
    [string]$WorkRoot = 'C:\dev\helworks\builds\helengine-windows\regression'
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
# $playerOutputPath, $playerExecutablePath, $regressionToolPath, $idlePlayerArguments, $idleMinimumIdleFrames and
# $idleMinimumElapsedMilliseconds. Progress lines use Write-Host so that they never become part of a function's
# return value.

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
the player's HOST_FINGERPRINT line from the startup log. FrameCount (default '30', the frame every golden is
captured at) and ExtraArguments (default none; the idle scenario passes the idle-throttle flags) extend the player's
fixed arguments.
.OUTPUTS
A [pscustomobject] with TimedOut (whether the launcher killed the run), ExitCode (the player's exit code; $null on a
timeout), CaptureExists (whether the capture file was written) and Fingerprint (the HOST_FINGERPRINT line without the
log prefix, or $null when the log has none).
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

    $fingerprintLine = $null
    if (Test-Path -LiteralPath $startupLogPath -PathType Leaf) {
        foreach ($startupLogLine in (Get-Content -LiteralPath $startupLogPath)) {
            if ("$startupLogLine" -match '(HOST_FINGERPRINT .+)$') {
                $fingerprintLine = $Matches[1]
            }
        }
    }

    return [pscustomobject]@{ TimedOut = $timedOut; ExitCode = $playerExitCode; CaptureExists = (Test-Path -LiteralPath $CapturePath -PathType Leaf); Fingerprint = $fingerprintLine }
}

<#
.SYNOPSIS
Records a FAIL for a scene run that timed out, did not exit cleanly, wrote no capture or logged no host fingerprint.
.OUTPUTS
$true when the run succeeded and its capture and fingerprint can be checked; otherwise $false, after recording a FAIL.
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
    if ($null -eq $PlayerRun.Fingerprint) {
        Add-CheckResult -Status FAIL -Kind fingerprint -Name $SceneId -Detail 'missing: the startup log has no HOST_FINGERPRINT line'
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
Runs the opt-in idle-throttle scenario once on a scene and records its checks as "idle" check lines.
.DESCRIPTION
Launches the player with the fixed 30-frame arguments plus the idle-throttle flags ($script:idlePlayerArguments), then
runs the regression tool's check-idle command on the run's HOST_FINGERPRINT line (idle throttle on, no Present
failures, at least $script:idleMinimumIdleFrames idle frames and $script:idleMinimumElapsedMilliseconds elapsed) and
compares the capture with the scene's golden, because throttling must never change what a frame renders. The
fingerprint is deliberately not compared with a recorded one: its idle and active frame counts and its elapsed time
differ from a normal run by design. When GoldenPath is empty the scene has no golden (it was recorded unstable), so the
capture comparison is reported as SKIP.
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
        [string]$GoldenPath
    )

    # A diff image left by an earlier run must never be mistaken for this run's, so it is deleted before the run.
    if (Test-Path -LiteralPath $DiffPath -PathType Leaf) {
        Remove-Item -LiteralPath $DiffPath -Force
    }
    $idleRun = Invoke-PlayerScene -SceneId $SceneId -CapturePath $CapturePath -ExtraArguments $script:idlePlayerArguments
    if (-not (Test-PlayerRunSucceeded -PlayerRun $idleRun -Kind idle -SceneId $SceneId -CapturePath $CapturePath)) {
        return
    }

    $checkIdleRun = Invoke-RegressionTool -ToolArguments @('check-idle', $idleRun.Fingerprint, $script:idleMinimumIdleFrames, $script:idleMinimumElapsedMilliseconds)
    if ($checkIdleRun.ExitCode -eq 0 -and "$($checkIdleRun.Lines -join ' ')" -match '^PASS (.+)$') {
        Add-CheckResult -Status PASS -Kind idle -Name $SceneId -Detail $Matches[1]
    }
    elseif ($checkIdleRun.ExitCode -eq 1) {
        foreach ($checkIdleLine in $checkIdleRun.Lines) {
            if ("$checkIdleLine" -notmatch '^FAIL (.+)$') {
                throw "The regression tool printed an unexpected check-idle line for scene '$SceneId': $checkIdleLine"
            }
            Add-CheckResult -Status FAIL -Kind idle -Name $SceneId -Detail $Matches[1]
        }
    }
    else {
        Add-CheckResult -Status FAIL -Kind idle -Name $SceneId -Detail ($checkIdleRun.Lines -join ' ')
    }

    if ($GoldenPath.Length -eq 0) {
        Add-CheckResult -Status SKIP -Kind idle -Name $SceneId -Detail 'capture not compared: the scene has no golden (unstable)'
        return
    }
    if (-not (Test-Path -LiteralPath $GoldenPath -PathType Leaf)) {
        Add-CheckResult -Status FAIL -Kind idle -Name $SceneId -Detail "golden missing: $GoldenPath"
        return
    }
    $compareRun = Invoke-RegressionTool -ToolArguments @('compare', $CapturePath, $GoldenPath, $DiffPath)
    $compareDetail = $compareRun.Lines -join ' '
    if ($compareRun.ExitCode -eq 0) {
        Add-CheckResult -Status PASS -Kind idle -Name $SceneId -Detail "capture matches golden: $compareDetail"
    }
    else {
        Add-CheckResult -Status FAIL -Kind idle -Name $SceneId -Detail "capture differs from golden: $compareDetail"
    }
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

$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path
$HelengineRoot = [System.IO.Path]::GetFullPath($HelengineRoot)
$ProjectSource = [System.IO.Path]::GetFullPath($ProjectSource)
$WorkRoot = [System.IO.Path]::GetFullPath($WorkRoot)
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

# 1. Extract the project's committed HEAD into the work root, then add its git-ignored user_settings folder.
$projectCommit = (& git -C $ProjectSource rev-parse HEAD)
if ($LASTEXITCODE -ne 0) {
    throw "Reading the project source's HEAD commit failed with exit code $LASTEXITCODE."
}
$projectCommit = $projectCommit.Trim()

New-Item -ItemType Directory -Path $WorkRoot -Force | Out-Null
$projectRootPath = Join-Path $WorkRoot 'project'
if (Test-Path -LiteralPath $projectRootPath) {
    Remove-Item -LiteralPath $projectRootPath -Recurse -Force
}
New-Item -ItemType Directory -Path $projectRootPath -Force | Out-Null

$projectArchivePath = Join-Path $WorkRoot 'project-source.tar'
& git -C $ProjectSource archive --format=tar -o $projectArchivePath $projectCommit
if ($LASTEXITCODE -ne 0) {
    throw "Archiving the project source's HEAD failed with exit code $LASTEXITCODE."
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

# 2. Write isolated engine user settings whose windows entry points at this checkout.
$sourceUserSettingsRootPath = Join-Path $HelengineRoot 'user_settings'
$platformsSource = [System.IO.File]::ReadAllText((Join-Path $sourceUserSettingsRootPath 'platforms.json'))
$platformsDocument = $platformsSource | ConvertFrom-Json
$windowsPlatform = $null
foreach ($platform in $platformsDocument.platforms) {
    if ($platform.platformId -eq 'windows') {
        $windowsPlatform = $platform
    }
}
if ($null -eq $windowsPlatform) {
    throw "No 'windows' platform entry was found in $sourceUserSettingsRootPath\platforms.json."
}

$pathPropertyNames = @('builderAssemblyPath', 'playerSourceRootPath', 'generatedCoreCppRootPath', 'codegenToolPath', 'pluginManifestPath')
foreach ($platform in $platformsDocument.platforms) {
    foreach ($pathPropertyName in $pathPropertyNames) {
        $pathProperty = $platform.PSObject.Properties[$pathPropertyName]
        if ($null -ne $pathProperty -and -not [string]::IsNullOrWhiteSpace($pathProperty.Value) -and -not [System.IO.Path]::IsPathRooted($pathProperty.Value)) {
            $pathProperty.Value = [System.IO.Path]::GetFullPath((Join-Path $sourceUserSettingsRootPath $pathProperty.Value))
        }
    }
}
$windowsPlatform.builderAssemblyPath = Join-Path $RepoRoot 'builder\bin\Debug\net9.0\helengine.windows.builder.dll'
$windowsPlatform.playerSourceRootPath = $RepoRoot

$engineUserSettingsRootPath = Join-Path $WorkRoot 'engine-user-settings'
New-Item -ItemType Directory -Path $engineUserSettingsRootPath -Force | Out-Null
[System.IO.File]::WriteAllText((Join-Path $engineUserSettingsRootPath 'platforms.json'), ($platformsDocument | ConvertTo-Json -Depth 20), $utf8WithoutBom)

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

Write-Output ("PROJECT_COMMIT=" + $projectCommit)
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

$goldenRootPath = Join-Path $RepoRoot 'regression\golden'
$baselineRootPath = Join-Path $RepoRoot 'regression\baselines'
$manifestPath = Join-Path $goldenRootPath 'manifest.json'
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
$testSuites = @(
    [pscustomobject]@{ Name = 'helengine.editor.tests'; ProjectPath = "$HelengineRoot\engine\helengine.editor.tests\helengine.editor.tests.csproj"; ExtraArguments = @() },
    [pscustomobject]@{ Name = 'helengine.render.validation.tests'; ProjectPath = "$HelengineRoot\engine\helengine.render.validation.tests\helengine.render.validation.tests.csproj"; ExtraArguments = @() },
    [pscustomobject]@{ Name = 'helengine.windows.builder.tests'; ProjectPath = "$RepoRoot\builder.tests\helengine.windows.builder.tests.csproj"; ExtraArguments = @("-p:HelEngineRoot=$HelengineRoot") }
)

if ($Record) {
    # 11R. Record into <WorkRoot>\record-staging; the committed goldens and baselines are only replaced at the end,
    #      when the whole record had no FAIL. Run every scene twice; a scene whose two captures are not stable is
    #      marked unstable and never becomes a golden. Each scene's host fingerprint (run a) goes into the manifest;
    #      run a must be healthy and run b must match it.
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

        Invoke-FingerprintCheck -SceneId $sceneId -ToolArguments @('check-fingerprint', $sceneId, $runA.Fingerprint) -PassDetail 'run a healthy'
        Invoke-FingerprintCheck -SceneId $sceneId -ToolArguments @('compare-fingerprint', $sceneId, $runA.Fingerprint, $runB.Fingerprint) -PassDetail 'run b matches run a'

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

        $fingerprintFields = ConvertTo-FingerprintFields -FingerprintLine $runA.Fingerprint
        $sceneElapsedMilliseconds = [long]$fingerprintFields['elapsedMs']
        $fingerprintFields.Remove('elapsedMs')
        if ($sceneId -eq $smokeSceneId) {
            $manifestScenes.Add([ordered]@{ id = $sceneId; kind = 'smoke'; status = $sceneStatus })
        }
        $manifestScenes.Add([ordered]@{ id = $sceneId; kind = 'golden'; status = $sceneStatus; fingerprint = $fingerprintFields; elapsedMs = $sceneElapsedMilliseconds })
    }

    # 11R-idle. Run the idle-throttle scenario once on the smoke scene and compare its capture with the smoke golden
    #           just staged. The manifest records the scenario and its minimums; its fingerprint is not recorded,
    #           because its idle and active frame counts and elapsed time are only checked against those minimums.
    $idleGoldenPath = Join-Path $stagingGoldenRootPath "$($smokeSceneId.Replace('/', '__')).png"
    if ($unstableSceneIds.Contains($smokeSceneId)) {
        $idleGoldenPath = ''
    }
    Invoke-IdleScenario -SceneId $smokeSceneId -CapturePath $idleCapturePath -DiffPath $idleDiffPath -GoldenPath $idleGoldenPath
    $manifestScenes.Add([ordered]@{ id = $smokeSceneId; kind = 'idle'; minIdleFrames = [int]$idleMinimumIdleFrames; minElapsedMs = [int]$idleMinimumElapsedMilliseconds })

    # 12R. Write the staged manifest: run settings, each scene's check kind, status and host fingerprint, and the
    #      provenance of the record (commits and the hashes of every other input that changes the output).
    $manifestDocument = [ordered]@{
        frames = 30
        fixedDelta = 0.016666
        width = 640
        height = 360
        projectSource = $ProjectSource
        projectCommit = $projectCommit
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
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        Add-CheckResult -Status FAIL -Kind manifest -Name manifest.json -Detail "golden missing: $manifestPath"
    }
    else {
        $manifest = [System.IO.File]::ReadAllText($manifestPath) | ConvertFrom-Json
        if ($manifest.projectCommit -ne $projectCommit) {
            Write-Output "WARN project changed since record: $($manifest.projectCommit) -> $projectCommit"
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
        }
        foreach ($recordedSceneId in $recordedGoldenSceneIds) {
            if (-not $sceneIds.Contains($recordedSceneId)) {
                Add-CheckResult -Status FAIL -Kind golden -Name $recordedSceneId -Detail "recorded in the manifest but no longer built"
            }
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

        # The recorded fingerprint line is rebuilt from the manifest's fields (in their recorded order) plus elapsedMs.
        $recordedScene = $recordedGoldenScenesById[$sceneId]
        if ($null -eq $recordedScene -or $null -eq $recordedScene.fingerprint -or $null -eq $recordedScene.elapsedMs) {
            Add-CheckResult -Status FAIL -Kind fingerprint -Name $sceneId -Detail "not recorded in the manifest: $manifestPath"
        }
        else {
            $recordedFingerprintTokens = New-Object System.Collections.Generic.List[string]
            foreach ($recordedFingerprintField in $recordedScene.fingerprint.PSObject.Properties) {
                $recordedFingerprintTokens.Add("$($recordedFingerprintField.Name)=$($recordedFingerprintField.Value)")
            }
            $recordedFingerprintTokens.Add("elapsedMs=$($recordedScene.elapsedMs)")
            Invoke-FingerprintCheck -SceneId $sceneId -ToolArguments @('compare-fingerprint', $sceneId, ($recordedFingerprintTokens -join ' '), $verifyRun.Fingerprint) -PassDetail 'matches record'
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
    if ($null -eq $recordedIdleScene -or $recordedIdleScene.id -ne $smokeSceneId) {
        Add-CheckResult -Status FAIL -Kind idle -Name $smokeSceneId -Detail "idle entry missing from manifest (re-record required): $manifestPath"
    }
    else {
        # The idle diff goes into the diffs folder wiped above, beside the scene diffs.
        $idleDiffPath = Join-Path $diffsRootPath "$($smokeSceneId.Replace('/', '__')).idle.diff.png"
        $idleGoldenPath = Join-Path $goldenRootPath "$($smokeSceneId.Replace('/', '__')).png"
        if ($unstableSceneIds.Contains($smokeSceneId)) {
            $idleGoldenPath = ''
        }
        Invoke-IdleScenario -SceneId $smokeSceneId -CapturePath $idleCapturePath -DiffPath $idleDiffPath -GoldenPath $idleGoldenPath
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
