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
    # Start-Process joins the elements with plain spaces, so an element that is empty or contains whitespace or a
    # quote is wrapped in double quotes with the CommandLineToArgvW escaping rules (backslashes before a quote or the
    # closing quote are doubled, embedded quotes become \"). Simple elements are passed through unchanged.
    $quotedArgumentList = New-Object System.Collections.Generic.List[string]
    foreach ($argument in $ArgumentList) {
        if ($argument.Length -eq 0 -or $argument -match '[\s"]') {
            $escapedArgument = ($argument -replace '(\\*)"', '$1$1\"') -replace '(\\+)$', '$1$1'
            $quotedArgumentList.Add('"' + $escapedArgument + '"')
        }
        else {
            $quotedArgumentList.Add($argument)
        }
    }
    $startArguments['ArgumentList'] = $quotedArgumentList.ToArray()
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
