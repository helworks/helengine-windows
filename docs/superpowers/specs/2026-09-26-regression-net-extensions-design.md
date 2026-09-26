# Regression Net Extensions (subproject 3a) — Design

Date: 2026-09-26
Status: Draft for review (Helena)
Series: subproject 3 = 3a net extensions → 3b opt-in per-monitor DPI → 3c multiple windows in one process (core + host) → 3d desktop-background role. Helena chose one process with multiple windows (core changes). The golden rule applies to both the player and the editor.

## Goal

Before any subproject-3 feature work, make `scripts/run-regression.ps1` able to see what subprojects 3b–3d will change. It must also prove that the player can be built from a helengine worktree, because 3c will change helengine core on a branch in a worktree and never in Helena's main checkout.

## Non-goals

- No player feature changes, apart from diagnostics that only run in `--frames` / probe mode.
- No helengine source changes. The worktree proof only adds a git worktree and initializes its submodules.

## 1. Probe runs exercise the real click-through toggle

- Today, probe runs skip `Win32ClickThroughController::Apply`. From now on, in probe mode the hit-test controller calls `Apply(alpha < 8)` exactly as the cursor path does.
- It then reads the resulting `GWL_EXSTYLE` and logs it:
  `HIT_TEST x=<x> y=<y> alpha=<a> clickThrough=<on|off> exStyle=0x<hex>`.
- Expectations:
  - the transparent probe gives `exStyle` with the `WS_EX_TRANSPARENT` bit (0x20) set;
  - the opaque probe gives the bit cleared. This proves `Apply` actually toggles, starting from the click-through-at-creation state.
- Because `exStyle` now depends on the probe, the overlay scenario records **one fingerprint per probe** (transparent and opaque) in the manifest. Verify compares each probe's fingerprint with its own recorded one.
- `HIT_TEST` parsing in the script becomes an exact match, including the `exStyle` value.

## 2. New fingerprint fields

`HOST_FINGERPRINT` (`--frames` mode only) gains these fields, placed after `windowMode` and before `elapsedMs`:
- `window=<name>`: a window tag. It is always `main` today. It prepares for per-window fingerprints in 3c.
- `dpi=<n>`: `GetDpiForWindow(hwnd)`.
- `dpiAwareness=<unaware|system|permonitor|permonitorv2|unknown>`: derived from `GetAwarenessFromDpiAwarenessContext(GetWindowDpiAwarenessContext(hwnd))`, with v2 detected through `AreDpiAwarenessContextsEqual` against `DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2`.
- `windowRect=<l>,<t>,<r>,<b>` from `GetWindowRect` in overlay mode. In normal mode the value is `windowRect=default`, because normal windows are placed with `CW_USEDEFAULT` and their position is not deterministic. `client=` already captures the normal-mode size.

The tool's `HostFingerprint` gains these compared (non-counter) fields, and an old line without them fails `Parse`. Every manifest entry is therefore re-recorded once, and the goldens must be byte-identical.

## 3. Multiple fingerprint lines

- The script collects **every** `HOST_FINGERPRINT` line in the startup log, keyed by `window`. Today there is exactly one, `main`.
- The manifest stores fingerprints as a map from window tag to fields: `fingerprints: { main: {…} }`, instead of a single `fingerprint` object.
- Verify fails on a missing window, an extra window, or any field mismatch per window.
- The migration happens in the same re-record. After it, the script reads only the new shape, and an old manifest shape is a FAIL with "re-record required".

## 4. Overlay + idle scenario

- A new manifest kind, `overlayIdle`, runs the smoke scene with the overlay arguments (profile bounds, transparent background) plus `--idle-throttle on --idle-after-ms 1 --idle-fps 10 --frames 30 --fixed-delta 0.016666 --capture … --hit-test-probe <transparent probe>`.
- Checks:
  - `check-idle` with the recorded minimums (the idle scenario's 25 frames and 2400 ms);
  - `check-premultiplied 0.01 0.01`;
  - `compare-rgba` against the existing overlay golden;
  - the `HIT_TEST` line for the transparent probe: `clickThrough=on` with the 0x20 bit set.
- The live cursor path (idle wake on cursor motion over a click-through window) cannot be automated without moving the mouse. It stays a documented blind spot.

## 5. Build-from-helengine-worktree proof

- Create a detached helengine worktree at `C:\dev\helworks\helengine\.worktrees\regression-reference`, at the commit Helena's main checkout is on. This adds git metadata only; her main checkout and its uncommitted files are never touched.
- Initialize its submodules there with `git submodule update --init engine/vendor/csharpcodegen engine/vendor/bepuphysics2`, using the pinned commits and her existing SSH keys.
- Prove it with `run-regression.ps1 -BuildOnly -HelengineRoot <worktree>`, which must print `PLAYER=` with exit 0. Then run one `--frames` scene run with the resulting player, whose capture must equal the golden.
- Record the exact procedure in `regression/README.md` ("Building against a helengine worktree"), for use in 3c.
- If the submodule init fails (network, SSH), the task reports BLOCKED with the error. It never works around it by copying files.

## 6. Verification

- **Unit and source tests:** the new tool fields and multi-line parsing, per-probe fingerprint comparison, `overlayIdle` checks, the `HIT_TEST` exStyle parsing, and the player's probe-mode `Apply` call and exStyle log.
- **Acceptance:**
  - `-Record` (deliberate, because the schema changes), after which the goldens must be byte-identical;
  - `-Verify` passes twice;
  - a negative check: temporarily force the transparent probe's expected clickThrough to `off` in the manifest, Verify must FAIL, then restore it;
  - a no-argument boot must be unchanged;
  - the worktree build proof.
