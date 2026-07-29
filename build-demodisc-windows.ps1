$ErrorActionPreference = "Stop"

$BuildScript = "C:\dev\helworks\helengine\scripts\build-platform.ps1"
$Project = "C:\dev\helprojs\demodisc\project.heproj"
$Output = "C:\dev\helprojs\demodisc\output\windows-manual"

& $BuildScript `
    -Project $Project `
    -Platform windows `
    -Configuration Release `
    -Output $Output

if ($LASTEXITCODE -ne 0) {
    throw "Windows build failed with exit code $LASTEXITCODE."
}

Write-Host "Build complete: $Output"
