# HelEngine Windows Host

This repository contains the Windows platform host and builder integration for HelEngine.

## Build

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ..\helengine\artifacts\build-platform.ps1 `
  -Project ..\helprojs\city\project.heproj `
  -Platform windows `
  -Output ..\helprojs\city\windows-build
```

## Run In Emulator

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\launch_in_emulator.ps1 `
  -ArtifactPath ..\helprojs\city\windows-build\helengine_windows.exe
```

## More Docs

- [Docker Build Notes](docs/Docker.md)
- [Platform Notes](docs/PlatformNotes.md)
