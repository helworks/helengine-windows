<##
.SYNOPSIS
Captures a bounded Tracy profile from an isolated copy of a Windows Profiler package.

.DESCRIPTION
Copies the supplied Profiler package into a new capture session directory, starts Tracy Capture,
launches the copied player and then runs an explicit repeatable workload. The source package and
its manifests are read only. The session directory receives the Tracy capture, manifest snapshots,
and a JSON report containing machine, build, workload, and artifact-hash metadata.
##>
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$ProfilerPackagePath,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$WindowsBuildManifestPath,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$TracyCapturePath,

    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$WorkloadExecutablePath,

    [string]$WorkloadProjectPath,

    [string[]]$WorkloadArguments = @(),

    [ValidateRange(1, 3600)]
    [int]$CaptureSeconds = 30,

    [ValidateRange(0, 60)]
    [int]$PlayerStartupSeconds = 2,

    [ValidateRange(1, 120)]
    [int]$CaptureShutdownSeconds = 15,

    [string]$OutputDirectory = (Join-Path $PWD 'windows-profiler-captures')
)

$ErrorActionPreference = 'Stop'

$resolvedProfilerPackagePath = [System.IO.Path]::GetFullPath($ProfilerPackagePath)
$resolvedWindowsBuildManifestPath = [System.IO.Path]::GetFullPath($WindowsBuildManifestPath)
$resolvedTracyCapturePath = [System.IO.Path]::GetFullPath($TracyCapturePath)
$resolvedWorkloadExecutablePath = [System.IO.Path]::GetFullPath($WorkloadExecutablePath)
$resolvedOutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)

if (-not (Test-Path -LiteralPath $resolvedProfilerPackagePath -PathType Container)) {
    throw "Profiler package directory was not found: $resolvedProfilerPackagePath"
} elseif (-not (Test-Path -LiteralPath $resolvedWindowsBuildManifestPath -PathType Leaf)) {
    throw "Windows build manifest was not found: $resolvedWindowsBuildManifestPath. Pass the windows-build-manifest.json produced beside the builder workspace."
} elseif (-not (Test-Path -LiteralPath $resolvedTracyCapturePath -PathType Leaf)) {
    throw "Tracy Capture executable was not found: $resolvedTracyCapturePath. Build Tracy's capture tool or pass its explicit path with -TracyCapturePath."
} elseif (-not (Test-Path -LiteralPath $resolvedWorkloadExecutablePath -PathType Leaf)) {
    throw "Workload executable was not found: $resolvedWorkloadExecutablePath"
}

$sourcePlayerExecutablePath = Join-Path $resolvedProfilerPackagePath 'helengine_windows.exe'
$sourceProfilerManifestPath = Join-Path $resolvedProfilerPackagePath 'runtime\generated_profiler_manifest.json'
$sourcePlayerPdbPath = Join-Path $resolvedProfilerPackagePath 'helengine_windows.pdb'
if (-not (Test-Path -LiteralPath $sourcePlayerExecutablePath -PathType Leaf)) {
    throw "Profiler package does not contain helengine_windows.exe: $sourcePlayerExecutablePath"
} elseif (-not (Test-Path -LiteralPath $sourceProfilerManifestPath -PathType Leaf)) {
    throw "Profiler package does not contain runtime\\generated_profiler_manifest.json: $sourceProfilerManifestPath"
} elseif (-not (Test-Path -LiteralPath $sourcePlayerPdbPath -PathType Leaf)) {
    throw "Profiler package does not contain helengine_windows.pdb: $sourcePlayerPdbPath"
}

$windowsBuildManifest = Get-Content -LiteralPath $resolvedWindowsBuildManifestPath -Raw | ConvertFrom-Json
$profilerArtifactIds = @($windowsBuildManifest.ProfilerArtifacts | ForEach-Object { $_.ItemId })
if ($profilerArtifactIds -notcontains 'generated-profiler-manifest' -or $profilerArtifactIds -notcontains 'native-pdb') {
    throw "Windows build manifest does not describe both profiler artifacts. Rebuild with the Windows 'profiler' profile before capturing."
}

if (-not [string]::IsNullOrWhiteSpace($WorkloadProjectPath)) {
    $resolvedWorkloadProjectPath = [System.IO.Path]::GetFullPath($WorkloadProjectPath)
    if (-not (Test-Path -LiteralPath $resolvedWorkloadProjectPath -PathType Leaf)) {
        throw "Workload project was not found: $resolvedWorkloadProjectPath"
    }
} else {
    $resolvedWorkloadProjectPath = $null
}

$sessionName = 'capture-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + [Guid]::NewGuid().ToString('N')
$sessionDirectoryPath = Join-Path $resolvedOutputDirectory $sessionName
$sessionPackagePath = Join-Path $sessionDirectoryPath 'package'
$sessionManifestDirectoryPath = Join-Path $sessionDirectoryPath 'manifests'
$capturePath = Join-Path $sessionDirectoryPath 'capture.tracy'
$reportPath = Join-Path $sessionDirectoryPath 'capture-report.json'

New-Item -ItemType Directory -Path $sessionPackagePath -Force | Out-Null
New-Item -ItemType Directory -Path $sessionManifestDirectoryPath -Force | Out-Null
Get-ChildItem -LiteralPath $resolvedProfilerPackagePath -Force | Copy-Item -Destination $sessionPackagePath -Recurse
Copy-Item -LiteralPath $resolvedWindowsBuildManifestPath -Destination (Join-Path $sessionManifestDirectoryPath 'windows-build-manifest.json')
Copy-Item -LiteralPath $sourceProfilerManifestPath -Destination (Join-Path $sessionManifestDirectoryPath 'generated_profiler_manifest.json')

$sessionPlayerExecutablePath = Join-Path $sessionPackagePath 'helengine_windows.exe'
$captureProcess = $null
$playerProcess = $null
$workloadProcess = $null
$captureStartedAtUtc = [DateTime]::UtcNow
$captureDeadlineUtc = $captureStartedAtUtc.AddSeconds($CaptureSeconds + $CaptureShutdownSeconds + $PlayerStartupSeconds)

try {
    $captureProcess = Start-Process -FilePath $resolvedTracyCapturePath -ArgumentList @('-o', $capturePath, '-a', '127.0.0.1', '-p', '8086', '-s', $CaptureSeconds.ToString()) -WorkingDirectory $sessionDirectoryPath -PassThru
    $playerProcess = Start-Process -FilePath $sessionPlayerExecutablePath -WorkingDirectory $sessionPackagePath -PassThru

    if ($PlayerStartupSeconds -gt 0) {
        Start-Sleep -Seconds $PlayerStartupSeconds
    }

    $workloadProcess = Start-Process -FilePath $resolvedWorkloadExecutablePath -ArgumentList $WorkloadArguments -WorkingDirectory (Split-Path -Path $resolvedWorkloadExecutablePath -Parent) -PassThru
    while (-not $workloadProcess.HasExited -and [DateTime]::UtcNow -lt $captureDeadlineUtc) {
        Start-Sleep -Milliseconds 250
        $workloadProcess.Refresh()
    }

    if (-not $workloadProcess.HasExited) {
        Stop-Process -Id $workloadProcess.Id -ErrorAction SilentlyContinue
        throw "Workload exceeded the bounded capture session of $($CaptureSeconds + $CaptureShutdownSeconds + $PlayerStartupSeconds) seconds."
    }
} finally {
    if ($playerProcess -ne $null -and -not $playerProcess.HasExited) {
        Stop-Process -Id $playerProcess.Id -ErrorAction SilentlyContinue
    }

    if ($captureProcess -ne $null) {
        while (-not $captureProcess.HasExited -and [DateTime]::UtcNow -lt $captureDeadlineUtc) {
            Start-Sleep -Milliseconds 250
            $captureProcess.Refresh()
        }

        if (-not $captureProcess.HasExited) {
            Stop-Process -Id $captureProcess.Id -ErrorAction SilentlyContinue
        }
    }
}

if (-not (Test-Path -LiteralPath $capturePath -PathType Leaf)) {
    throw "Tracy Capture exited without writing '$capturePath'. Confirm the Profiler player can connect to Tracy at 127.0.0.1:8086 and that the workload exercises it."
}

$machineMetadata = [ordered]@{
    ComputerName = $env:COMPUTERNAME
    OperatingSystem = (Get-CimInstance -ClassName Win32_OperatingSystem | Select-Object -First 1 -Property Caption, Version, BuildNumber)
    Processor = (Get-CimInstance -ClassName Win32_Processor | Select-Object -First 1 -Property Name, NumberOfCores, NumberOfLogicalProcessors)
    MemoryBytes = (Get-CimInstance -ClassName Win32_ComputerSystem | Select-Object -First 1 -ExpandProperty TotalPhysicalMemory)
    PowerShellVersion = $PSVersionTable.PSVersion.ToString()
}

$report = [ordered]@{
    CaptureStartedUtc = $captureStartedAtUtc.ToString('O')
    CaptureCompletedUtc = [DateTime]::UtcNow.ToString('O')
    CaptureSeconds = $CaptureSeconds
    PlayerStartupSeconds = $PlayerStartupSeconds
    ProfilerPackageSourcePath = $resolvedProfilerPackagePath
    WorkloadExecutablePath = $resolvedWorkloadExecutablePath
    WorkloadProjectPath = $resolvedWorkloadProjectPath
    WorkloadArguments = @($WorkloadArguments)
    TracyCapturePath = $resolvedTracyCapturePath
    CapturePath = 'capture.tracy'
    Machine = $machineMetadata
    Build = [ordered]@{
        WindowsBuildManifestSourcePath = $resolvedWindowsBuildManifestPath
        WindowsBuildManifestSnapshotPath = 'manifests/windows-build-manifest.json'
        WindowsBuildManifestSha256 = (Get-FileHash -LiteralPath $resolvedWindowsBuildManifestPath -Algorithm SHA256).Hash
        GeneratedProfilerManifestSourcePath = $sourceProfilerManifestPath
        GeneratedProfilerManifestSnapshotPath = 'manifests/generated_profiler_manifest.json'
        GeneratedProfilerManifestSha256 = (Get-FileHash -LiteralPath $sourceProfilerManifestPath -Algorithm SHA256).Hash
        PlayerExecutableSha256 = (Get-FileHash -LiteralPath $sourcePlayerExecutablePath -Algorithm SHA256).Hash
        PlayerPdbSha256 = (Get-FileHash -LiteralPath $sourcePlayerPdbPath -Algorithm SHA256).Hash
        ProjectId = $windowsBuildManifest.ProjectId
        ProjectVersion = $windowsBuildManifest.ProjectVersion
        RequiredEngineVersion = $windowsBuildManifest.RequiredEngineVersion
        StartupSceneId = $windowsBuildManifest.StartupSceneId
    }
}

$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding utf8
Write-Output "CAPTURE=$capturePath"
Write-Output "REPORT=$reportPath"
