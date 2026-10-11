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

Repeat `--window tag,mode,left,top,width,height` to add views of the running scene. Modes are `normal`, `overlay` and `desktop`; normal and overlay positions follow the selected DPI context; desktop bounds are physical screen pixels. Each view shares the simulation and GPU assets, and owns its presentation surface.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\launch_in_emulator.ps1 `
  -ArtifactPath C:\dev\helworks\builds\multiple-windows\regression-integrated\player\helengine_windows.exe `
  -ArgumentList @('--scene', 'axis_test', `
    '--window', 'preview,normal,700,40,640,360', `
    '--window', 'glass,overlay,0,0,640,360')
```

Closing a secondary view leaves the player running. Closing the primary view ends the process. See the [stage 3c design and validation](docs/superpowers/specs/2026-10-06-multiple-windows-design.md) for configuration limits, captures, and the shared-scene scope.

## Camera Routing

Cameras can select a view using `OutputWindowTag`: empty renders in every view, main selects the primary, and a secondary tag such as glass selects that named view. Combine this with existing layer masks to show wallpaper and overlay UI from separate cameras in one scene and simulation.

Pointer routing uses the selected input view's camera assignments. Camera-bound UI viewports, anchors and reference-canvas scaling follow that camera's window size and react independently to resizing. Shared cameras and screen-bound viewports use the primary size. See the [camera routing design and validation](docs/superpowers/specs/2026-10-11-window-camera-routing-design.md) for authoring and lifecycle rules.

## Desktop Background (stage 3d)

Use `--window-mode desktop` to cover every startup monitor behind desktop icons. Add `--window wallpaper,desktop,left,top,width,height` for an explicit wallpaper view alongside ordinary or overlay windows. Desktop views are opaque, ignore input, and open without activation.

See the [stage 3d design and validation](docs/superpowers/specs/2026-10-11-desktop-background-design.md) for Explorer compatibility, DPI policy, diagnostics, and the current DemoDisc asset-migration limitation.

## More Docs

- [Docker Build Notes](docs/Docker.md)
- [Platform Notes](docs/PlatformNotes.md)
- [Windows Profiler Capture](docs/WindowsProfilerCapture.md)
