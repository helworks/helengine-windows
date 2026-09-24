<#
.SYNOPSIS
Builds this checkout's Windows player against an isolated copy of the DemoDisc project for regression runs.

.DESCRIPTION
Modes (exactly one is required):
  -BuildOnly  Builds the player from THIS checkout (the folder above this script) into <WorkRoot>\player and prints
              PROJECT_COMMIT=<DemoDisc HEAD>, PLAYER_SOURCE_ROOT=<checkout> and PLAYER=<exe path>.
  -Record     Builds, runs every scene twice (runs a and b) and records each stable scene as a golden under
              regression\golden, writes regression\golden\manifest.json (run settings, scene status and provenance)
              and records each test suite's failing-test set under regression\baselines.
  -Verify     Builds, runs every scene once, checks that the smoke scene is not blank, compares every stable scene
              with its golden and compares each test suite's failing-test set with its baseline. Prints one line per
              check, then RESULT: PASS or RESULT: FAIL (<n> failing), and exits 0 only on PASS.

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
# $playerOutputPath, $playerExecutablePath and $regressionToolPath. Progress lines use Write-Host so that they never
# become part of a function's return value.

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
Records one check's outcome ("<PASS|FAIL|SKIP> <kind> <name> <detail>") for the final summary and echoes it.
#>
function Add-CheckResult {
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet('PASS', 'FAIL', 'SKIP')]
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
window), creates the capture folder (the player's BMP writer does not create folders) and deletes any capture left by
an earlier run so that a stale file is never checked. On a non-zero exit code it prints the tail of the startup log.
.OUTPUTS
A [pscustomobject] with ExitCode (the player's exit code) and CaptureExists (whether the capture file was written).
#>
function Invoke-PlayerScene {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SceneId,

        [Parameter(Mandatory = $true)]
        [string]$CapturePath
    )

    [System.IO.File]::WriteAllText((Join-Path $script:playerOutputPath 'profile.json'), '{"resolutionWidth":640,"resolutionHeight":360}', $script:utf8WithoutBom)
    New-Item -ItemType Directory -Path (Split-Path -Path $CapturePath -Parent) -Force | Out-Null
    if (Test-Path -LiteralPath $CapturePath) {
        Remove-Item -LiteralPath $CapturePath -Force
    }

    Write-Host "RUN $SceneId -> $CapturePath"
    $playerArguments = @('--scene', $SceneId, '--frames', '30', '--fixed-delta', '0.016666', '--capture', $CapturePath)
    $launchLines = @(& "$script:RepoRoot\scripts\launch_in_emulator.ps1" -ArtifactPath $script:playerExecutablePath -ArgumentList $playerArguments -Wait)
    $playerExitCode = $null
    foreach ($launchLine in $launchLines) {
        if ("$launchLine" -match '^EXIT_CODE=(-?\d+)$') {
            $playerExitCode = [int]$Matches[1]
        }
    }
    if ($null -eq $playerExitCode) {
        throw "The launcher did not report EXIT_CODE= for scene '$SceneId'."
    }

    if ($playerExitCode -ne 0) {
        $startupLogPath = Join-Path $script:playerOutputPath 'helengine_windows.startup.log'
        Write-Host "Player exited with code $playerExitCode on scene '$SceneId'. Startup log tail ($startupLogPath):"
        if (Test-Path -LiteralPath $startupLogPath -PathType Leaf) {
            Get-Content -LiteralPath $startupLogPath -Tail 20 | ForEach-Object { Write-Host "  $_" }
        }
        else {
            Write-Host "  (no startup log was written)"
        }
    }

    return [pscustomobject]@{ ExitCode = $playerExitCode; CaptureExists = (Test-Path -LiteralPath $CapturePath -PathType Leaf) }
}

<#
.SYNOPSIS
Records a FAIL for a scene run that did not exit cleanly or wrote no capture.
.OUTPUTS
$true when the run succeeded and its capture can be checked; otherwise $false, after recording a FAIL.
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

    if ($PlayerRun.ExitCode -ne 0) {
        Add-CheckResult -Status FAIL -Kind $Kind -Name $SceneId -Detail "player exit code $($PlayerRun.ExitCode)"
        return $false
    }
    if (-not $PlayerRun.CaptureExists) {
        Add-CheckResult -Status FAIL -Kind $Kind -Name $SceneId -Detail "capture missing: $CapturePath"
        return $false
    }
    return $true
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
The path of the written failing-test list.
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
    return $failingListPath
}

$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path
$HelengineRoot = [System.IO.Path]::GetFullPath($HelengineRoot)
$ProjectSource = [System.IO.Path]::GetFullPath($ProjectSource)
$WorkRoot = [System.IO.Path]::GetFullPath($WorkRoot)
Assert-WorkRootIsolated -WorkRootPath $WorkRoot -ProtectedRootPaths @($ProjectSource, $HelengineRoot, $RepoRoot)
$utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)
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
Write-Output ("HELENGINE_COMMIT=" + $helengineCommit)
Write-Output ("HELENGINE_DIRTY=" + $helengineDirty)

$goldenRootPath = Join-Path $RepoRoot 'regression\golden'
$baselineRootPath = Join-Path $RepoRoot 'regression\baselines'
$manifestPath = Join-Path $goldenRootPath 'manifest.json'
$capturesRootPath = Join-Path $WorkRoot 'captures'
$testSuites = @(
    [pscustomobject]@{ Name = 'helengine.editor.tests'; ProjectPath = "$HelengineRoot\engine\helengine.editor.tests\helengine.editor.tests.csproj"; ExtraArguments = @() },
    [pscustomobject]@{ Name = 'helengine.render.validation.tests'; ProjectPath = "$HelengineRoot\engine\helengine.render.validation.tests\helengine.render.validation.tests.csproj"; ExtraArguments = @() },
    [pscustomobject]@{ Name = 'helengine.windows.builder.tests'; ProjectPath = "$RepoRoot\builder.tests\helengine.windows.builder.tests.csproj"; ExtraArguments = @("-p:HelEngineRoot=$HelengineRoot") }
)

if ($Record) {
    # 11R. Run every scene twice; a scene whose two captures are not stable is marked unstable and never becomes a
    #      golden. Old goldens are removed first so that a scene that turned unstable leaves no stale golden behind.
    New-Item -ItemType Directory -Path $goldenRootPath -Force | Out-Null
    New-Item -ItemType Directory -Path $baselineRootPath -Force | Out-Null
    Get-ChildItem -LiteralPath $goldenRootPath -Filter '*.png' | Remove-Item -Force

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

        if ($sceneId -eq $smokeSceneId) {
            Test-SmokeCapture -SceneId $sceneId -CapturePath $captureAPath
        }

        $stableRun = Invoke-RegressionTool -ToolArguments @('stable', $captureAPath, $captureBPath)
        $stableDetail = $stableRun.Lines -join ' '
        $sceneStatus = $null
        if ($stableRun.ExitCode -eq 0) {
            $goldenPath = Join-Path $goldenRootPath "$sanitizedSceneId.png"
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
        $manifestScenes.Add([ordered]@{ id = $sceneId; kind = 'golden'; status = $sceneStatus })
    }

    # 12R. Write the manifest right after the goldens and before any test suite runs, so that the goldens and the
    #      manifest always describe the same record even when a suite step throws: run settings, each scene's check
    #      kind and status, and the provenance of the record.
    $manifestDocument = [ordered]@{
        frames = 30
        fixedDelta = 0.016666
        width = 640
        height = 360
        projectSource = $ProjectSource
        projectCommit = $projectCommit
        helengineCommit = $helengineCommit
        helengineDirty = $helengineDirty
        scenes = $manifestScenes.ToArray()
    }
    [System.IO.File]::WriteAllText($manifestPath, ($manifestDocument | ConvertTo-Json -Depth 10), $utf8WithoutBom)
    Write-Output ("MANIFEST=" + $manifestPath)
    if ($unstableSceneIds.Count -gt 0) {
        Write-Output ("UNSTABLE scenes (no golden recorded): " + ($unstableSceneIds -join ', '))
    }

    # 13R. Record each test suite's failing-test set as its baseline, each one written right after its suite runs.
    foreach ($testSuite in $testSuites) {
        $failingListPath = Invoke-TestSuite -SuiteName $testSuite.Name -ProjectPath $testSuite.ProjectPath -ExtraArguments $testSuite.ExtraArguments
        $baselinePath = Join-Path $baselineRootPath "$($testSuite.Name).failing.txt"
        Copy-Item -LiteralPath $failingListPath -Destination $baselinePath -Force
        $failingCount = @(Get-Content -LiteralPath $baselinePath | Where-Object { $_.Length -gt 0 }).Count
        Add-CheckResult -Status PASS -Kind suite -Name $testSuite.Name -Detail "baseline recorded ($failingCount failing): $baselinePath"
    }
}
else {
    # 11V. Read the manifest, warn when the project or helengine has moved since the record, and check that it was
    #      recorded with the same run settings this script uses.
    $unstableSceneIds = New-Object System.Collections.Generic.List[string]
    $recordedGoldenSceneIds = New-Object System.Collections.Generic.List[string]
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
        if ($manifest.frames -ne 30 -or [double]$manifest.fixedDelta -ne 0.016666 -or $manifest.width -ne 640 -or $manifest.height -ne 360) {
            Add-CheckResult -Status FAIL -Kind manifest -Name manifest.json -Detail "recorded with frames=$($manifest.frames) fixedDelta=$($manifest.fixedDelta) size=$($manifest.width)x$($manifest.height); expected frames=30 fixedDelta=0.016666 size=640x360"
        }
        foreach ($manifestScene in $manifest.scenes) {
            if ($manifestScene.kind -eq 'golden') {
                $recordedGoldenSceneIds.Add($manifestScene.id)
                if ($manifestScene.status -eq 'unstable') {
                    $unstableSceneIds.Add($manifestScene.id)
                }
            }
        }
        foreach ($recordedSceneId in $recordedGoldenSceneIds) {
            if (-not $sceneIds.Contains($recordedSceneId)) {
                Add-CheckResult -Status FAIL -Kind golden -Name $recordedSceneId -Detail "recorded in the manifest but no longer built"
            }
        }
    }

    # 12V. Run every scene once: smoke check on the first scene, golden comparison on every stable scene.
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

    # 13V. Compare each test suite's failing-test set with its recorded baseline.
    foreach ($testSuite in $testSuites) {
        $failingListPath = Invoke-TestSuite -SuiteName $testSuite.Name -ProjectPath $testSuite.ProjectPath -ExtraArguments $testSuite.ExtraArguments
        $baselinePath = Join-Path $baselineRootPath "$($testSuite.Name).failing.txt"
        if (-not (Test-Path -LiteralPath $baselinePath -PathType Leaf)) {
            Add-CheckResult -Status FAIL -Kind suite -Name $testSuite.Name -Detail "baseline missing: $baselinePath"
            continue
        }
        $compareFailingRun = Invoke-RegressionTool -ToolArguments @('compare-failing', $baselinePath, $failingListPath)
        foreach ($compareFailingLine in ($compareFailingRun.Lines | Select-Object -Skip 1)) {
            Write-Output "  $($testSuite.Name): $compareFailingLine"
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

# 14. Summary: one line per check, then the overall result.
Write-Output ""
Write-Output "SUMMARY"
foreach ($checkLine in $CheckResults) {
    Write-Output $checkLine
}
$failingCheckCount = @($CheckResults | Where-Object { $_.StartsWith('FAIL ') }).Count
if ($failingCheckCount -eq 0) {
    Write-Output "RESULT: PASS"
    exit 0
}
Write-Output "RESULT: FAIL ($failingCheckCount failing)"
exit 1
