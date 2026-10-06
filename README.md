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

## Multiple Windows (stage 3c)

Repeat `--window tag,mode,left,top,width,height` to add views of the running scene. Modes are `normal` and `overlay`; positions use screen coordinates and dimensions are client pixels in the selected DPI context. Each view shares the simulation and GPU assets, and owns its presentation surface.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\launch_in_emulator.ps1 `
  -ArtifactPath C:\dev\helworks\builds\multiple-windows\regression\player\helengine_windows.exe `
  -ArgumentList @('--scene', 'axis_test', `
    '--window', 'preview,normal,700,40,640,360', `
    '--window', 'glass,overlay,0,0,640,360')
```

Closing a secondary view leaves the player running. Closing the primary view ends the process. See the [stage 3c design and validation](docs/superpowers/specs/2026-10-06-multiple-windows-design.md) for configuration limits, captures, and the shared-scene scope.

## More Docs

- [Docker Build Notes](docs/Docker.md)
- [Platform Notes](docs/PlatformNotes.md)
- [Windows Profiler Capture](docs/WindowsProfilerCapture.md)
