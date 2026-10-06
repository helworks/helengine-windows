<#
.SYNOPSIS
Checks real multi-window rendering and lifecycle against the existing smoke-scene goldens.
.DESCRIPTION
Uses only the canonical launcher and test-owned windows. Captures are deterministic renderer back-buffer exports;
desktop pixels, cursor position and display scaling are never read or changed.
#>
param(
    [Parameter(Mandatory = $true)][string]$ArtifactPath,
    [Parameter(Mandatory = $true)][string]$RegressionToolPath,
    [Parameter(Mandatory = $true)][string]$WorkRoot
)
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$resolvedWorkRoot = [IO.Path]::GetFullPath($WorkRoot)
New-Item -ItemType Directory -Path $resolvedWorkRoot -Force | Out-Null
$launcher = Join-Path $PSScriptRoot 'launch_in_emulator.ps1'
$playerRoot = Split-Path -Parent ([IO.Path]::GetFullPath($ArtifactPath))
$startupLog = Join-Path $playerRoot 'helengine_windows.startup.log'
$normalGolden = Join-Path $repositoryRoot 'regression/golden/axis_test.png'
$overlayGolden = Join-Path $repositoryRoot 'regression/golden/axis_test.overlay.png'

function Invoke-Comparison {
    param([string[]]$Arguments)
    & dotnet $RegressionToolPath @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Regression comparison failed: $Arguments" }
}

function Invoke-Scenario {
    param([string]$Name, [string[]]$WindowArguments, [string[]]$Tags)
    $capture = Join-Path $resolvedWorkRoot "$Name.bmp"
    $arguments = @('--scene', 'axis_test', '--frames', '30', '--fixed-delta', '0.016666', '--capture', $capture) + $WindowArguments
    & $launcher -ArtifactPath $ArtifactPath -ArgumentList $arguments -Wait -TimeoutSeconds 45
    if ($LASTEXITCODE -ne 0) { throw "Multi-window scenario failed: $Name" }
    $log = Get-Content -LiteralPath $startupLog -Raw
    foreach ($tag in (@('main') + $Tags)) {
        if ($log -notmatch "(?m)^(?:\[Host\] )?HOST_FINGERPRINT .*presentCount=30 .*presentFailures=0 frames=30 .*window=$tag(?: |$)") {
            throw "Scenario $Name lacks 30 successful presentations for $tag."
        }
    }
    Invoke-Comparison -Arguments @('compare', $capture, $normalGolden, (Join-Path $resolvedWorkRoot "$Name.diff.png"))
    Write-Output "PASS multi-window $Name"
}

$profilePath = Join-Path $playerRoot 'profile.json'
$profileExisted = Test-Path -LiteralPath $profilePath
$savedProfile = if ($profileExisted) { [IO.File]::ReadAllBytes($profilePath) } else { $null }
[IO.File]::WriteAllText($profilePath, '{"resolutionWidth":640,"resolutionHeight":360}', [Text.Encoding]::ASCII)
try {
Invoke-Scenario 'two-normal' @('--window', 'preview,normal,700,40,640,360') @('preview')
Invoke-Comparison -Arguments @('compare', (Join-Path $resolvedWorkRoot 'two-normal.preview.bmp'), $normalGolden, (Join-Path $resolvedWorkRoot 'two-normal.preview.diff.png'))
Invoke-Scenario 'mixed' @('--window', 'preview,normal,700,40,640,360', '--window', 'glass,overlay,0,0,640,360') @('preview', 'glass')
Invoke-Comparison -Arguments @('compare', (Join-Path $resolvedWorkRoot 'mixed.preview.bmp'), $normalGolden, (Join-Path $resolvedWorkRoot 'mixed.preview.diff.png'))
Invoke-Comparison -Arguments @('check-premultiplied', (Join-Path $resolvedWorkRoot 'mixed.glass.bmp'), '0.01', '0.01')
Invoke-Comparison -Arguments @('compare-rgba', (Join-Path $resolvedWorkRoot 'mixed.glass.bmp'), $overlayGolden, (Join-Path $resolvedWorkRoot 'mixed.glass.diff.png'))

# The low idle rate leaves a deterministic interval in which test-owned windows can be minimized and resized.
Add-Type -Path (Join-Path $repositoryRoot 'tests/WindowLifecycleProbe.cs')
$capture = Join-Path $resolvedWorkRoot 'lifecycle.bmp'
$arguments = @('--scene', 'axis_test', '--frames', '30', '--fixed-delta', '0.016666', '--capture', $capture,
    '--idle-throttle', 'on', '--idle-after-ms', '1', '--idle-fps', '5', '--dpi-awareness', 'permonitorv2',
    '--window', 'preview,normal,700,40,640,360', '--window', 'glass,overlay,0,0,640,360')
$launchOutput = @(& $launcher -ArtifactPath $ArtifactPath -ArgumentList $arguments)
$processLine = $launchOutput | Where-Object { $_ -like 'PROCESS_ID=*' } | Select-Object -First 1
if (-not $processLine) { throw 'The launcher did not return a player process ID.' }
$playerProcess = Get-Process -Id ([int]$processLine.Substring('PROCESS_ID='.Length))
$null = $playerProcess.Handle
try {
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    do {
        if ($playerProcess.HasExited) { throw 'The lifecycle player exited before initialization.' }
        $windows = [HelEngine.Windows.Tests.WindowLifecycleProbe]::FindWindows($playerProcess.Id)
        $ready = (Test-Path -LiteralPath $startupLog) -and ((Get-Content -LiteralPath $startupLog -Raw) -match 'Entering render loop')
        if ($windows.ContainsKey('HelEngine Windows Host') -and $windows.ContainsKey('preview') -and $windows.ContainsKey('glass') -and $ready) { break }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    if (-not $windows.ContainsKey('HelEngine Windows Host') -or -not $windows.ContainsKey('preview') -or -not $windows.ContainsKey('glass') -or -not $ready) { throw 'Three live windows were not initialized in one process.' }
    [HelEngine.Windows.Tests.WindowLifecycleProbe]::Minimize($windows['HelEngine Windows Host'])
    Start-Sleep -Milliseconds 450
    [HelEngine.Windows.Tests.WindowLifecycleProbe]::Restore($windows['HelEngine Windows Host'])
    [HelEngine.Windows.Tests.WindowLifecycleProbe]::Minimize($windows['preview'])
    Start-Sleep -Milliseconds 450
    [HelEngine.Windows.Tests.WindowLifecycleProbe]::Restore($windows['preview'])
    [HelEngine.Windows.Tests.WindowLifecycleProbe]::Resize($windows['preview'], 800, 600)
    if (-not $playerProcess.WaitForExit(45000)) { throw 'The lifecycle player timed out.' }
    if ($playerProcess.ExitCode -ne 0) { throw "Lifecycle player failed: $($playerProcess.ExitCode)" }
    $log = Get-Content -LiteralPath $startupLog -Raw
    if ($log -notmatch '(?m)^(?:\[Host\] )?HOST_FINGERPRINT .*client=800x600 .*window=preview ') { throw 'Secondary resize was not applied.' }
    $main = [regex]::Match($log, '(?m)^(?:\[Host\] )?HOST_FINGERPRINT .*frames=(\d+) .*window=main ')
    $glass = [regex]::Match($log, '(?m)^(?:\[Host\] )?HOST_FINGERPRINT .*frames=(\d+) .*window=glass ')
    if (-not $main.Success -or -not $glass.Success -or [int]$main.Groups[1].Value -ge [int]$glass.Groups[1].Value) {
        throw 'Minimizing the primary did not leave the secondary rendering independently.'
    }
    Invoke-Comparison -Arguments @('compare', $capture, $normalGolden, (Join-Path $resolvedWorkRoot 'lifecycle.diff.png'))
    Write-Output 'PASS multi-window resize and minimized-window independence'
} finally {
    if (-not $playerProcess.HasExited) { $playerProcess.Kill(); $playerProcess.WaitForExit() }
}

# A second process checks closing a secondary and then ending the remaining views through the primary.
$launchOutput = @(& $launcher -ArtifactPath $ArtifactPath -ArgumentList @('--scene', 'axis_test', '--window', 'preview,normal,700,40,640,360'))
$processLine = $launchOutput | Where-Object { $_ -like 'PROCESS_ID=*' } | Select-Object -First 1
$playerProcess = Get-Process -Id ([int]$processLine.Substring('PROCESS_ID='.Length))
$null = $playerProcess.Handle
try {
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    do {
        $windows = [HelEngine.Windows.Tests.WindowLifecycleProbe]::FindWindows($playerProcess.Id)
        if ($windows.ContainsKey('HelEngine Windows Host') -and $windows.ContainsKey('preview')) { break }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline -and -not $playerProcess.HasExited)
    if (-not $windows.ContainsKey('HelEngine Windows Host') -or -not $windows.ContainsKey('preview')) { throw 'The close test did not initialize two windows.' }
    [HelEngine.Windows.Tests.WindowLifecycleProbe]::Close($windows['preview'])
    Start-Sleep -Milliseconds 300
    $windows = [HelEngine.Windows.Tests.WindowLifecycleProbe]::FindWindows($playerProcess.Id)
    if ($playerProcess.HasExited -or -not $windows.ContainsKey('HelEngine Windows Host') -or $windows.ContainsKey('preview')) { throw 'Closing a secondary stopped the process or left a stale window.' }
    [HelEngine.Windows.Tests.WindowLifecycleProbe]::Close($windows['HelEngine Windows Host'])
    if (-not $playerProcess.WaitForExit(10000) -or $playerProcess.ExitCode -ne 0) { throw 'Closing the primary did not end the process cleanly.' }
    Write-Output 'PASS multi-window secondary close and primary shutdown'
} finally {
    if (-not $playerProcess.HasExited) { $playerProcess.Kill(); $playerProcess.WaitForExit() }
}
# Finite diagnostics retain their multi-window frame policy after the final secondary closes.
$launchOutput = @(& $launcher -ArtifactPath $ArtifactPath -ArgumentList @('--scene', 'axis_test', '--frames', '30',
    '--fixed-delta', '0.016666', '--idle-throttle', 'on', '--idle-after-ms', '1', '--idle-fps', '5',
    '--window', 'preview,normal,700,40,640,360'))
$processLine = $launchOutput | Where-Object { $_ -like 'PROCESS_ID=*' } | Select-Object -First 1
if (-not $processLine) { throw 'The finite close launcher did not return a process ID.' }
$playerProcess = Get-Process -Id ([int]$processLine.Substring('PROCESS_ID='.Length))
$null = $playerProcess.Handle
try {
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    do {
        if ($playerProcess.HasExited) { throw 'The finite close player ended before initialization.' }
        $windows = [HelEngine.Windows.Tests.WindowLifecycleProbe]::FindWindows($playerProcess.Id)
        $ready = (Test-Path -LiteralPath $startupLog) -and ((Get-Content -LiteralPath $startupLog -Raw) -match 'Entering render loop')
        if ($windows.ContainsKey('preview') -and $ready) { break }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    if (-not $windows.ContainsKey('preview') -or -not $ready) { throw 'The finite close view was not initialized.' }
    [HelEngine.Windows.Tests.WindowLifecycleProbe]::Close($windows['preview'])
    if (-not $playerProcess.WaitForExit(45000) -or $playerProcess.ExitCode -ne 0) { throw 'The finite run failed after its final secondary closed.' }
    $log = Get-Content -LiteralPath $startupLog -Raw
    if ($log -notmatch '(?m)^(?:\[Host\] )?HOST_FINGERPRINT .*presentCount=30 .*frames=30 .*window=main ') {
        throw 'The remaining primary did not retain its finite frame accounting.'
    }
    if ($log -match '(?m)^(?:\[Host\] )?HOST_FINGERPRINT .*window=preview ') { throw 'A closed secondary emitted a stale final fingerprint.' }
    Write-Output 'PASS multi-window finite run after final secondary close'
} finally {
    if (-not $playerProcess.HasExited) { $playerProcess.Kill(); $playerProcess.WaitForExit() }
}
Write-Output 'RESULT: PASS multi-window acceptance'

} finally {
    if ($profileExisted) { [IO.File]::WriteAllBytes($profilePath, $savedProfile) }
    else { Remove-Item -LiteralPath $profilePath }
}
