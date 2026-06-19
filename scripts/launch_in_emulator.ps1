param(
    [Parameter(Mandatory = $true)]
    [string]$ArtifactPath
)

$ErrorActionPreference = 'Stop'

$resolvedArtifactPath = [System.IO.Path]::GetFullPath($ArtifactPath)
if (-not (Test-Path -LiteralPath $resolvedArtifactPath -PathType Leaf)) {
    throw "Artifact was not found: $resolvedArtifactPath"
}

if ([System.IO.Path]::GetExtension($resolvedArtifactPath) -ine '.exe') {
    throw "Expected a .exe artifact but got '$resolvedArtifactPath'."
}

$process = Start-Process -FilePath $resolvedArtifactPath -WorkingDirectory (Split-Path -Path $resolvedArtifactPath -Parent) -PassThru
Write-Output ("ARTIFACT=" + $resolvedArtifactPath)
Write-Output ("PROCESS_ID=" + $process.Id)
