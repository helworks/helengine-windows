param(
    [Parameter(Mandatory = $true)]
    [string]$ArtifactPath,

    [Parameter()]
    [string[]]$ArgumentList,

    [Parameter()]
    [switch]$Wait
)

$ErrorActionPreference = 'Stop'

$resolvedArtifactPath = [System.IO.Path]::GetFullPath($ArtifactPath)
if (-not (Test-Path -LiteralPath $resolvedArtifactPath -PathType Leaf)) {
    throw "Artifact was not found: $resolvedArtifactPath"
}

if ([System.IO.Path]::GetExtension($resolvedArtifactPath) -ine '.exe') {
    throw "Expected a .exe artifact but got '$resolvedArtifactPath'."
}

if ($ArgumentList -and $ArgumentList.Count -gt 0) {
    $startArguments = @{ FilePath = $resolvedArtifactPath; WorkingDirectory = (Split-Path -Path $resolvedArtifactPath -Parent); PassThru = $true }
    $startArguments['ArgumentList'] = $ArgumentList
    $process = Start-Process @startArguments
}
else {
    $process = Start-Process -FilePath $resolvedArtifactPath -WorkingDirectory (Split-Path -Path $resolvedArtifactPath -Parent) -PassThru
}
if ($Wait) {
    # Opening the handle now keeps the exit code readable after the process ends (Start-Process does not hold one).
    $null = $process.Handle
}
Write-Output ("ARTIFACT=" + $resolvedArtifactPath)
Write-Output ("PROCESS_ID=" + $process.Id)
if ($Wait) {
    $process.WaitForExit()
    Write-Output ("EXIT_CODE=" + $process.ExitCode)
    exit $process.ExitCode
}
