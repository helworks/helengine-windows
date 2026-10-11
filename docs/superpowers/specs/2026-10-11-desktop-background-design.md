# Desktop background window role (stage 3d)

Date: 2026-10-11
Status: Implemented; native, builder, live-shell, and shared-scene acceptance passed. Current-main DemoDisc packaging remains blocked by its independent asset-schema migration.
Series: 3a regression extensions -> 3b per-monitor DPI -> 3c multiple windows -> **3d desktop background**.

## Behavior

A desktop view draws the existing shared scene in Explorer's wallpaper layer behind desktop icons. It is opaque, disabled for input, absent from the taskbar, and shown without activation. Normal and overlay views can coexist in the same process and continue to share one Core, one simulation, one DirectX11 device/context, and renderer caches.

Primary desktop mode covers each monitor present at startup. The primary display is the primary view; other displays receive `desktop-monitor-2`, `desktop-monitor-3`, and so on. Coverage uses full physical display rectangles, including negative coordinates. The primary remains the shared UI layout reference. This does not assign independent scenes or cameras to displays.

An explicitly configured secondary desktop view uses its requested physical screen rectangle. Resizing or closing it follows the multiple-window lifecycle. A desktop view keeps rendering while a normal primary is minimized. Closing the primary, including a desktop primary, ends the player.

## Configuration

```powershell
# Wallpaper on every startup display.
.\helengine_windows.exe --window-mode desktop

# Wallpaper plus an ordinary control/preview window.
.\helengine_windows.exe --window-mode desktop --window preview,normal,700,40,640,360

# Explicit desktop view alongside an ordinary primary and a transparent overlay.
.\helengine_windows.exe --window wallpaper,desktop,0,0,1920,1080 --window glass,overlay,0,0,640,360
```

`profile.json` also accepts `"windowMode":"desktop"`; command-line selection overrides the profile as before. Existing overlay bounds/background options apply only to overlays. Desktop mode uses the camera background with opaque native presentation.

The total remains limited to 16 views, including automatically created monitor views. The `desktop-monitor-` tag prefix is reserved when the primary is a desktop view. Explicit dimensions remain 1..16384 pixels. Desktop bounds whose right/bottom coordinates would overflow a signed screen coordinate are rejected. Configuration errors exit with code 2.

Desktop children are created in the shell parent's DPI context, and the calling thread context is restored before other windows are created. Monitor enumeration and screen diagnostics use physical pixels; desktop client queries use the child's awareness. Ordinary windows keep the selected process DPI policy.

## Native implementation

`Win32DesktopHost` owns discovery and attachment validation. It owns no Explorer windows.

- Classic layout: find the same-process top-level WorkerW behind the icon-bearing window, and create a child in it.
- Raised Windows 11 layout: create a layered child under Progman, below SHELLDLL_DefView and above its wallpaper WorkerW.
- If the wallpaper worker is absent, request it once with a bounded Explorer message. A missing program manager, icon view, worker, or unsupported topology fails explicitly.
- Create directly with the resolved parent, rather than using cross-process SetParent. No shell window is hidden, resized, destroyed, or reparented.
- Use WS_CHILD, WS_DISABLED, clipping styles, WS_EX_LAYERED, WS_EX_TRANSPARENT, WS_EX_NOACTIVATE, and WS_EX_TOOLWINDOW. Initialize layered alpha to 255.
- Desktop DirectX surfaces use opaque blt presentation (DISCARD, one buffer). Normal flip presentation and overlay DirectComposition remain unchanged.
- The executable compatibility manifest enables layered child windows without setting a new default DPI policy.
- Each message-pump cycle validates live desktop attachments. A destroyed secondary retains its close behavior; loss of a live attachment fails explicitly. Explorer restarts do not trigger automatic reattachment.

Desktop fingerprints append `desktopParent` and physical `desktopRect` fields. Ordinary/overlay fingerprint fields retain their existing behavior.

The raised-shell arrangement follows the Explorer guidance documented in [Lively's desktop host](https://github.com/lively-community/lively/blob/core-separation/src/Lively/Lively/Core/WinDesktopCore.cs). Windows documents [layered-child manifest requirements](https://learn.microsoft.com/en-us/windows/win32/winmsg/using-windows) and [cross-process SetParent DPI behavior](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setparent). WorkerW discovery depends on Explorer's shell topology, rather than a public wallpaper-host contract.

## Validation and provenance

Artifacts: `C:\dev\helworks\builds\desktop-background`.

- Native configuration tests: pass, including desktop parsing, negative positions, and overflowing bounds.
- Native desktop tests: pass for classic and raised substitute shells, invalid topology, attachment loss, opaque alpha, input styles, physical sizing, DPI restoration, shared devices, blt presentation, and resize.
- Opt-in `helengine_windows_desktop_tests --live-shell`: pass on the actual Windows 11 raised Progman layout, one 3840x2160 monitor at 96 DPI, 30 presentations, no Present failures, correct sibling order, focus preserved, and Explorer parent preserved after cleanup.
- Windows builder tests against committed engine main 077b5b02: 206 passed.
- Regression tool tests: 113 passed, including required desktop fields, rejection of missing/invalid-mode diagnostics, and changed parent/coverage comparison. Desktop fields are retained in strict log parsing and compared only for desktop views.
- Canonical shared-scene acceptance: pass for normal/overlay references, mixed normal+desktop+overlay views, primary desktop, profile selection, process/default and explicit per-monitor DPI preservation, resize, minimized-primary independence, secondary close, and primary shutdown. Every compared image had zero differing pixels.
- `scripts/test-desktop-windows.ps1` exports renderer back buffers only. It preserves profile bytes and enumerates descendants while filtering handles by player process ownership.

Current engine main 077b5b02 and the pinned DemoDisc 5cc124ee do not build together: the project code and serialized scene payloads still reference removed RenderOrder2D fields. The first attempt stopped at the C# reference; a validation-only copy of the existing updated label component then exposed the serialized-field error. Neither production engine nor DemoDisc sources were changed.

Full-scene acceptance therefore used engine 0db0df09 and builder ff62f3f, the previously accepted stage-3c pair, with exactly the new native source, manifest, and tests. Source SHA-256 equivalence is recorded in `validation-native-source-sha256.json`; generated C++ was produced by the canonical build and was not edited. The current builder's cache-contract changes were preserved. Existing committed visual goldens were not re-recorded.

Only one physical monitor at 96 DPI was available. Multiple-display coverage is implemented, but real mixed-DPI hardware, display hotplug, and an actual Explorer restart were not exercised. Shell-loss cases use isolated substitute windows. Runtime display reconfiguration, automatic shell recovery, scene assignment per display, and fullscreen roles remain outside this stage.
