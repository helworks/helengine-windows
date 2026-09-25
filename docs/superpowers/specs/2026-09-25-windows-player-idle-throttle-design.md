# Windows Player Idle Throttle ("render on demand", host-only) — Design

Date: 2026-09-25
Status: Draft for review (Helena)
Series: subproject 1 of 4 (0 safety net ✅ → **1 idle throttle** → 2 transparent DirectComposition windows → 3 per-monitor DPI + multi-window).
Golden rule: additive and opt-in. With the feature off (the default), player behavior is identical to today's, and `scripts/run-regression.ps1 -Verify` proves it.

## Goal

A desktop-shell UI that sits idle must not burn CPU and GPU at vsync rate. When the player is opted in and nothing is happening, it drops to a low frame rate and truly sleeps between frames. Input or window activity brings it back to full rate immediately.

## Decisions (with Helena)

- **Approach A:** adaptive frame rate in the **host only**. There are no changes to helengine core, the editor or codegen.
- A true core-level invalidation API (`Core.RequestRedraw`/`KeepAwake`) is out of scope. It is a possible future subproject if Gevo needs zero-GPU idle.

## Non-goals

- Skipping `Update` while presenting. Every loop iteration still runs `Update → Draw → Present`, so animations, physics, audio, input polling and scene commits keep working, just less often while idle.
- Changing the vsync interval, tearing or flip model.
- Detecting "nothing visually changed". The core has no signal for it, and that is the future subproject mentioned above.

## 1. Configuration (opt-in)

Idle throttling is **off** unless it is enabled in one of these places:

- **Runtime profile** (`<exe dir>\profile.json`): optional fields
  - `idleThrottleEnabled` (bool, default false)
  - `idleAfterMilliseconds` (int > 0, default 500)
  - `idleFramesPerSecond` (int, 1..30, default 10)

  If a field is absent, its default applies. If a field is present with an invalid value, the player logs the error and exits with code 2; it never silently defaults. The profile loader's writer must **preserve** these fields when it re-seeds or rewrites `profile.json`.
- **Command line** (for tests and overrides), in the same strict family as the regression flags:
  - `--idle-throttle on|off`
  - `--idle-after-ms <n>`
  - `--idle-fps <n>`

  The command line overrides the profile. These are "known flags", so their presence enables strict validation (see the safety-net spec §1, Revision 2).

## 2. Activity tracking

- A new `Win32ActivityTracker` class records the time of the last activity (`std::chrono::steady_clock`).
- `Win32Window`'s WndProc calls the tracker **only when one is attached**; with the feature off, none is attached. It is called for:
  - keyboard: `WM_KEYDOWN`, `WM_KEYUP`, `WM_SYSKEYDOWN`, `WM_SYSKEYUP`, `WM_CHAR`;
  - mouse: `WM_MOUSEMOVE`, `WM_LBUTTON*`, `WM_RBUTTON*`, `WM_MBUTTON*`, `WM_XBUTTON*`, `WM_MOUSEWHEEL`, `WM_MOUSEHWHEEL`;
  - window: `WM_SIZE`, `WM_ACTIVATE`, `WM_SETFOCUS`, `WM_KILLFOCUS`, `WM_PAINT`, `WM_DISPLAYCHANGE`, `WM_DPICHANGED`.

  Every message is still passed on exactly as today (`DefWindowProc` and the existing handling).
- **Engine keep-awake conditions** read existing public core state; the core is not changed. While either holds, the player stays at full rate:
  1. a scene load, unload or transition is pending, because `Draw()` commits it;
  2. physics will step this update (`Core.PredictedPhysicsStepSeconds > 0`).

  The implementation plan confirms the exact getters exist in the transpiled core. If one does not, that condition is dropped and documented; the core is not changed to add it.

## 3. The loop

- **Off (default):** `while (PumpMessages()) { RenderFrame(); }`. The code is unchanged.
- **On:** a new `Win32IdleFramePacer` decides, before each iteration, whether the player is **active** or **idle**. It is active if the time since the last activity is below `idleAfterMilliseconds`, or if any keep-awake condition holds.
  - **Active:** run `RenderFrame()` immediately, exactly as today (vsync pacing).
  - **Idle:** wait until the next idle frame time (`1000 / idleFramesPerSecond` ms after the previous frame started), or until a message arrives, using `MsgWaitForMultipleObjectsEx(0, nullptr, remainingMs, QS_ALLINPUT, MWMO_INPUTAVAILABLE)`. Then pump messages and run `RenderFrame()`.
  - A message that wakes the player counts as activity, which returns it to active mode within the same iteration.
- The minimized and zero-size early return inside `RenderFrame` stays as it is. The pacer's timed wait also keeps a minimized player from spinning.
- `--frames N` still counts only presented frames. With idle throttling on and no input, a `--frames` run takes about N / idleFps seconds.

## 4. Diagnostics

- **Fingerprint.** In `--frames` mode, the existing `HOST_FINGERPRINT` line gains:
  - `idleThrottle=<on|off>`
  - `idleFrames=<count of iterations run in idle mode>`
  - `activeFrames=<count>`
- **Startup log.** When the feature is on, it logs one line with the effective configuration and its source (profile or command line).
- **Tracy.** Reuse the existing `"Frame.PacingAndIdle"` zone name around the wait.

## 5. Regression safety

1. **Default path unchanged.** `run-regression.ps1 -Verify` with the existing goldens and fingerprints (idle throttling off) must pass unchanged. The fingerprint's recorded fields gain `idleThrottle=off`, `idleFrames=0` and `activeFrames=30`. Re-record deliberately once, and show the goldens are byte-identical.
2. **New idle scenario** in `run-regression.ps1`: one scene (the smoke scene) runs with `--idle-throttle on --idle-after-ms 1 --idle-fps 10 --frames 10 --fixed-delta 0.016666 --capture ...`.
   - The capture must equal that scene's golden.
   - The fingerprint must show `idleFrames` at least 9.
   - `elapsedMs` must be at least 800. That is 10 frames at 10 fps minus tolerance, which proves the throttle actually slept.
   - The recorded idle-scenario fingerprint is part of the manifest.
3. **Unit and source tests.**
   - Pacer decision logic is written as a pure class that takes the clock as input. Tests cover: active vs idle thresholds, the wait computation, and wake on activity.
   - Profile parsing: absent fields, valid values, invalid values leading to exit 2, and fields preserved by the writer.
   - Source tests check that WndProc calls the tracker only when one is attached, and that the default loop text is unchanged.

## 6. Out of scope

- Throttling Update separately from Draw.
- The core invalidation API.
- Per-window pacing (subproject 3).
- Power-state awareness (battery).

## Revision 1 (2026-09-25): facts found while planning

- **Invalid idle fields.** `RuntimePlayerProfileLoader::LoadOrCreateProfile` catches any read or validation exception and **rewrites `profile.json` with defaults**, which is a silent repair. Invalid idle-throttle fields must not go through that path. The loader throws a dedicated `RuntimePlayerProfileConfigurationError` for them, outside the repair catch, and `Win32Application` turns it into `Win32ExitRequest(2, ...)`.
- **Preserving fields when the file is rewritten.** The writer only runs on seed (file missing) and on an invalid-resolution repair.
  - On a repair, any idle fields that were present and valid are written back.
  - Seeded files stay byte-identical to today's, because idle fields are emitted only when they were present.
- **Keep-awake getters** (generated core):
  - physics: `EngineCore->get_PredictedPhysicsStepSeconds() > 0`, using the previous Update's prediction;
  - scene: `get_SceneManager()` non-null and (`get_IsSceneTransitionActive()` or `get_LastTracePendingOperationCount() > 0`). This is a trace-snapshot proxy for pending operations, because the operation list is private.
  - Scenes with active physics therefore never go idle. That is expected, and it is documented.
  - `get_LastTracePendingOperationCount()` is a trace snapshot: it is updated only when `SceneManager` records trace state, so it can briefly be stale. That is acceptable for keep-awake (at worst a frame or two stays active, or goes idle, one frame late). Revisit it if helengine exposes a live pending-operation count.
- **Idle regression scenario.** Goldens are frame-30 captures, so the idle scenario runs the smoke scene with `--idle-throttle on --idle-after-ms 1 --idle-fps 10 --frames 30 --fixed-delta 0.016666 --capture ...` (about 3 s). Assertions:
  - the capture equals the golden;
  - `idleFrames` is at least 25, because the first frames are active while the startup scene load is pending;
  - `elapsedMs` is at least 2400.

  `--idle-after-ms` is 1, not 0, because the flag requires a value of at least 1. It uses a threshold check, not exact equality, for `idleFrames`, `activeFrames` and `elapsedMs`, because the split between active and idle frames depends on load timing. The smoke scene must have no physics; the plan verifies this for `axis_test`.

  This replaces §5 item 2's "recorded idle-scenario fingerprint": the idle run's fingerprint is never compared field by field with a golden fingerprint, because its idle and active counts and its timing differ by design. The manifest records only an idle entry (`id`, `kind='idle'`, `minIdleFrames=25`, `minElapsedMs=2400`); verify fails with `idle entry missing from manifest` when an older manifest has none.
- **No native unit-test harness exists.** The pacer decision is a pure, Win32-free class (`Decide(nowMs, lastActivityMs, lastFrameStartMs, keepAwake)` returning a small struct; no tuples). It is covered by source tests plus the behavioral idle scenario.

## Revision 2 (2026-09-25): whole-branch review

- **Looping audio keeps the player awake (third keep-awake condition).** The Windows audio backend restarts a looping voice from `Update()`, after the wave-out callback marks its buffer done. With the throttle idle, that restart could wait up to one idle interval, which is heard as a gap at the end of every loop. `Win32AudioBackend::HasActiveLoopingVoice()` (read-only, under the backend's voice mutex) is true while the backend owns a looping voice that is not paused, including a looping voice whose buffer completed and waits for its restart. `Win32Application::IsEngineKeepAwake()` returns true when the audio backend exists and reports one. Keep-awake is consulted only by the idle loop, so the default path is unaffected.
  - Consequence: the player stays at full rate while looping audio plays, so ambient music or looping ambience removes the idle savings for as long as it plays. One-shot sounds do not keep the player awake.
  - Finer alternative for later: the wave-out callback posts a thread message that wakes the idle wait, and the loop services the restart at idle rate without rendering a full-rate frame.
- **Which wakes count as activity (amends §3).** §3 said a message that wakes the player counts as activity. The implementation is narrower: only the messages on the tracker's list (§2) count as activity. When a wake is caused by any other message, the loop pumps it and waits again for the rest of the idle interval instead of rendering early, unless the wait timed out (a timed-out wait always renders; any remainder is only timer granularity). This is deliberate: `WM_TIMER`, posted thread messages and similar traffic must not keep the player awake forever.
- **Untracked messages.** `WM_MOUSELEAVE`, `WM_NCMOUSEMOVE`, `WM_IME_*`, `WM_POINTER*`, `WM_MOVE` and `WM_ACTIVATEAPP` are not tracked. This is acceptable: pointer and touch input are promoted to mouse messages, which are tracked; the others cost at most one idle interval of latency before the next idle frame shows their effect. The tracker is attached only to the main window, which subproject 3 (per-window pacing) must revisit.
- **Idle minimums come from the manifest.** Verify passes the idle entry's `minIdleFrames` and `minElapsedMs` to `check-idle` instead of the script's constants; Record keeps writing the constants into the entry. An entry without either field fails with a re-record request.
