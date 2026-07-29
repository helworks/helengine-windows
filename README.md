# HelEngine Windows Host

This repository contains the Windows platform host and builder integration for HelEngine.

## Build

```powershell
dotnet run --project ..\helengine\tools\build-waiter\helengine.buildwaiter.csproj -- `
  --output ..\helprojs\city\windows-build `
  --require helengine_windows.exe `
  -- powershell -NoProfile -ExecutionPolicy Bypass -File ..\helengine\scripts\build-platform.ps1 `
  -Project ..\helprojs\city\project.heproj `
  -Platform windows `
  -Output ..\helprojs\city\windows-build
```

The Build Waiter returns successfully only after `helengine_windows.exe` is fresh and non-empty.

## Run In Emulator

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\launch_in_emulator.ps1 `
  -ArtifactPath ..\helprojs\city\windows-build\helengine_windows.exe
```

## More Docs

- [Docker Build Notes](docs/Docker.md)
- [Platform Notes](docs/PlatformNotes.md)
- [Windows Profiler Capture](docs/WindowsProfilerCapture.md)
