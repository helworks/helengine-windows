# Multiple windows in one process (stage 3c)

Date: 2026-10-06
Status: Implemented and validated against the committed engine and Windows main branches. Integrated locally; unrelated working-tree edits preserved.
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

## Integration fixes

Invocation directory names retain the first 64 SHA-256 bits in 13 lowercase base32 characters rather than 16 hexadecimal characters. This removes three path characters without reducing hash entropy or weakening the native object-path budget.

Editor-owned authoring components, including BlueprintInstanceComponent, resolve through the explicitly owned editor assembly. Project components still use the configured script resolver and propagate its failures; they do not probe the default load context. This restores canonical scene packaging after the committed main's type-resolution change.

CameraProjectionUtils uses the two-argument ArgumentOutOfRangeException constructor supported by the native runtime. Invalid inputs still throw with their parameter names and validation messages. Generated C++ is rebuilt from the corrected C# sources.

## Worktrees and build provenance

- Windows: `C:\dev\helworks\helengine-windows\.worktrees\multiple-windows`, branch `feature/multiple-windows`, based on stage 3b `4de1705`.
- Core: `C:\dev\helworks\helengine\.worktrees\multiple-windows`, branch `feature/multiple-windows`, based on regression reference `dd9ca693`.
- Regression project: DemoDisc pinned to `5cc124eec06b8db729b2f9b15d99d26cb1ca8cc3`.
- Vendor submodules use the reference worktree's exact pinned commits, cloned from local Git repositories when SSH was unavailable.
- Build outputs and logs: `C:\dev\helworks\builds\multiple-windows`.

The canonical `scripts/run-regression.ps1` builds through the editor/code generator with `-HelengineRoot` pointing to the isolated core worktree and `-PlatformsManifestPath C:\dev\helworks\helengine\user_settings\platforms.json`. Generated C++ is never patched. The engine feature includes main `9d7e1a0e`; the Windows feature includes main `61ede6d`. Integration into the main checkouts uses a fast-forward, with overlapping local edits combined through a checked three-way merge. Unrelated working-tree edits remain uncommitted and are outside this validation snapshot.

## Validation

The acceptance script runs the canonical launcher with test-owned windows and restores the player's original profile bytes after testing:

```powershell
powershell -NoProfile -File .\scripts\test-multiple-windows.ps1 `
  -ArtifactPath C:\dev\helworks\builds\multiple-windows\regression-integrated\player\helengine_windows.exe `
  -RegressionToolPath .\tools\regression\bin\Release\net9.0-windows\helengine.windows.regression.dll `
  -GoldenRootPath C:\dev\helworks\builds\multiple-windows\visual-main-reference\golden `
  -WorkRoot C:\dev\helworks\builds\multiple-windows\acceptance-integrated
```

It covers two normal windows; a primary plus a normal view and transparent overlay; primary/secondary minimize and restore; secondary resize to 800x600; closing secondary then primary; and finite frame accounting after the last secondary closes. Equal-sized normal views match the selected RGB reference, and the overlay matches the selected RGBA reference and premultiplication check. WindowLifecycleProbe enumerates only visible windows belonging to the test process and operates only on those handles.

The explicit CMake target `helengine_windows_window_tests` exercises the production configuration parsers without creating windows. Core tests cover independent registry sizes, selected input, unregistering, invalid handles and primary layout preservation during minimize. Existing source tests retain their default-path invariants while allowing a separate multi-window render path.

### Results

| Check | Result |
|---|---|
| Canonical integrated native build | PASS, generated directly from corrected C# |
| Aggregate Verify against historical goldens | FAIL for ClearType differences and five obsolete fixture invocations; the five fixture cases subsequently passed after correction |
| Core suite | 210 passed |
| Editor isolation/path-budget tests | 39 passed |
| Editor component persistence tests | 53 passed |
| Corrected BuildRequest fixture cases | 5 passed after removing the obsolete twelfth reflection argument |
| Editor layout/input checks | 28 passed |
| Updated editor window fixtures | 12 passed |
| Native configuration parser target | PASS, including case-insensitive capture-tag collisions |
| Integrated real multi-window acceptance | PASS for normal/overlay captures, resize, minimize/restore, close and finite-frame scenarios |
| Independent current-main visual reference | All 11 scene captures plus idle, overlay, overlay+idle and per-monitor-v2 DPI captures match with 0 differing pixels; two reference captures per scene are stable |
| Host fingerprints and non-image checks | PASS in the canonical Verify, including idle pacing, overlay alpha/click-through and available 96-DPI configuration |
| helengine.editor.tests.trx | Executed 3046; 3039 passed, 7 failed; five obsolete fixture cases subsequently corrected and passed; two remaining failures are already in the baseline |
| helengine.render.validation.tests.trx | Executed 1; 0 passed, 1 failed; no new failure beyond committed baseline |
| helengine.windows.builder.tests.trx | Executed 206; 206 passed, 0 failed; no new failure beyond committed baseline |

The complete Verify was repeated after the window-fixture, path-budget and production integration fixes. It executed 3046 editor cases: 3039 passed and seven failed. Five failures were obsolete twelve-argument reflection calls to main's eleven-argument BuildRequest; those five fixtures were then corrected and all five cases passed. The other two failures require installed-platform settings absent from the isolated worktree and already appear in the baseline. The complete suite was not repeated after this final test-only correction.

The committed goldens predate main's intentional ClearType font change (`61ede6d`), and report approximately 1.6% different pixels on the text. The aggregate Verify log therefore remains FAIL, including the now-corrected fixture failures. No failure allowlist or committed golden was changed.

To separate the committed main change from stage 3c, a detached Windows reference combines stage 3b `4de1705` with ClearType `61ede6d`, without the multiple-window host commit. It is built canonically against the same validated engine and pinned DemoDisc revision. Two captures of every scene are stable. All 11 integrated scene captures and six additional normal/overlay/idle/DPI captures match that reference with zero differing pixels. The final multi-window acceptance uses those independent reference images through `-GoldenRootPath`; omission of that option still selects the committed goldens and preserves strict failures.

Refreshing the committed visual baselines is separate maintenance for ClearType and follows the deliberate Record/Verify workflow in `regression/README.md`. Test-suite baselines remain unchanged. There is no unaddressed newly failing suite case after the targeted fixture correction.

The main working tree's pending native-cache changes retain a twelve-argument BuildRequest signature. Its five fixture calls retain their original bytes to match that pending implementation. The committed, isolated validation snapshot uses the eleven-argument signature and matching fixtures. This preserves the existing pending work without committing it into stage 3c.

Final logs are in `C:\dev\helworks\builds\multiple-windows`: `verify-integrated.log`, `native-tests-integrated.log`, `acceptance-integrated.log` and `visual-reference-build.log`. Strict visual comparison results and reference images are under `visual-main-reference`. Full-suite TRX files are under `regression-integrated/trx`; integration snapshots and preservation evidence are under `integration`. Runner and display-probe sources are under `provenance`.

The desktop exposes one 3840x2160 monitor at 96 DPI, recorded in `display-configuration.txt`. Physical cross-monitor migration between differing scale factors remains unverified. The existing per-monitor-v2 capture scenarios passed at the available 96-DPI configuration.
