# Multiple windows in one process (stage 3c)

Date: 2026-10-06
Status: Implemented in isolated worktrees; validation results below. Not merged.
Series: 3a regression extensions -> 3b per-monitor DPI -> **3c multiple windows** -> 3d desktop-background role.

## Behavior and scope

One player process owns one Core, one simulation and one loaded scene. Additional windows display that scene using the existing cameras. Each window has an independent client size, swap chain, render target, depth buffer, presentation result and overlay hit-test state. All views share one D3D11 device/context and renderer asset caches.

Core.Update runs once per host frame. Core.Draw commits pending scene operations once; further views draw the same committed state. One visible view paces presentation, and the others present without adding another vertical-sync wait. The focused player window supplies the input coordinate space. Secondary windows open without activation.

The primary window remains the shared UI layout reference, including its last usable dimensions while minimized. Resizing a secondary window updates its registry entry and GPU surface without changing that reference. Closing a secondary unregisters it and releases its resources. Closing the primary ends the process. A visible secondary continues rendering while the primary is minimized; when all views are minimized, the host waits for activity.

These are views of a shared scene. Independent scenes, cameras assigned to specific windows, independently laid-out UI, runtime window creation, profile-driven window lists and the desktop-background role are future work.

## Configuration

`--window tag,normal|overlay,left,top,width,height` is repeatable. The primary window still uses the existing profile and command-line settings. Without this flag, the original single-window render path is used.

- Up to 15 additional windows (16 total).
- A tag contains 1..64 ASCII letters, digits, underscores or hyphens. `main` is reserved. Additional tags must be unique ignoring case, so their capture filenames cannot collide on Windows.
- Coordinates are signed decimal screen positions; negative monitor coordinates are valid.
- Client dimensions must be decimal integers from 1 to 16384. Malformed fields, duplicates and missing values fail during parsing.
- All windows inherit the process DPI policy. With `--dpi-awareness permonitorv2`, dimensions are physical pixels. Secondary normal windows reuse the existing DPI sizing/message handlers.
- Secondary overlays use a transparent background and independent premultiplied-alpha state. Click-through sampling belongs to each overlay.

Example launcher arguments:

```powershell
-ArgumentList @('--scene', 'axis_test', '--dpi-awareness', 'permonitorv2',
  '--window', 'preview,normal,700,40,640,360',
  '--window', 'glass,overlay,0,0,640,360')
```

## Finite runs and captures

`--frames N` counts host frames with at least one visible view. Each `HOST_FINGERPRINT` includes `window=<tag>` and reports that view's actual presentations. A minimized view can report fewer than N. The host keeps this frame-count policy even after its last secondary window closes.

For `--capture path/scene.bmp`, the primary writes `scene.bmp`, while a secondary tagged `preview` writes `scene.preview.bmp`. Captures export the renderer's back buffer; they do not read desktop pixels. A view still minimized on the final frame cannot provide a capture and causes an explicit failure. Closed secondary views no longer participate. The existing deterministic `--hit-test-probe` requires the primary window to be an overlay; secondary overlays also report the probe when requested in that configuration.

## Core and host changes

RenderManager3D stores registered sizes by handle and separates the primary layout handle from the selected input handle. Add/resize/select/remove operations reject invalid state instead of creating missing registrations. PointerInteractionSystem uses InputWindowSize. The editor test host explicitly registers its virtual window before sending resize events. Fixtures that previously used AddWindow to configure that already-registered window now use OnWindowResize.

Win32SecondaryWindow owns secondary presentation resources and diagnostics. The renderer selects the current output surface and restores the matching 2D/3D blend modes before each view. GPU uploads continue using the shared device. Secondary WM_DESTROY does not post WM_QUIT. Window-procedure exceptions from every view propagate through the message pump.

## Worktrees and build provenance

- Windows: `C:\dev\helworks\helengine-windows\.worktrees\multiple-windows`, branch `feature/multiple-windows`, based on stage 3b `4de1705`.
- Core: `C:\dev\helworks\helengine\.worktrees\multiple-windows`, branch `feature/multiple-windows`, based on regression reference `dd9ca693`.
- Regression project: DemoDisc pinned to `5cc124eec06b8db729b2f9b15d99d26cb1ca8cc3`.
- Vendor submodules use the reference worktree's exact pinned commits, cloned from local Git repositories when SSH was unavailable.
- Build outputs and logs: `C:\dev\helworks\builds\multiple-windows`.

The canonical `scripts/run-regression.ps1` builds through the editor/code generator with `-HelengineRoot` pointing to the isolated core worktree and `-PlatformsManifestPath C:\dev\helworks\helengine\user_settings\platforms.json`. Generated C++ is never patched. Existing main checkouts and their unrelated changes are preserved.

## Validation

The acceptance script runs the canonical launcher with test-owned windows and restores the player's original profile bytes after testing:

```powershell
powershell -NoProfile -File .\scripts\test-multiple-windows.ps1 `
  -ArtifactPath C:\dev\helworks\builds\multiple-windows\regression\player\helengine_windows.exe `
  -RegressionToolPath .\tools\regression\bin\Release\net9.0-windows\helengine.windows.regression.dll `
  -WorkRoot C:\dev\helworks\builds\multiple-windows\acceptance
```

It covers two normal windows; a primary plus a normal view and transparent overlay; primary/secondary minimize and restore; secondary resize to 800x600; closing secondary then primary; and finite frame accounting after the last secondary closes. Equal-sized normal views match the existing RGB golden, and the overlay matches the existing RGBA golden and premultiplication check. WindowLifecycleProbe enumerates only visible windows belonging to the test process and operates only on those handles.

The explicit CMake target `helengine_windows_window_tests` exercises the production configuration parsers without creating windows. Core tests cover independent registry sizes, selected input, unregistering, invalid handles and primary layout preservation during minimize. Existing source tests retain their default-path invariants while allowing a separate multi-window render path.

### Results

| Check | Result |
|---|---|
| Canonical native BuildOnly, with the final primary-minimize correction generated from C# | PASS |
| Core suite | 210 passed |
| Windows builder suite | 206 passed |
| Editor layout/input checks | 28 passed |
| Updated editor window fixtures | 12 passed |
| Native configuration parser target | PASS, including case-insensitive capture-tag collisions |
| Final real multi-window acceptance | PASS for all scenarios, including finite close |
| Single-window visual Verify | All 11 scene goldens and fingerprints passed, along with idle, overlay, overlay+idle and DPI scenarios; image comparisons reported 0 differing pixels |
| Full editor Verify, initial run | Executed 3124; 3077 passed, 47 failed. Compared with the committed baseline: 5 newly failing cases and 5 fixed cases |
| Render-validation suite | No additional failure beyond its existing baseline |

Four new failures in the initial full run were duplicate registrations in the old editor fixture assembly. Those fixtures were corrected and all 12 tests in their three classes passed afterward. The remaining failure is `EditorPlatformBuildGraphWorkspaceFactoryTests.Create_WhenWindowsNativeObjectPathIsBudgeted_LeavesCmakeObjectPathHeadroom`: the current account's default temporary path produces a 243-character native object filename, exceeding the test's 242-character budget. The same test also fails on the unchanged regression-reference checkout; its source is byte-identical. This is an existing environment-dependent limitation, and the initial aggregate Verify result remains FAIL. No failure allowlist or golden was changed, and the full editor suite was not repeated after the fixture correction.

Logs are kept in `C:\dev\helworks\builds\multiple-windows`: `buildonly.log`, `core-tests.log`, `builder-tests.log`, `editor-tests.log`, `editor-window-fixture-tests.log`, `native-tests.log`, `acceptance.log`, `verify.log` and `reference-path-budget-test.log`. The full run's TRX files are under `regression/trx`.

Physical cross-monitor migration and differing display scale factors remain unverified because the available desktop has one 96-DPI monitor.
