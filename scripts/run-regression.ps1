<#
.SYNOPSIS
Builds this checkout's Windows player against an isolated copy of the DemoDisc project for regression runs.

.DESCRIPTION
Modes (exactly one is required):
  -BuildOnly  Builds the player from THIS checkout (the folder above this script) into <WorkRoot>\player and prints
              PROJECT_COMMIT=<DemoDisc HEAD>, PLAYER_SOURCE_ROOT=<checkout> and PLAYER=<exe path>.
  -Record     Reserved for the verify stage (not implemented yet).
  -Verify     Reserved for the verify stage (not implemented yet).

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

if ($Record -or $Verify) {
    throw "Not implemented until the verify stage is added."
}

$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path
$HelengineRoot = [System.IO.Path]::GetFullPath($HelengineRoot)
$ProjectSource = [System.IO.Path]::GetFullPath($ProjectSource)
$WorkRoot = [System.IO.Path]::GetFullPath($WorkRoot)
$utf8WithoutBom = New-Object System.Text.UTF8Encoding($false)
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
