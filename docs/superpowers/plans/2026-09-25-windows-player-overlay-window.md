# Windows Player Overlay Window Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an opt-in overlay window mode: a borderless, topmost, DirectComposition window with per-pixel premultiplied alpha and per-pixel click-through. With `windowMode` at `normal` (the default), the player is identical to today.

**Architecture:** Host-only (helengine-windows).
- `Win32WindowModeSettings` is resolved from the profile and the command line before the window exists.
- `Win32Window` takes an explicit `Win32WindowStyle`.
- `DirectX11Bootstrap` gains a composition path (DComp device, target and visual, premultiplied swap chain).
- The render bridge selects premultiplied clear and blend states in overlay mode.
- `DirectX11HitTestSampler` plus `Win32ClickThroughController` toggle `WS_EX_TRANSPARENT` from a non-blocking 1×1 readback.
- The regression net gains an alpha-aware overlay scenario.

**Tech Stack:** C++20, Win32, D3D11, DXGI 1.2 and DirectComposition (`dcomp.lib`); the C# regression tool; PowerShell; builder.tests source tests.

**Spec:** `docs/superpowers/specs/2026-09-25-windows-player-overlay-window-design.md`

## Global Constraints

- **Where to work.** Only in this repo, in worktree `C:\dev\helworks\helengine-windows\.worktrees\overlay-window` on branch `feature/overlay-window`. Never modify `C:\dev\helworks\helengine`, `C:\dev\helprojs\demodisc` or the main helengine-windows checkout (it holds the owner's uncommitted work).
- **Normal mode stays identical.** With no overlay configuration, all of these keep today's exact code text and behavior:
  - `WS_OVERLAPPEDWINDOW`, ex-style 0, `AdjustWindowRect` and `Show()`;
  - `CreateSwapChainForHwnd` with `ALPHA_MODE_IGNORE`;
  - `AlphaBlendState` and the implicitly inherited 3D blend state;
  - the clear colors.
- **AGENTS.md rules:**
  - one class per file;
  - `///` docs on every member, repeated on the .cpp definitions;
  - PascalCase fields;
  - no tuples;
  - no local helper functions;
  - throw instead of defaulting;
  - byte-cap command output.
- **Build and launch.**
  - Build only with `scripts\run-regression.ps1 -BuildOnly`.
  - Launch only through `scripts\launch_in_emulator.ps1`, called in-process with `&`.
  - builder and builder.tests need `-p:HelEngineRoot=C:\dev\helworks\helengine`.
  - Run dotnet from inside the worktree.
  - Exception: Task 1's throwaway spike, which is not a platform build and whose files live outside the repo.
- **Values:**
  - `windowMode`: normal | overlay
  - `overlayBounds`: monitor | profile (default monitor)
  - `overlayBackground`: camera | transparent (default camera)
  - click-through threshold: alpha < 8
  - invalid configuration exits with code 2
- **Overlay styles:** `WS_POPUP` with `WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED`. Task 1's spike can amend this list.
- **Overlay swap chain:** B8G8R8A8, 2 buffers, `DXGI_SCALING_STRETCH`, `FLIP_DISCARD`, `DXGI_ALPHA_MODE_PREMULTIPLIED`, `CreateSwapChainForComposition`.
- **Overlay regression scenario:** smoke scene with `--window-mode overlay --overlay-bounds profile --overlay-background transparent --frames 30 --fixed-delta 0.016666 --capture … --hit-test-probe x,y`.

## Review Focus

- **Normal mode must be byte-identical.** Every task adds a source test that pins the normal-mode text. Task 8 re-records with the goldens byte-identical.
- **A spike failure invalidates the design.** If DComp + NOREDIRECTIONBITMAP + LAYERED does not show content or pass clicks, Task 1 records the working combination and the spec is amended before Tasks 3–5 proceed.
- **The hit-test readback must never stall the GPU:** it uses `D3D11_MAP_FLAG_DO_NOT_WAIT` and a ring (Task 5 source test).
- **Click-through with idle throttle:** cursor movement over transparent areas must still wake the idle loop (Task 5).
- **The overlay capture must be valid premultiplied alpha** (B, G, R ≤ A), and not the "alpha holes" state (Task 7 invariants).

---

### Task 1: Spike: prove DirectComposition, window styles and click-through (throwaway)

**Output:** knowledge, not product code.
- The spike folder is `C:\dev\helworks\builds\helengine-windows\spikes\overlay\`.
- Build a minimal standalone C++ program with `cl` through `VsDevCmd.bat` (not a platform build).

**The program:**
1. Creates a `WS_POPUP` window with the ex-styles listed above, at 400×300 near the screen center.
2. Creates a D3D11 device and a composition swap chain (premultiplied), then the DComp device, target, visual and commit.
3. Clears to (0,0,0,0), draws an opaque premultiplied rectangle in the middle by clearing a sub-rect via a scissor or a second RTV clear, then presents.
4. Toggles `WS_EX_TRANSPARENT` every 3 seconds with `SetWindowPos(SWP_FRAMECHANGED | …)` and logs the state.
5. Records into a log file the results of `WindowFromPoint` at the transparent corner and at the opaque center, in both states. This is an objective check of the hit-test path that does not need a person.

**Determine:**
- (a) Does the window content render: `Present` succeeds and a `PrintWindow`-independent check such as `DwmGetWindowAttribute` or `WindowFromPoint` behaves?
- (b) Does `WindowFromPoint` return the window behind when `WS_EX_TRANSPARENT` is on, and the overlay when it is off?
- (c) Is `WS_EX_LAYERED` required for (b)? Test with and without it.
- (d) Does `LAYERED` without `SetLayeredWindowAttributes` hide DComp content? Test with and without `SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA)`.

- [ ] **Step 1:** Write the spike source (outside the repo) and build it.
- [ ] **Step 2:** Run the four combinations (LAYERED on/off × SetLayeredWindowAttributes on/off) and record the `WindowFromPoint` results.
- [ ] **Step 3:** Append a "Revision 1 (spike results)" section to the spec. It names the exact working style set and whether `SetLayeredWindowAttributes` is needed, and it updates §2 and this plan's Global Constraints line if anything differs.
- [ ] **Step 4:** Commit only the spec change: `docs(spec): record the overlay window spike results`.

---

### Task 2: Window-mode settings (profile and command line)

**Files:**
- Modify: `runtime_player_profile.hpp/.cpp` and `runtime_player_profile_loader.hpp/.cpp`, adding `windowMode`, `overlayBounds` and `overlayBackground` as optional string fields. Follow the idle fields exactly: absent means the default; present-but-invalid raises `RuntimePlayerProfileConfigurationError`; the fields are emitted on repair only when present.
- Modify: `win32_command_line_options.hpp/.cpp`, adding the known flags `--window-mode`, `--overlay-bounds`, `--overlay-background` and `--hit-test-probe`.
  - The `x,y` probe value parses as two non-negative ints.
  - `--hit-test-probe` requires `--frames` and `--window-mode overlay`, or overlay mode from the profile. That combination is checked when settings resolve.
- Create: `src/platform/windows/win32/win32_window_mode_settings.hpp/.cpp` (`Win32WindowModeSettings`):
  - enums as small `enum class` types, each in its own header per the one-type-per-file rule;
  - `Resolve(profile, options)`;
  - getters;
  - `Describe()`, which returns `windowMode=overlay bounds=<…> background=<…> source=<…>`.
- Modify: `CMakeLists.txt`.
- Tests: extend `RuntimePlayerProfileSourceTests` and `Win32CommandLineOptionsSourceTests`, and add `Win32WindowModeSettingsSourceTests`.

- [ ] **TDD:** write the source tests first, run them and see them fail, implement, and see them pass. Then run the full builder.tests.
- [ ] **Build and prove:**
  1. Run `-BuildOnly`.
  2. `--window-mode sideways --frames 1` must exit with code 2.
  3. A profile containing `"windowMode":"sideways"` must exit with code 2 and the file must not be rewritten. Restore `profile.json` afterward.
  4. A run with no arguments must be unchanged.
- [ ] **Commit:** `feat(player): resolve opt-in overlay window-mode settings`.

---

### Task 3: Overlay window and composition swap chain

**Files:**
- Create: `win32_window_style.hpp/.cpp` (`Win32WindowStyle`: `Style`, `ExStyle`, `ShowCommand`, plus `static Win32WindowStyle Normal()` and `static Win32WindowStyle Overlay()`).
- Modify: `win32_window.hpp/.cpp`.
  - The ctor takes a `Win32WindowStyle`.
  - Normal mode uses the exact current calls. Use `AdjustWindowRectEx` only in overlay mode, and keep the `AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE)` text for normal mode.
  - Overlay `Show` uses `SW_SHOWNOACTIVATE` plus `SetWindowPos(HWND_TOPMOST, bounds…, SWP_NOACTIVATE)`.
- Modify: `win32_application.cpp` `CreateMainWindow`.
  - Resolve `Win32WindowModeSettings` before constructing the window.
  - Compute the overlay bounds: for `monitor`, use `MonitorFromPoint({0,0}, MONITOR_DEFAULTTOPRIMARY)` + `GetMonitorInfo`; for `profile`, use the primary monitor's top-left with the profile size.
  - Log `Describe()` once when overlay mode is on.
- Modify: `directx11_bootstrap.hpp/.cpp`.
  - Take a composition flag.
  - In composition mode, run `CreateSwapChainForComposition` and the DComp setup (device, target with topmost TRUE, visual, SetContent, SetRoot, Commit).
  - Add ComPtr members, declared so they are released before the swap chain and device.
  - Leave the normal path untouched.
- Modify: `directx11_host_fingerprint.*`, adding `windowMode=<normal|overlay>` after `activeFrames`. Update `tools/regression/HostFingerprint.cs` and its tests. The normal fingerprint gets `windowMode=normal`.
- Modify: `CMakeLists.txt`, adding `dcomp` to `target_link_libraries`.
- Tests:
  - source tests pin the normal-mode `CreateWindowExW(0, …, WS_OVERLAPPEDWINDOW …)` and `CreateSwapChainForHwnd` text;
  - the composition calls exist only in the composition branch;
  - `dcomp` is linked;
  - the fingerprint field is present;
  - tool tests cover the new fingerprint field.

- [ ] **TDD, then run the full builder.tests and the tool tests.**
- [ ] **Build and prove:**
  1. Run `-BuildOnly`.
  2. `--window-mode overlay --overlay-bounds profile --overlay-background transparent --scene axis_test --frames 30 --fixed-delta 0.016666 --capture <bmp>` must give `EXIT_CODE=0`, and the fingerprint must show `windowMode=overlay alpha=1` with the overlay ex-style bits.
  3. Normal `--frames 5` must show `windowMode=normal alpha=3` and the old style values.
  4. A run with no arguments must be unchanged.
- [ ] **Commit:** `feat(player): opt-in DirectComposition overlay window`.

---

### Task 4: Premultiplied rendering in overlay mode

**Files:**
- Create: `win32_render_alpha_mode.hpp` (`enum class Win32RenderAlphaMode { Straight, Premultiplied }`).
- Modify: `win32_render_bridge.cpp/.hpp`. It gets a setter or a ctor argument carrying the alpha mode and the background mode, applied once at startup from `Win32WindowModeSettings`.
  - **Clear** (~2174-2176 and `ClearBackBuffer` at ~1426/1438/1475): in Premultiplied mode, premultiply the camera color, or use (0,0,0,0) when the background is `transparent`.
  - **2D:** add `PremultipliedDestinationBlendState` next to `AlphaBlendState` (~3400). Color is `SRC_ALPHA/INV_SRC_ALPHA`; alpha is `ONE/INV_SRC_ALPHA`. Select it at the bind sites (~3527, ~3555) only in Premultiplied mode.
  - **3D:** in Premultiplied mode only, bind `OverlayOpaqueBlendState` at the start of each camera's 3D pass (around `RenderCamera` ~2233). Color is written unchanged (blending off for color is not possible alone, so use `SrcBlend=ONE, DestBlend=ZERO`). Alpha is `SrcBlendAlpha=BLEND_FACTOR` with blend factor a=1 and `DestBlendAlpha=ZERO`. Straight mode stays byte-identical, including the inherited-state accident.
- Tests: the source tests assert that normal-mode bind sites still use `AlphaBlendState`, and that the new states are used only under the Premultiplied branch.

- [ ] **TDD, then run the full builder.tests.**
- [ ] **Build and prove:**
  1. The Task 3 overlay capture command, now read with alpha. Until Task 6 lands, write a one-off check (the tool's `BmpImageReader` forces alpha, so use a temporary unit test or a small PowerShell `System.Drawing` read in the report).
  2. That check must show pixels with alpha 0, pixels with alpha 255, and no pixel where any color channel is greater than alpha.
  3. A normal `--frames 30` capture of axis_test must equal the committed golden: `compare` → `PASS 0`.
- [ ] **Commit:** `feat(player): render premultiplied alpha in overlay mode`.

---

### Task 5: Per-pixel click-through

**Files:**
- Create: `directx11_hit_test_sampler.hpp/.cpp` (`DirectX11HitTestSampler`).
  - A ring of three 1×1 staging textures.
  - `void Capture(int x, int y)`: `CopySubresourceRegion` into the next slot, only when the pixel is inside the client area.
  - `bool TryReadLatestAlpha(int& alpha)`: maps the oldest pending slot with `D3D11_MAP_FLAG_DO_NOT_WAIT` and returns false on `DXGI_ERROR_WAS_STILL_DRAWING`.
- Create: `win32_click_through_controller.hpp/.cpp` (`Win32ClickThroughController`).
  - `void Apply(bool clickThrough)` toggles `WS_EX_TRANSPARENT` only when the state changes, using `SetWindowLongPtrW` + `SetWindowPos(SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)`.
  - `bool IsClickThrough() const`.
- Modify: `win32_application.cpp`. Both objects are constructed only in overlay mode.
  - In `RenderFrame`, between Draw and Present: take the cursor from `GetCursorPos` → `ScreenToClient`, or the probe point when `--hit-test-probe` is set.
    - Capture the pixel, then try to read the latest alpha.
    - When not probing: `Apply(alpha < 8)`.
    - When probing: never toggle, but remember the last alpha.
  - In the `--frames` block, log `HIT_TEST x=<x> y=<y> alpha=<a> clickThrough=<on|off>` when probing. If no readback completed, log `alpha=pending` and exit with code 3; never guess.
  - **Idle interaction:** in `RunIdleThrottledLoop`, when overlay mode is on, sample `GetCursorPos` each iteration. A change while the cursor is inside the window rect counts as activity, through `ActivityTracker->MarkActivity()`.
- Tests: source tests for the DO_NOT_WAIT flag, the ring of three, the threshold `< 8`, construction only in overlay mode, the probe-only log, and the idle cursor sampling only under overlay mode.

- [ ] **TDD, then run the full builder.tests.**
- [ ] **Build and prove:**
  1. Run `-BuildOnly`.
  2. Run the overlay capture command twice with `--hit-test-probe` at a transparent corner (for example `2,2`) and at an opaque point read from the Task 4 capture. Record both `HIT_TEST` lines: they must show `clickThrough=on` and `off`.
  3. Manual proof, recorded in the report without screenshots: launch the overlay without `--frames` and use `WindowFromPoint` from a tiny PowerShell P/Invoke at a transparent point. It must return a window other than the overlay after the state settles. Stop the player.
- [ ] **Commit:** `feat(player): per-pixel click-through for the overlay window`.

---

### Task 6: Alpha-aware tool support

**Files:** `tools/regression`: `BmpImageReader` (an alpha-preserving read option), `ImageComparer` (a 4-channel option), and a new `PremultipliedAlphaChecker`. The tests go in `tools/regression.tests`.

**New commands:**
- `compare-rgba <capture.bmp> <golden.png> <diff.png>`: the same output contract as `compare`, over 4 channels.
- `record-golden-rgba <capture.bmp> <golden.png>`: keeps alpha.
- `check-premultiplied <capture.bmp> <minTransparentFraction> <minOpaqueFraction>`: FAIL if any pixel has a color channel greater than alpha; otherwise PASS or FAIL on the fraction thresholds (`0.01 0.01`).
- `find-probes <golden.png>`: prints `TRANSPARENT x,y` and `OPAQUE x,y`, the first pixels in scan order with alpha 0 and alpha 255, each with a 2-pixel margin from the edges and from pixels of the other kind.

**Rules:** the existing commands stay unchanged. `PngImageStore` must round-trip alpha exactly when asked (a new method). Do not change the opaque-forcing default.

- [ ] **TDD** for every command and edge case, then run the full tool tests.
- [ ] **Commit:** `feat(regression): alpha-aware capture comparison and premultiplied checks`.

---

### Task 7: Overlay scenario in the regression net

**Files:** `scripts/run-regression.ps1`, `builder.tests/RegressionScriptSourceTests.cs`, `regression/README.md`.

**Record:** run the overlay scenario once.
- `record-golden-rgba` → `regression/golden/axis_test.overlay.png`.
- `find-probes` on it → store the result in the manifest entry `@{ id=<smoke>; kind='overlay'; transparentProbe='x,y'; opaqueProbe='x,y'; fingerprint=<fields> }`.
- Then run twice with each probe and require `clickThrough=on` and `clickThrough=off`.
- Everything goes through the existing staging and publish path.

**Verify:** run the overlay scenario with the transparent probe and then with the opaque probe.
- `check-premultiplied 0.01 0.01`.
- `compare-rgba` against the overlay golden.
- Compare the fingerprint fields exactly with the recorded ones.
- Require the `HIT_TEST` lines as recorded.
- A missing entry is a FAIL (re-record required).
- The idle and golden scenarios are unchanged.

**README:** document the scenario, the probes, and that the net cannot see what DWM composites on screen. Only the manual proofs cover that.

- [ ] Write the source tests first, run them and see them fail, implement, and see them pass.
- [ ] Prove the scenario manually on the built player, without running `-Record`.
- [ ] **Commit:** `feat(regression): verify the overlay window scenario`.

---

### Task 8: Acceptance

- [ ] **Step 1: `-Record`.** This is deliberate, because the fingerprint gained `windowMode`. `git diff --stat -- regression/golden/*.png` must show **no change to the existing golden PNGs**. Only `axis_test.overlay.png` is new. If an existing PNG changed, stop and report BLOCKED.
- [ ] **Step 2:** `-Verify` → PASS.
- [ ] **Step 3: negative check.** Locally change the overlay scenario's background to `camera`. The scene's camera clears opaque, so `check-premultiplied` must FAIL on the transparent fraction, or the RGBA compare must fail. Revert the change, then `-Verify` → PASS.
- [ ] **Step 4: manual proofs**, written into the report as objective checks without screenshots:
  1. Overlay launched without `--frames` over another window: `WindowFromPoint` at a transparent point returns the window behind, and at an opaque point returns the overlay.
  2. `GetWindowLongPtr` exStyle shows TOPMOST, TOOLWINDOW, NOREDIRECTIONBITMAP and (per the spike) LAYERED.
  3. The overlay did not steal the foreground at start: `GetForegroundWindow` is unchanged.
- [ ] **Step 5:** No-argument boot: the player is alive after 5 s, loads the default scene, and logs no overlay lines.
- [ ] **Step 6: commit.** Manifest, new golden, baselines and README: `test(regression): record the overlay scenario and verify normal goldens are unchanged`.
