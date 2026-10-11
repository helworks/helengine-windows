<#
.SYNOPSIS
Checks shared-scene desktop rendering, finite presentation, profile selection, resize, and window lifetime.
.DESCRIPTION
Uses the canonical launcher and player-owned windows. Captures export only renderer back buffers;
the script never captures the screen, moves the cursor, or changes display or Explorer settings.
An optional GoldenRootPath selects independently validated normal and overlay reference images.
#>
param(
    [Parameter(Mandatory = $true)][string]$ArtifactPath,
    [Parameter(Mandatory = $true)][string]$RegressionToolPath,
    [Parameter(Mandatory = $true)][string]$WorkRoot,
    [Parameter()][string]$GoldenRootPath = ''
)
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$resolvedWorkRoot = [IO.Path]::GetFullPath($WorkRoot)
New-Item -ItemType Directory -Path $resolvedWorkRoot -Force | Out-Null
$launcher = Join-Path $PSScriptRoot 'launch_in_emulator.ps1'
$playerRoot = Split-Path -Parent ([IO.Path]::GetFullPath($ArtifactPath))
$startupLog = Join-Path $playerRoot 'helengine_windows.startup.log'
$profilePath = Join-Path $playerRoot 'profile.json'
$profileExisted = Test-Path -LiteralPath $profilePath
$savedProfile = if ($profileExisted) { [IO.File]::ReadAllBytes($profilePath) } else { $null }
$results = [Collections.Generic.List[string]]::new()
Add-Type -Path (Join-Path $repositoryRoot 'tests/WindowLifecycleProbe.cs')

function Invoke-Comparison {
    param([string[]]$ComparisonArguments)
    & dotnet $RegressionToolPath @ComparisonArguments
    if ($LASTEXITCODE -ne 0) { throw "Desktop render comparison failed: $ComparisonArguments" }
}

function Assert-Fingerprint {
    param([string]$Log, [string]$Tag, [string]$Mode, [int]$Frames = 30)
    $escapedTag = [regex]::Escape($Tag)
    $match = [regex]::Match($Log, "(?m)^(?:\[Host\] )?HOST_FINGERPRINT .*presentCount=$Frames .*presentFailures=0 frames=$Frames .*windowMode=$Mode window=$escapedTag(?: |$).*")
    if (-not $match.Success) { throw "Missing $Frames successful presentations for $Tag in mode $Mode." }
    if ($Mode -eq 'desktop' -and $match.Value -notmatch 'alpha=3 swapEffect=0 buffers=1 .*desktopParent=(Progman|WorkerW) desktopRect=') {
        throw 'Desktop presentation did not use opaque blt output inside the Explorer wallpaper layer.'
    }
    Invoke-Comparison @('check-fingerprint', 'axis_test', ($match.Value.Trim() -replace '^\[Host\] ', ''))
}

function Invoke-Scenario {
    param([string]$Name, [string[]]$PlayerArguments, [bool]$Capture = $true)
    $capturePath = Join-Path $resolvedWorkRoot "$Name.bmp"
    $launchArguments = @('--scene', 'axis_test', '--frames', '30', '--fixed-delta', '0.016666') + $PlayerArguments
    if ($Capture) { $launchArguments += @('--capture', $capturePath) }
    & $launcher -ArtifactPath $ArtifactPath -ArgumentList $launchArguments -Wait -TimeoutSeconds 45 | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Desktop player scenario failed: $Name" }
    $log = [IO.File]::ReadAllText($startupLog)
    [IO.File]::WriteAllText((Join-Path $resolvedWorkRoot "$Name.startup.log"), $log)
    return $log
}

function Start-LifecyclePlayer {
    param([string[]]$PlayerArguments)
    $launchOutput = @(& $launcher -ArtifactPath $ArtifactPath -ArgumentList $PlayerArguments)
    $processLine = $launchOutput | Where-Object { $_ -like 'PROCESS_ID=*' } | Select-Object -First 1
    if (-not $processLine) { throw 'Desktop launcher did not return a player process ID.' }
    $process = Get-Process -Id ([int]$processLine.Substring('PROCESS_ID='.Length))
    $null = $process.Handle
    return $process
}

function Wait-PlayerWindows {
    param([Diagnostics.Process]$PlayerProcess, [string[]]$Titles)
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    do {
        if ($PlayerProcess.HasExited) { throw 'Desktop lifecycle player ended before initialization.' }
        $windows = [HelEngine.Windows.Tests.WindowLifecycleProbe]::FindWindows($PlayerProcess.Id)
        $ready = (Test-Path -LiteralPath $startupLog) -and ((Get-Content -LiteralPath $startupLog -Raw) -match 'Entering render loop')
        foreach ($title in $Titles) { if (-not $windows.ContainsKey($title)) { $ready = $false } }
        if ($ready) { return $windows }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'The expected player-owned desktop windows were not initialized.'
}

function Add-Result {
    param([string]$Message)
    $results.Add($Message)
    Write-Output "PASS desktop $Message"
}

try {
    [IO.File]::WriteAllText($profilePath, '{"resolutionWidth":640,"resolutionHeight":360}', [Text.Encoding]::ASCII)
    $log = Invoke-Scenario 'normal-reference' @()
    Assert-Fingerprint $log 'main' 'normal'
    $log = Invoke-Scenario 'overlay-reference' @('--window-mode', 'overlay', '--overlay-bounds', 'profile', '--overlay-background', 'transparent')
    Assert-Fingerprint $log 'main' 'overlay'
    Invoke-Comparison @('check-premultiplied', (Join-Path $resolvedWorkRoot 'overlay-reference.bmp'), '0.01', '0.01')
    if (-not [string]::IsNullOrWhiteSpace($GoldenRootPath)) {
        Invoke-Comparison @('compare', (Join-Path $resolvedWorkRoot 'normal-reference.bmp'), (Join-Path $GoldenRootPath 'axis_test.png'), (Join-Path $resolvedWorkRoot 'normal-reference.diff.png'))
        Invoke-Comparison @('compare-rgba', (Join-Path $resolvedWorkRoot 'overlay-reference.bmp'), (Join-Path $GoldenRootPath 'axis_test.overlay.png'), (Join-Path $resolvedWorkRoot 'overlay-reference.diff.png'))
    }
    Invoke-Comparison @('record-golden', (Join-Path $resolvedWorkRoot 'normal-reference.bmp'), (Join-Path $resolvedWorkRoot 'normal-reference.png'))
    Invoke-Comparison @('record-golden-rgba', (Join-Path $resolvedWorkRoot 'overlay-reference.bmp'), (Join-Path $resolvedWorkRoot 'overlay-reference.png'))
    Add-Result 'normal and overlay references'

    $log = Invoke-Scenario 'mixed' @('--window', 'wallpaper,desktop,0,0,640,360', '--window', 'preview,normal,700,40,640,360', '--window', 'glass,overlay,0,0,640,360')
    Assert-Fingerprint $log 'main' 'normal'
    Assert-Fingerprint $log 'wallpaper' 'desktop'
    Assert-Fingerprint $log 'preview' 'normal'
    Assert-Fingerprint $log 'glass' 'overlay'
    foreach ($captureName in @('mixed.bmp', 'mixed.wallpaper.bmp', 'mixed.preview.bmp')) {
        Invoke-Comparison @('compare', (Join-Path $resolvedWorkRoot $captureName), (Join-Path $resolvedWorkRoot 'normal-reference.png'), (Join-Path $resolvedWorkRoot "$captureName.diff.png"))
    }
    Invoke-Comparison @('compare-rgba', (Join-Path $resolvedWorkRoot 'mixed.glass.bmp'), (Join-Path $resolvedWorkRoot 'overlay-reference.png'), (Join-Path $resolvedWorkRoot 'mixed.glass.diff.png'))
    Add-Result 'normal, desktop and overlay share one scene without changing its output'

    $log = Invoke-Scenario 'primary-desktop' @('--window-mode', 'desktop', '--window', 'preview,normal,700,40,640,360', '--window', 'glass,overlay,0,0,640,360') $false
    Assert-Fingerprint $log 'main' 'desktop'
    Assert-Fingerprint $log 'preview' 'normal'
    Assert-Fingerprint $log 'glass' 'overlay'
    if ($log -notmatch 'Desktop client size: \d+x\d+\.') { throw 'Primary desktop monitor coverage was not reported.' }
    if ($log -notmatch 'HOST_FINGERPRINT .*window=preview .*dpiAwareness=unaware ' -or $log -notmatch 'HOST_FINGERPRINT .*window=glass .*dpiAwareness=unaware ') {
        throw 'Desktop primary changed the ordinary views default DPI policy.'
    }
    Add-Result 'primary desktop covers the monitor and coexists with ordinary views'

    $log = Invoke-Scenario 'primary-desktop-aware' @('--window-mode', 'desktop', '--dpi-awareness', 'permonitorv2', '--window', 'preview,normal,700,40,640,360') $false
    Assert-Fingerprint $log 'main' 'desktop'
    Assert-Fingerprint $log 'preview' 'normal'
    if ($log -notmatch 'HOST_FINGERPRINT .*window=preview .*dpiAwareness=permonitorv2 ') { throw 'Desktop primary lost the explicitly selected per-monitor DPI policy.' }
    Add-Result 'desktop primary preserves explicitly selected per-monitor DPI policy'

    [IO.File]::WriteAllText($profilePath, '{"resolutionWidth":640,"resolutionHeight":360,"windowMode":"desktop"}', [Text.Encoding]::ASCII)
    $log = Invoke-Scenario 'profile-desktop' @() $false
    Assert-Fingerprint $log 'main' 'desktop'
    Add-Result 'profile selects desktop mode'
    [IO.File]::WriteAllText($profilePath, '{"resolutionWidth":640,"resolutionHeight":360}', [Text.Encoding]::ASCII)

    $capture = Join-Path $resolvedWorkRoot 'lifecycle.bmp'
    $playerProcess = Start-LifecyclePlayer @('--scene', 'axis_test', '--frames', '30', '--fixed-delta', '0.016666', '--capture', $capture,
        '--idle-throttle', 'on', '--idle-after-ms', '1', '--idle-fps', '5', '--dpi-awareness', 'permonitorv2', '--window', 'wallpaper,desktop,0,0,640,360')
    try {
        $windows = Wait-PlayerWindows $playerProcess @('HelEngine Windows Host', 'wallpaper')
        [HelEngine.Windows.Tests.WindowLifecycleProbe]::Minimize($windows['HelEngine Windows Host'])
        Start-Sleep -Milliseconds 450
        [HelEngine.Windows.Tests.WindowLifecycleProbe]::Restore($windows['HelEngine Windows Host'])
        [HelEngine.Windows.Tests.WindowLifecycleProbe]::Resize($windows['wallpaper'], 800, 600)
        if (-not $playerProcess.WaitForExit(45000) -or $playerProcess.ExitCode -ne 0) { throw 'Desktop resize/minimize lifecycle failed.' }
        $log = [IO.File]::ReadAllText($startupLog)
        [IO.File]::WriteAllText((Join-Path $resolvedWorkRoot 'lifecycle.startup.log'), $log)
        Assert-Fingerprint $log 'wallpaper' 'desktop'
        if ($log -notmatch 'HOST_FINGERPRINT .*client=800x600 .*window=wallpaper ') { throw 'Desktop view resize did not reach its renderer surface.' }
        $main = [regex]::Match($log, 'HOST_FINGERPRINT .*frames=(\d+) .*window=main ')
        if (-not $main.Success -or [int]$main.Groups[1].Value -ge 30) { throw 'Desktop did not keep rendering while the normal primary was minimized.' }
        Invoke-Comparison @('compare', $capture, (Join-Path $resolvedWorkRoot 'normal-reference.png'), (Join-Path $resolvedWorkRoot 'lifecycle.diff.png'))
        Add-Result 'desktop resize and continued rendering while primary is minimized'
    } finally {
        if (-not $playerProcess.HasExited) { $playerProcess.Kill(); $playerProcess.WaitForExit() }
    }

    $playerProcess = Start-LifecyclePlayer @('--scene', 'axis_test', '--window', 'wallpaper,desktop,0,0,640,360')
    try {
        $windows = Wait-PlayerWindows $playerProcess @('HelEngine Windows Host', 'wallpaper')
        [HelEngine.Windows.Tests.WindowLifecycleProbe]::Close($windows['wallpaper'])
        Start-Sleep -Milliseconds 300
        $windows = [HelEngine.Windows.Tests.WindowLifecycleProbe]::FindWindows($playerProcess.Id)
        if ($playerProcess.HasExited -or $windows.ContainsKey('wallpaper') -or -not $windows.ContainsKey('HelEngine Windows Host')) {
            throw 'Closing the desktop secondary left a stale view or stopped the normal primary.'
        }
        [HelEngine.Windows.Tests.WindowLifecycleProbe]::Close($windows['HelEngine Windows Host'])
        if (-not $playerProcess.WaitForExit(10000) -or $playerProcess.ExitCode -ne 0) { throw 'Normal primary shutdown failed after desktop close.' }
        Add-Result 'closing desktop secondary preserves the process; closing primary ends it'
    } finally {
        if (-not $playerProcess.HasExited) { $playerProcess.Kill(); $playerProcess.WaitForExit() }
    }

    $playerProcess = Start-LifecyclePlayer @('--scene', 'axis_test', '--window-mode', 'desktop', '--window', 'preview,normal,700,40,640,360')
    try {
        $windows = Wait-PlayerWindows $playerProcess @('HelEngine Desktop', 'preview')
        [HelEngine.Windows.Tests.WindowLifecycleProbe]::Close($windows['HelEngine Desktop'])
        if (-not $playerProcess.WaitForExit(10000) -or $playerProcess.ExitCode -ne 0) { throw 'Closing desktop primary did not release the remaining views.' }
        Add-Result 'desktop primary shutdown releases all views'
    } finally {
        if (-not $playerProcess.HasExited) { $playerProcess.Kill(); $playerProcess.WaitForExit() }
    }
    [IO.File]::WriteAllText((Join-Path $resolvedWorkRoot 'results.json'), ($results.ToArray() | ConvertTo-Json), [Text.Encoding]::UTF8)
    Write-Output 'RESULT: PASS desktop acceptance'
} finally {
    if ($profileExisted) { [IO.File]::WriteAllBytes($profilePath, $savedProfile) }
    elseif (Test-Path -LiteralPath $profilePath) { Remove-Item -LiteralPath $profilePath }
}
