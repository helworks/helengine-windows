# Opt-in Per-Monitor DPI Awareness (subproject 3b): Design

Date: 2026-09-27
Status: Draft for review (Helena)
Series: subproject 3 = 3a net extensions (merged at 37e0360) → **3b opt-in per-monitor DPI** → 3c multiple windows in one process (core + host) → 3d desktop-background role.

## Goal

The Windows player can opt into Per-Monitor v2 DPI awareness, so an overlay or a future desktop background renders 1:1 in physical pixels and stays sharp on scaled displays. Today the player is DPI-unaware: it has no manifest and makes no API call, so on a scaled monitor Windows bitmap-stretches its output and virtualizes monitor coordinates.

## Decisions (Helena, 2026-09-27)

- **Host only.** helengine core does not change.
- **Opt in at run time** through the process API, not an embedded manifest, because a manifest would change the default path.
- **With awareness on, the profile resolution means physical pixels.** For example, 640×360 is a 640×360-pixel client area on any monitor. On a scaled monitor the window looks smaller. Content/UI scaling is out of scope; it would need a core scale factor, which is left for a later subproject.

## Non-goals

- No UI or content scale factor in the core.
- No mixing of unaware and aware windows in one process (thread or window DPI contexts).
- No multiple windows (that is 3c).
- No `system` or `permonitor` (v1) modes. The only values are `unaware` and `permonitorv2`.

## 1. Configuration

- A new enum `Win32DpiAwareness { Unaware, PerMonitorV2 }`. Its names are `unaware` and `permonitorv2`, parsed and formatted by a names class in the same style as `win32_window_mode_names`.
- **Command line:** `--dpi-awareness unaware|permonitorv2`. An unknown value is an invalid argument (exit code 2 through `Win32ExitRequest`, like `--window-mode`).
- **Profile:** an optional `"dpiAwareness": "unaware"|"permonitorv2"` field in profile.json.
  - It follows the existing optional-field pattern, with a present flag. A profile without the field is read, and rewritten when repaired, byte-identically to today.
  - An invalid value is a configuration error, raised the same way as an invalid `windowMode`.
- **Resolution:** a `Win32DpiAwarenessSettings::Resolve(profile, commandLineOptions)` class, in the style of `Win32WindowModeSettings`. The command line wins over the profile, and the default is `Unaware`.
- **Scope:** the setting works in every mode (normal and overlay, with or without `--frames`). It is not restricted to diagnostics.

## 2. Applying awareness

- When the resolved value is `PerMonitorV2`, the host calls `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)` before creating any window or D3D device. The plan must confirm the exact call site in `Win32Application` and that no HWND exists before it.
- If the call fails, the player throws `std::runtime_error` naming the call and `GetLastError()`, and exits through the normal startup-failure path. It never silently continues unaware.
- When the value is `Unaware`, no new call is made. The process stays exactly as today.

## 3. Window sizing with awareness on

A new host class, `Win32DpiWindowSizing`, holds the pure sizing math so it can be pinned by tests and reused in 3c. It provides:
- `OuterSizeForClient(clientWidth, clientHeight, style, exStyle, dpi)`, using `AdjustWindowRectExForDpi`;
- the `WM_DPICHANGED` placement rule described below.

**Normal window:**
- **Unaware:** creation uses today's exact calls (`AdjustWindowRect`, `CW_USEDEFAULT`).
- **PerMonitorV2:**
  - The window is created as today.
  - Right after creation, the host reads `GetDpiForWindow`.
  - If the outer size computed for that DPI differs from the size it was created with, the host calls `SetWindowPos` with `SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE`, so the client area is exactly the profile resolution in physical pixels.
  - At 96 dpi the sizes are equal, so no `SetWindowPos` happens.

**Overlay window:** its bounds already come from `GetMonitorInfoW` and are physical under PerMonitorV2. No extra sizing is needed; at 96 dpi the rectangle is identical to today's.

**`WM_DPICHANGED`** (only aware windows receive it):
- **Normal:** move to the suggested rectangle's top-left, but size the window with `OuterSizeForClient` at the new DPI, so the client pixel size stays the same. The core therefore sees no resize, and the known core `OnWindowResize` issue is not reached.
- **Overlay:** keep the current rectangle and ignore the suggestion. The overlay covers the monitor in physical pixels, which a DPI change does not alter.
- The handler returns 0.
- In unaware mode the message never arrives, so the default path cannot reach this code.

## 4. Diagnostics

- The existing `HOST_FINGERPRINT` fields `dpi` and `dpiAwareness` already report the result. With awareness on, `dpiAwareness=permonitorv2` is expected.
- No new fingerprint fields are needed.

## 5. Regression net

- A new manifest kind, `dpiAware`, has two runs of the smoke scene (axis_test), both with `--dpi-awareness permonitorv2`:
  - **normal:** `--frames 30 --fixed-delta 0.016666 --capture`. The capture must equal the existing axis_test golden exactly. The fingerprint is recorded and compared per window, with `dpiAwareness=permonitorv2` required.
  - **overlay:** the overlay arguments with the recorded transparent probe. It checks `compare-rgba` against the existing overlay golden, `check-premultiplied`, and the exact `HIT_TEST` line. The fingerprint is recorded, with `dpiAwareness=permonitorv2` required.
- Record and Verify both fail if either run reports any `dpiAwareness` other than `permonitorv2`. Verify also fails if the entry is missing.
- **Re-record once:** every existing golden must stay byte-identical. The only manifest changes allowed are the new `dpiAware` entry and `elapsedMs` jitter.

## 6. Known blind spot

- Helena's machine has one 3840×2160 monitor at 100% (96 dpi). So dpi ≠ 96, `WM_DPICHANGED` and cross-monitor moves cannot be exercised automatically here, and the display scaling must not be changed by automation.
- The sizing math is isolated in `Win32DpiWindowSizing` and pinned by source tests.
- The README documents a manual check for a scaled display: run with `--dpi-awareness permonitorv2 --frames 30`, then check the fingerprint's `dpi`, the `client=` size and the capture size.

## 7. Verification

- **builder.tests source tests:**
  - enum and names;
  - flag parsing and exit code 2 on bad values;
  - profile optional field with its present flag and byte-identical rewrite;
  - Resolve precedence;
  - `SetProcessDpiAwarenessContext` called only on the `PerMonitorV2` branch and before window creation;
  - throw on failure;
  - `CreateNormalWindow` unaware path unchanged;
  - `WM_DPICHANGED` handling per mode;
  - `Win32DpiWindowSizing` usage.
- **Acceptance:**
  - `-Record` pinned to the manifest's projectCommit, with goldens byte-identical;
  - `-Verify` twice;
  - a negative check: temporarily edit the `dpiAware` entry's recorded `dpiAwareness` to `unaware`, confirm Verify FAILs, then restore it;
  - the no-argument boot unchanged (`dpiAwareness=unaware` in a `--frames` run without the flag, and no fingerprint lines without `--frames`).
