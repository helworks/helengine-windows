# Windows Player Overlay Window (DirectComposition, per-pixel alpha) — Design

Date: 2026-09-25
Status: Draft for review (Helena)
Series: subproject 2 of 4 (0 safety net ✅ → 1 idle throttle ✅ → **2 overlay window** → 3 per-monitor DPI + multi-window, including the desktop-background role).
Golden rule: additive and opt-in. With `windowMode` at `normal` (the default), the player behaves identically, and `scripts/run-regression.ps1 -Verify` proves it.

## Goal

An opt-in **overlay** window mode for the Windows player:
- borderless, always on top, no taskbar button;
- composed by DWM with **per-pixel premultiplied alpha** through DirectComposition;
- **click-through where the pixel is transparent**, so clicks reach the windows behind it;
- interactive where it is opaque.

This is the foundation for Gevo's "glass over everything" shell layer.

## Decisions (with Helena)

- **Overlay only.** The desktop-background role (bottom-most, covering each monitor) moves to subproject 3, together with multi-monitor support.
- **Host-only.** All player rendering is hand-written host C++ in `win32_render_bridge.cpp`; `helengine.directx11` is not in the player build. No changes to helengine core, the editor or codegen.

## Non-goals

- Desktop-background mode, multi-monitor support and per-monitor DPI (subproject 3).
- Blur or acrylic behind the window. DWM backdrop effects are a later visual subproject.
- Fixing the pre-existing accident where the 3D pass inherits the 2D blend state in normal mode. Fixing it would change the normal-mode goldens. It is recorded as a known issue for Helena.
- Keyboard focus policy beyond the Windows default. The overlay starts without stealing focus, and it activates normally when an opaque pixel is clicked.

## 1. Configuration (opt-in, the same pattern as idle throttle)

- **Profile** (`profile.json`), optional fields:
  - `windowMode`: `"normal"` (default) or `"overlay"`.
  - `overlayBounds`: `"monitor"` (default; the primary monitor's full bounds) or `"profile"` (the profile resolution at the primary monitor's top-left). `"profile"` exists for deterministic tests.
  - `overlayBackground`: `"camera"` (default; each camera's clear color, premultiplied) or `"transparent"` (clear to 0,0,0,0 regardless of camera).

  Invalid values give exit 2 through `RuntimePlayerProfileConfigurationError` and are never repaired silently. Idle throttle already follows this pattern.
- **Command line** (known flags, strict): `--window-mode normal|overlay`, `--overlay-bounds monitor|profile`, `--overlay-background camera|transparent`, and the test-only `--hit-test-probe <x>,<y>` (§5). The command line overrides the profile.
- A resolved `Win32WindowModeSettings` value object has a `Describe()` method, like `Win32IdleThrottleSettings`. In overlay mode it is logged once at startup.

## 2. Window

- `Win32Window` gains an explicit style configuration: a `Win32WindowStyle` value with style, ex-style and show command.
  - **Normal mode:** exactly today's values. `WS_OVERLAPPEDWINDOW`, ex-style 0, `AdjustWindowRect` and the current `Show()` behavior all stay unchanged. Source tests pin the default text.
  - **Overlay mode:** `WS_POPUP`; `WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED`. The window is shown with `SW_SHOWNOACTIVATE` and positioned to the resolved bounds with `SetWindowPos(HWND_TOPMOST, …, SWP_NOACTIVATE)`.
- The window mode is resolved **before** the window is created, because `WS_EX_NOREDIRECTIONBITMAP` can only be set at creation. `CreateMainWindow` already resolves the profile first.
- `WS_EX_LAYERED` is required so the click-through toggle takes effect. Plan task 1 is a **spike** that proves DirectComposition content with `NOREDIRECTIONBITMAP + LAYERED` is visible and that toggling `WS_EX_TRANSPARENT` passes clicks through. If `LAYERED` breaks composition, the spike finds the working combination (for example, not using `LAYERED` and relying on `WS_EX_TRANSPARENT` alone), and the spec is amended before implementation continues.
- **Confirmed by the spike (Revision 1):** the style set above works as listed. `SetLayeredWindowAttributes` is **not** called: a `LAYERED` window without it still shows the DirectComposition content. Without `LAYERED`, `WS_EX_TRANSPARENT` has no effect on hit-testing. DWM does not hit-test by pixel alpha on its own (a fully transparent pixel still hits the overlay while `WS_EX_TRANSPARENT` is off), so the §5 sampler and toggle are required.

## 3. Swap chain and composition

- `DirectX11Bootstrap` gains a composition path, used only in overlay mode:
  - `CreateSwapChainForComposition(Device, &desc, nullptr, …)` with the same format (B8G8R8A8), 2 buffers, `DXGI_SCALING_STRETCH`, `FLIP_DISCARD` and `DXGI_ALPHA_MODE_PREMULTIPLIED`;
  - `DCompositionCreateDevice`, then `CreateTargetForHwnd(hwnd, TRUE)`, `CreateVisual`, `SetContent(swapChain)`, `SetRoot`, `Commit`;
  - the DComp objects are ComPtr members, declared so they are released before the swap chain and device;
  - link `dcomp`.
- **Normal mode:** the `CreateSwapChainForHwnd` path is untouched.
- `Resize`, the render-target view, `Present(1, 0)`, the back-buffer capture and the fingerprint work unchanged. The fingerprint reports `alpha=1` (PREMULTIPLIED) in overlay mode.

## 4. Premultiplied rendering (host render bridge, overlay mode only)

- **Clear.**
  - `overlayBackground=camera`: clear to the camera's clear color, premultiplied (rgb·a, a).
  - `overlayBackground=transparent`: clear to (0,0,0,0).
  - The fallback `ClearBackBuffer` calls follow the same rule.
- **2D blend.** A second blend state, `PremultipliedDestinationBlendState`: color `SRC_ALPHA / INV_SRC_ALPHA`; alpha `ONE / INV_SRC_ALPHA`. It is selected wherever `AlphaBlendState` is bound today (quads, text, rounded rects). With the current straight-alpha shaders, this produces correct premultiplied src-over into a premultiplied destination.
- **3D.** In overlay mode, the same `PremultipliedDestinationBlendState` (premultiplied-destination src-over) is bound at the start of each camera's 3D pass. The material's output alpha is written: opaque materials are opaque, and materials with alpha < 1 appear translucent on the overlay. No blend state can force a constant alpha (the fixed-function blend always scales the shader's or the destination's alpha), so the 3D pass does not try to. Its color math equals what normal mode's 3D pass already gets from the inherited `AlphaBlendState`; only the alpha accumulation differs. Normal mode keeps today's implicit inherited state, byte for byte.
- A small `Win32RenderAlphaMode` flag, set once at startup, selects these paths. All the choices live in the host render bridge.

## 5. Per-pixel click-through

- A new `DirectX11HitTestSampler` class (overlay mode only) uses a 1×1 staging texture ring of 2 or 3 entries.
  - After Draw and before Present, if the cursor (`GetCursorPos` → client) is inside the client area, it copies that pixel with `CopySubresourceRegion` into the next ring slot.
  - It maps the oldest completed slot with `D3D11_MAP_FLAG_DO_NOT_WAIT`. It never stalls, and results arrive with 1–2 frames of latency.
  - The sampled alpha decides the state: alpha < 8/255 means click-through on, otherwise off.
  - A new `Win32ClickThroughController` toggles `WS_EX_TRANSPARENT` with `SetWindowLongPtr` + `SetWindowPos(SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)`, and only when the state changes.
  - With the cursor outside the window, the state stays unchanged.
- **Interaction with idle throttle.** While click-through is on, the window receives no mouse messages, so the activity tracker cannot see the cursor moving over transparent areas.
  - In overlay mode with idle throttle on, the idle loop also samples `GetCursorPos` each iteration. A cursor position change counts as activity **only if the cursor is inside the overlay's bounds**.
  - The pacer and tracker contracts stay the same; this is a new input into `RunIdleThrottledLoop` when overlay mode is on.
- **Test hook:** `--hit-test-probe x,y`, valid only with `--frames`, samples that fixed client pixel instead of the cursor. It logs `HIT_TEST x=<x> y=<y> alpha=<a> clickThrough=<on|off>` after the last frame, and it does not toggle styles. This proves the readback path deterministically, without moving the user's mouse.

## 6. Diagnostics

- `HOST_FINGERPRINT` (in `--frames` mode) gains `windowMode=<normal|overlay>`.
- The existing `alpha`, `style` and `exStyle` fields already capture the composition and style differences.

## 7. Regression safety

1. **Default unchanged.** `-Verify` passes. Normal scenes' goldens and fingerprints must be byte-identical after a deliberate re-record: the fingerprint gains `windowMode=normal`, and the goldens must not change.
2. **New `overlay` scenario** on the smoke scene with `--window-mode overlay --overlay-bounds profile --overlay-background transparent --frames 30 --fixed-delta 0.016666 --capture … --hit-test-probe <p>`:
   - The capture is read **with alpha**. A new tool mode keeps alpha instead of forcing it to 255.
   - Invariants: every pixel has B, G, R ≤ A (valid premultiplied); at least 1% of pixels have A = 0 (the transparent background exists); at least 1% have A = 255.
   - The capture is compared against an overlay golden (a PNG with alpha), using a 4-channel comparison with the existing tolerance.
   - The fingerprint shows `windowMode=overlay`, `alpha=1`, and an `exStyle` containing `NOREDIRECTIONBITMAP`, `TOPMOST`, `TOOLWINDOW` and `LAYERED`.
   - The `HIT_TEST` line for a probe point on a transparent pixel reports `clickThrough=on`. The probe points (one transparent, one opaque) are chosen from the recorded golden at Record time and stored in the manifest. Verify runs one probe per run, so the scenario runs twice, once for each probe.
3. **Manual proofs** in acceptance (reported, not automated):
   - The overlay is visible over other windows with a transparent background.
   - Clicks on transparent areas reach the window behind.
   - Clicks on opaque content activate the overlay.
   - A no-argument boot is unchanged.
4. **Unit and source tests:**
   - settings parsing and validation;
   - the normal-mode text of the window style, swap chain and blend path is unchanged;
   - the overlay paths are selected only in overlay mode;
   - hit-test sampler: ring logic, the non-blocking map flag, the threshold;
   - tool: alpha-preserving read, the premultiplied invariant check, the 4-channel comparison.

## 8. Known issue recorded for Helena (not fixed here)

In normal mode, 3D draws inherit the 2D `AlphaBlendState` after the first 2D draw (win32_render_bridge.cpp: `OMSetBlendState` only at the 2D sites; `ClearState` only on asset flush). The player has always rendered this way and the goldens encode it. Fixing it is a separate, deliberate change with a re-record.

## Revision 1 (spike results)

Date: 2026-09-25. Plan task 1, throwaway spike (standalone C++ built with `cl`, source and logs outside the repo in `C:\dev\helworks\builds\helengine-windows\spikes\overlay\`). Machine: Windows 11 Pro 10.0.26200, 3840×2160 primary monitor, per-monitor-DPI-aware process.

**Setup.** A 400×300 `WS_POPUP` overlay near the screen center with `WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOPMOST | WS_EX_TOOLWINDOW` (+ `WS_EX_LAYERED` where noted). D3D11 device, `CreateSwapChainForComposition` (B8G8R8A8, 2 buffers, `STRETCH`, `FLIP_DISCARD`, `ALPHA_MODE_PREMULTIPLIED`), `DCompositionCreateDevice` → `CreateTargetForHwnd(hwnd, TRUE)` → visual → `SetContent` → `SetRoot` → `Commit`. Cleared to (0,0,0,0) with an opaque red (1,0,0,1) center rect (a `ClearView` sub-rect), `Present(1, 0)`. Shown with `SW_SHOWNOACTIVATE` + `SetWindowPos(HWND_TOPMOST, …, SWP_NOACTIVATE)`. A solid green `WS_POPUP` "behind" window, 50 px larger on each side, sits under the overlay. `WS_EX_TRANSPARENT` was toggled with `SetWindowLongPtr` + `SetWindowPos(SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)`, holding each state 3 s, in the order off → on → off.

**Checks (objective, no screenshots):**
- `WindowFromPoint` at a transparent corner (+15,+15), at the opaque center, and at a point outside the overlay over the behind window.
- A 1-pixel `BitBlt(… SRCCOPY | CAPTUREBLT)` read from the desktop DC at the same points, which shows the composed desktop color.
- `DwmGetWindowAttribute(DWMWA_CLOAKED)`, `IsWindowVisible`, the HRESULTs of every D3D/DXGI/DComp call and `Present`.
- `GetForegroundWindow` before and after showing.

Run A used a non-topmost behind window; the owner's other windows sometimes covered it, which changed only the "behind" readings, not the conclusions. Run B made the behind window topmost (still below the overlay, which was created later) and is fully deterministic. The table is from run B; run A agreed on every overlay reading.

| LAYERED | SetLayeredWindowAttributes(0,255,LWA_ALPHA) | TRANSPARENT | WFP transparent corner | WFP opaque center | Pixel corner | Pixel center |
|---|---|---|---|---|---|---|
| off | off | off | overlay | overlay | green (behind) | red (overlay) |
| off | off | **on** | **overlay** | **overlay** | green | red |
| off | on (fails: `ERROR_INVALID_PARAMETER`, 87) | off | overlay | overlay | green | red |
| off | on (fails, 87) | **on** | **overlay** | **overlay** | green | red |
| on | off | off | overlay | overlay | green | red |
| on | off | **on** | **behind** | **behind** | green | red |
| on | on (succeeds) | off | overlay | overlay | green | red |
| on | on (succeeds) | **on** | **behind** | **behind** | green | red |

In every configuration, every call returned `S_OK`, `Present` succeeded, the overlay was visible and not cloaked (`DWMWA_CLOAKED` = 0), and toggling `WS_EX_TRANSPARENT` back off restored the "off" readings exactly.

**Answers:**
- (a) DirectComposition content renders with and without `LAYERED`. The composed desktop shows the overlay's opaque red at the center and the window behind through the (0,0,0,0) pixels.
- (b) With `LAYERED`, `WS_EX_TRANSPARENT` on makes `WindowFromPoint` return the window behind at every point, including over opaque pixels. With it off, the overlay is returned at every point, **including over fully transparent pixels**. DWM does not hit-test by alpha on its own, so the §5 sampler and toggle are required.
- (c) `WS_EX_LAYERED` **is required**. Without it, `WS_EX_TRANSPARENT` has no effect on `WindowFromPoint`.
- (d) `LAYERED` without `SetLayeredWindowAttributes` does **not** hide DirectComposition content. `SetLayeredWindowAttributes` is not needed and changes nothing measurable. It fails on a non-layered window.
- `SW_SHOWNOACTIVATE` + `SWP_NOACTIVATE` left `GetForegroundWindow` unchanged in all eight runs, and the overlay never became the foreground window.

**Working style set (unchanged from §2):** `WS_POPUP`; `WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED`, **without** calling `SetLayeredWindowAttributes`. Shown with `SW_SHOWNOACTIVATE`, then `SetWindowPos(HWND_TOPMOST, …, SWP_NOACTIVATE)`. The click-through toggle is `WS_EX_TRANSPARENT` via `SetWindowLongPtr` + `SetWindowPos(SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)`. The plan's Global Constraints style line needs no change.

**D3D11 device note.** The player's D3D11 device is created without `D3D11_CREATE_DEVICE_BGRA_SUPPORT`; that flag is required if DirectComposition surfaces (`IDCompositionSurface`) or Direct2D interop are added later. The overlay uses a composition swap chain, which does not need it, so the device creation (shared with normal mode) is deliberately unchanged.

**Scope note.** `WindowFromPoint` is the hit-test the spike could check without moving the user's mouse. Real mouse-click routing is left to the §7 manual proofs.
