# Windows player regression net

`scripts\run-regression.ps1` builds **this checkout's** Windows player against an isolated copy of one committed
DemoDisc version: `-Verify` uses the commit pinned in the manifest, and `-Record` uses HEAD or `-ProjectCommit`
(see "Project pinning"). It then checks that the player still renders what it rendered when the goldens were recorded, that
its host layer (swap chain, window, Present) is unchanged, and that the editor-side test suites have no new failures.

## What is checked

| Check | Scenes / suites | Passes when |
|---|---|---|
| `scene` | Every scene run | The player exits within 120 seconds. A hung player is killed and reported as `FAIL scene <id> timeout`. |
| `smoke` | The first rendering scene in build order (currently `axis_test`) | The player exits with code 0, writes a capture, and the capture is not blank. |
| `golden` | Every DemoDisc rendering scene in the committed windows scene package (`scenes/rendering/*.helen`), including the smoke scene | The capture matches `regression\golden\<scene>.png` within the thresholds below. |
| `fingerprint` | Every scene run, per window | The run logs the same windows as the record (no missing, extra or duplicate window), every window's `HOST_FINGERPRINT` line matches the recorded one field for field, `presentCount` is at least `frames` and `presentFailures` is 0 (see "Host fingerprint"). |
| `pacing` | Every scene run, per window | Never fails. `WARN pacing <scene>@<window> recorded=<x>ms actual=<y>ms` when the window's run took more than 3 times, or less than a third of, the recorded wall-clock time. |
| `suite` | `helengine.editor.tests`, `helengine.render.validation.tests` (both from `-HelengineRoot`), `helengine.windows.builder.tests` (this checkout) | The run neither errored nor aborted, executed at least 95% of the tests recorded in `<suite>.executed.txt` (any other difference is a `WARN`), and its set of failing tests adds no test that is missing from `regression\baselines\<suite>.failing.txt`. Known failures stay tolerated; fixed ones are only reported. Tests in `<suite>.flaky.txt` only produce a `WARN` (see "Known flaky tests"). |
| `idle` | The smoke scene, once more, with the idle throttle on (see "Idle-throttle scenario") | The manifest has an idle entry, `check-idle` passes (throttle on, no Present failures, at least 25 idle frames, at least 2400 ms elapsed) and the capture matches the smoke scene's golden. |
| `overlay` | The smoke scene, twice more, in a transparent overlay window with one hit-test probe per run (see "Overlay scenario") | The manifest has a complete overlay entry, and each run exits 0, logs exactly the expected `HIT_TEST` line (click-through state and the recorded `exStyle`, with `WS_EX_TRANSPARENT` set for the transparent probe and cleared for the opaque one), passes `check-premultiplied`, matches `regression\golden\<scene>.overlay.png` with `compare-rgba` and matches its own probe's recorded fingerprints window for window and field for field. |
| `overlayIdle` | The smoke scene, once more, in the overlay window with the idle throttle on and the transparent probe (see "Overlay + idle scenario") | The manifest has an overlayIdle entry, and the run exits 0, passes `check-idle` with the idle entry's minimums, passes `check-premultiplied`, matches the overlay golden with `compare-rgba` and logs `HIT_TEST ... clickThrough=on` with exactly the recorded transparent `exStyle`. |
| `manifest` | `regression\golden\manifest.json` | It exists and was recorded with the same run settings. |

Every scene runs in a 640x360 window with `--frames 30 --fixed-delta 0.016666 --capture <bmp>`, so the captured
frame does not depend on wall-clock time.

A test counts as failing when its TRX outcome is anything other than `Passed` or `NotExecuted` (so `Failed`,
`Error`, `Timeout`, `Aborted` and unknown outcomes all count).

## Thresholds

- A pixel **differs** when any of its B, G or R channels differs by more than 8 levels. Alpha is ignored because the
  swap chain uses `ALPHA_MODE_IGNORE`: the tool reads every capture as fully opaque, so goldens are opaque PNGs.
- A golden check **fails** when more than 0.1% of the pixels differ, or when the sizes differ. On failure, a diff
  image (differing pixels magenta, the rest greyscale) is written to `<WorkRoot>\diffs\<scene>.diff.png`.
- A frame is **blank** when one color covers at least 99.9% of its pixels.

Goldens are machine-local: they are only valid on the machine, GPU and driver that recorded them, and other GPUs or
drivers are not expected to match.

## Host fingerprint

With `--frames`, and only then, the player writes one line per window to `helengine_windows.startup.log` after the
last frame is presented and before it quits (today there is one window, `main`):

```
HOST_FINGERPRINT format=87 alpha=3 swapEffect=4 buffers=2 scaling=0 style=0x14CF0000 exStyle=0x00000100 client=640x360 presentCount=30 presentFailures=0 frames=30 idleThrottle=off idleFrames=0 activeFrames=30 windowMode=normal window=main dpi=96 dpiAwareness=unaware windowRect=default elapsedMs=118
```

- `format`, `alpha`, `swapEffect`, `buffers` and `scaling` come from `IDXGISwapChain1::GetDesc1`, as the numeric
  `DXGI_FORMAT`, `DXGI_ALPHA_MODE`, `DXGI_SWAP_EFFECT` and `DXGI_SCALING` values (in the line above: 87 is
  `DXGI_FORMAT_B8G8R8A8_UNORM`, alpha 3 is `DXGI_ALPHA_MODE_IGNORE`, swap effect 4 is `DXGI_SWAP_EFFECT_FLIP_DISCARD`
  and scaling 0 is `DXGI_SCALING_STRETCH`).
- `style` and `exStyle` are the main window's `GWL_STYLE` and `GWL_EXSTYLE`; `client` is its client rectangle.
- `presentCount` is `IDXGISwapChain::GetLastPresentCount`. `presentFailures` counts `Present` calls that returned a
  failing HRESULT; the player logs each distinct failing HRESULT once.
- `frames` is the number of rendered frames; `elapsedMs` is the wall-clock time from the first to the last Present.
- `idleThrottle` is `on` when the opt-in idle throttle was enabled (profile or command line) and `off` otherwise;
  `idleFrames` and `activeFrames` count the frames that ran while the window was idle or active. With the throttle
  off every frame is active (`idleFrames=0 activeFrames=30`).
- `windowMode` is `normal` for the default window and `overlay` for the opt-in overlay window
  (`--window-mode overlay`, see "Overlay scenario").
- `window` is the window's tag. It is always `main` today; it keys the per-window fingerprints below and prepares
  for several windows in one process (subproject 3c).
- `dpi` is `GetDpiForWindow`, and `dpiAwareness` is the window's DPI awareness context: `unaware`, `system`,
  `permonitor`, `permonitorv2` or `unknown`.
- `windowRect` is `GetWindowRect` as `<left>,<top>,<right>,<bottom>` in overlay mode (for example `0,0,640,360`).
  Normal windows are placed with `CW_USEDEFAULT`, so their position is not deterministic and the value is
  `windowRect=default`; `client` already covers their size.

**Per-window fingerprints.** The script collects **every** `HOST_FINGERPRINT` line of a run's startup log into a map
keyed by the `window` field. A run with no line prints
`FAIL fingerprint <scene> missing: the startup log has no HOST_FINGERPRINT line`, a line without a `window` field is a
`FAIL` (the player predates per-window fingerprints), and two lines with the same tag print
`FAIL fingerprint <scene> duplicate window '<tag>': ...`. Every per-window check is named `<scene>@<window>` (for
example `PASS fingerprint axis_test@main matches record`).

`-Record` stores the map under the scene's `"fingerprints"` key in `manifest.json`: one object per window with every
field in the player's order, ending with that window's `elapsedMs` (a number):

```
{ "id": "axis_test", "kind": "golden", "status": "stable",
  "fingerprints": { "main": { "format": "87", ..., "window": "main", "dpi": "96", "dpiAwareness": "unaware", "windowRect": "default", "elapsedMs": 118 } } }
```

`-Record` also requires every window of run a to be healthy and run b to log the same windows with matching fields.
`-Verify` first compares the window sets exactly: a recorded window the run did not log prints
`FAIL fingerprint <scene>@<window> window missing: ...`, and a window the record does not have prints
`FAIL fingerprint <scene>@<window> window extra: ...`. It then compares every window's fields and prints
`FAIL fingerprint <scene>@<window> <field> recorded=<x> actual=<y>` for every differing field; the pacing `WARN` is
computed per window from its own `elapsedMs`. Without arguments the player never creates the fingerprint and never
writes the line.

**Migration (re-record once).** Manifests recorded before per-window fingerprints store a single `"fingerprint"`
object plus `"elapsedMs"` on each golden entry and on the overlay entry. The script reads only the new shape: an old
golden entry prints
`FAIL fingerprint <scene> manifest entry uses the old single 'fingerprint' shape or has no 'fingerprints' (re-record required)`,
an old overlay entry prints `FAIL overlay <scene> overlay entry uses the old single 'fingerprint' shape (re-record required)`,
and a manifest without an overlayIdle entry prints
`FAIL overlayIdle <scene> overlayIdle entry missing from manifest (re-record required)`. The fingerprint line also
gained `window`, `dpi`, `dpiAwareness` and `windowRect`, which the regression tool requires. Re-record once with
`-Record`; the goldens must come out byte-identical.

## Idle-throttle scenario

The player's idle throttle is opt-in and off in every normal scene run. To prove that it still works and that it
never changes what a frame renders, `-Record` and `-Verify` both run the smoke scene once more, after the scene loop,
with the usual 30-frame arguments plus:

```
--idle-throttle on --idle-after-ms 1 --idle-fps 10
```

The window goes idle 1 ms after the last activity (the player requires at least 1), and idle frames are paced at
10 fps, about 100 ms each, so the run takes about 3 s: the first one or two frames are active while the startup scene
load is pending, the rest are idle. The capture goes to `<WorkRoot>\captures\idle\<scene>.bmp`.

- Every window's `HOST_FINGERPRINT` line goes through the regression tool's
  `check-idle "<fingerprint line>" <minIdleFrames> <minElapsedMs>` command. `-Record` passes the script's
  minimums, `25` and `2400`, and writes them into the manifest's idle entry; `-Verify` passes the minimums recorded
  in that entry. It prints
  `PASS idleFrames=<n> elapsedMs=<n>`, or one `FAIL <reason>` line per failed check (`idleThrottle` is not `on`,
  `presentFailures` is not 0, too few idle frames, or too little elapsed time, which means the throttle did not
  sleep), and exits 0, 1, or 2 on a usage or parse error.
- The capture is compared with the smoke scene's golden using the normal thresholds; on failure `-Record` writes the
  diff to `<WorkRoot>\captures\idle\<scene>.diff.png` and `-Verify` writes it to
  `<WorkRoot>\diffs\<scene>.idle.diff.png`. The scenario deletes a diff left by an earlier run before it runs, so a
  diff image on disk always belongs to the last run.
- The fingerprint is **not** compared field by field with a recorded one: its idle and active frame counts and its
  elapsed time differ from a normal run by design and depend on load timing, so only the minimums above are checked.
- `-Record` adds `{ "id": "<smoke scene>", "kind": "idle", "minIdleFrames": 25, "minElapsedMs": 2400 }` to
  `manifest.json` (the other `-Verify` checks ignore this kind). `-Verify` against a manifest without that entry
  prints `FAIL idle <scene> idle entry missing from manifest (re-record required)`, and against an idle entry without
  `minIdleFrames` or `minElapsedMs` it prints
  `FAIL idle <scene> idle entry lacks minIdleFrames or minElapsedMs (re-record required)`.
- Check lines are `PASS|FAIL|SKIP idle <scene> <detail>`; the `check-idle` lines name the window
  (`PASS idle axis_test window main: idleFrames=26 elapsedMs=2828`).

**Physics caveat.** A scene whose physics steps every update never goes idle: the throttle keeps the window awake
while `PredictedPhysicsStepSeconds` is above 0 or while a scene transition or pending scene operation is reported. The
idle scenario therefore needs a scene without physics.

**Audio keep-awake.** The throttle also keeps the window awake while a looping audio voice plays (the Windows audio
backend reports a looping voice that is not paused, including one waiting for its restart), because the backend
restarts a looping voice from its per-frame update and an idle interval between frames would be heard as a gap. A
scene with looping music or ambience therefore stays at full rate while it plays, and the idle scenario also needs a
scene without looping audio. `axis_test` has no audio component. The smoke scene `axis_test` qualifies: its scene file
(`assets\scenes\rendering\axis_test.helen`) holds only camera, light, mesh, text, sprite, viewport, FPS and DemoDisc
rendering and menu components, with no rigid body, collider or other physics component, and its startup log has no
physics-runtime line. Two manual idle-scenario runs on 2026-09-25 reported `idleFrames=29 activeFrames=1` with
`elapsedMs` 3050 and 3052, and their captures matched the `axis_test` golden with 0 differing pixels. If the smoke
scene ever gains physics, point the idle scenario at the first golden scene without physics and update this section.

**Re-record once.** The fingerprint gained `idleThrottle`, `idleFrames` and `activeFrames`, and the manifest gained the
idle entry, so goldens recorded before the idle throttle must be re-recorded once with `-Record`: an older manifest
fails every fingerprint check (missing fields) and the idle check (`idle entry missing from manifest`).

## Overlay scenario

The player's overlay window (`--window-mode overlay`, a DirectComposition swap chain with premultiplied alpha and
per-pixel click-through) is opt-in and off in every normal scene run. To prove that it still renders a valid
transparent frame and that its hit-test readback still decides click-through from the drawn alpha, `-Record` and
`-Verify` both run the smoke scene in overlay mode, after the idle scenario, with the usual 30-frame arguments plus:

```
--window-mode overlay --overlay-bounds profile --overlay-background transparent --hit-test-probe <x,y>
```

`--overlay-bounds profile` sizes the overlay from the 640x360 `profile.json` the script writes before every run, and
`--overlay-background transparent` clears to (0,0,0,0), so only the scene's drawn content is opaque.
`--hit-test-probe x,y` (valid only with `--frames` in overlay mode) samples that fixed client pixel through the same
readback that follows the cursor in normal use, applies the result exactly as the cursor path does
(`Win32ClickThroughController::Apply(alpha < 8)`, which sets or clears `WS_EX_TRANSPARENT`), reads the window's
`GWL_EXSTYLE` back and logs
`HIT_TEST x=<x> y=<y> alpha=<a> clickThrough=<on|off> exStyle=0x<8 upper-case hex digits>` in the startup log after the
last frame (`clickThrough=on` when the alpha is below 8). The window is created click-through, so the opaque probe
proves that `Apply` really clears the bit: today the transparent probe logs `clickThrough=on exStyle=0x002800A8` and
the opaque probe `clickThrough=off exStyle=0x00280088`. A run whose readback never completed logs `alpha=pending` and
exits with code 3.

Because the probe's toggle changes the window's `exStyle`, the window's `HOST_FINGERPRINT` also differs per probe, so
the overlay entry keeps **one fingerprint map per probe**.

**Probes.** The probe points come from the overlay golden itself. `find-probes <golden.png>` prints
`TRANSPARENT x,y` (the first pixel, in row-major order, whose 5x5 neighbourhood is entirely alpha 0) and
`OPAQUE x,y` (the same for alpha 255), kept 2 pixels from every edge so that the probe never sits on an anti-aliased
border. They are stored in the manifest as `transparentProbe` and `opaqueProbe`.

**Record** (everything goes to `<WorkRoot>\record-staging` first, like every other golden):

1. One record run with `--hit-test-probe 2,2`, an arbitrary in-bounds point (the probe never changes the capture;
   the real probes are only known once the golden exists), captured to `<WorkRoot>\captures\overlay\<scene>.record.bmp`.
   Every window's fingerprint must be healthy (`check-fingerprint`) and describe the overlay window
   (`windowMode=overlay`, `alpha=1`, and an `exStyle` holding `WS_EX_NOREDIRECTIONBITMAP`, `WS_EX_LAYERED`,
   `WS_EX_TOOLWINDOW` and `WS_EX_TOPMOST`, mask `0x00280088`), and its capture must pass `check-premultiplied`.
2. `record-golden-rgba` turns that capture into `<scene>.overlay.png` (today `axis_test.overlay.png`), keeping its
   real alpha, and `find-probes` picks the two probes from it.
3. One run with the transparent probe (it must report `clickThrough=on` with `WS_EX_TRANSPARENT`, bit `0x20`, set in
   `exStyle`) and one with the opaque probe (it must report `clickThrough=off` with the bit cleared). Each must match
   the staged golden with `compare-rgba` and pass `check-premultiplied`, and each window's fingerprint must be healthy
   and describe the overlay window. Record accepts any `exStyle` that passes the bit rule and stores the exact value
   it observed.
4. The manifest gains

   ```
   { "id": "<smoke scene>", "kind": "overlay", "transparentProbe": "x,y", "opaqueProbe": "x,y",
     "transparentExStyle": "0x002800A8", "opaqueExStyle": "0x00280088",
     "fingerprintsByProbe": { "transparent": { "main": { ..., "elapsedMs": <n> } }, "opaque": { "main": { ... } } } }
   ```

   where each probe's fingerprints are that probe run's window map, stored like a scene's `"fingerprints"`.

**Verify** runs the overlay scenario twice, first with the recorded transparent probe and then with the opaque one
(captures `<WorkRoot>\captures\overlay\<scene>.transparent.bmp` and `<scene>.opaque.bmp`). Each run must:

- exit with code 0 and write its capture and `HOST_FINGERPRINT` line;
- log exactly `HIT_TEST x=<probe x> y=<probe y> alpha=<a> clickThrough=on exStyle=<transparentExStyle>` for the
  transparent probe, or `clickThrough=off exStyle=<opaqueExStyle>` for the opaque probe. The bit rule is checked too
  (`0x20` set for `on`, cleared for `off`), so a recorded value that breaks it can never pass. A different `exStyle`
  prints `FAIL overlay <scene> <probe> probe exStyle recorded=<x> actual=<y>: <line>`, and a recorded value that is not
  `0x<8 upper-case hex digits>` is a `FAIL` (re-record required);
- pass `check-premultiplied <capture.bmp> 0.01 0.01`: the capture is read with its real alpha, every pixel has B, G
  and R at most A (valid premultiplied alpha), and at least 1% of the pixels are fully transparent and at least 1%
  fully opaque;
- match `regression\golden\<scene>.overlay.png` with `compare-rgba`, which compares all four channels with the normal
  thresholds (a channel differs by more than 8 levels; more than 0.1% of the pixels differ fails). The opaque-forcing
  `compare` is never used on an overlay capture. On failure the diff goes to
  `<WorkRoot>\diffs\<scene>.overlay.transparent.diff.png` or `<scene>.overlay.opaque.diff.png`;
- match **its own probe's** recorded fingerprints (`fingerprintsByProbe.transparent` or `fingerprintsByProbe.opaque`)
  window for window and field for field, like a scene's fingerprints. Its lines are named
  `<scene>.overlay.transparent@<window>` and `<scene>.overlay.opaque@<window>` (for example
  `FAIL fingerprint axis_test.overlay.opaque@main exStyle recorded=<x> actual=<y>`), so they never mix with the scene's
  normal-run fingerprint lines.

A manifest without an overlay entry prints `FAIL overlay <scene> overlay entry missing from manifest (re-record required)`,
an entry in the old shape prints `FAIL overlay <scene> overlay entry uses the old single 'fingerprint' shape (re-record required)`,
and an entry without `transparentProbe`, `opaqueProbe`, `transparentExStyle`, `opaqueExStyle` or both probes'
fingerprints prints
`FAIL overlay <scene> overlay entry lacks transparentProbe, opaqueProbe, transparentExStyle, opaqueExStyle or fingerprintsByProbe.transparent/opaque (re-record required)`.
A recorded `transparentExStyle` or `opaqueExStyle` that is not `0x<8 upper-case hex digits>` (an empty string
included) prints `FAIL overlay <scene> overlay entry's transparentExStyle '<x>' or opaqueExStyle '<y>' is not 0x<8 upper-case hex digits> (re-record required)`,
and neither the probe runs nor the overlay+idle run starts. Only Record's own probe runs accept any `exStyle` that
passes the bit rule, through an explicit record mode; outside it an exact expected value is always required.
The other check lines are `PASS|FAIL overlay <scene> <detail>`.

**What it proves, and what it cannot see.** The scenario proves that overlay mode creates the overlay window and its
premultiplied composition swap chain (the fingerprint), that the frame the player presents is valid premultiplied
alpha with a transparent background and opaque content, that this frame has not changed, and that the hit-test
readback turns a transparent pixel into click-through on and an opaque pixel into click-through off, with `Apply`
really setting and clearing `WS_EX_TRANSPARENT` on the window. The capture is the swap chain's back buffer before
Present. The net cannot see what DWM finally composites on screen (whether the overlay really shows over other windows
with the desktop visible through its transparent pixels), and probe runs never route a real mouse click or follow the
live cursor. Those are covered only by the manual proofs in the
overlay window spec (`docs\superpowers\specs\2026-09-25-windows-player-overlay-window-design.md`, section 7): the
overlay is visible over other windows with a transparent background, clicks on transparent areas reach the window
behind, clicks on opaque content activate the overlay, and a no-argument boot is unchanged.

The overlay window is topmost while it runs; do not click over it or move the mouse across it during Record/Verify.

**Known limitations of the overlay window** (recorded in the spec's Revision 2; not fixed yet):

- The window is created click-through (`WS_EX_TRANSPARENT`, `exStyle=0x002800A8` until the first hit-test sample)
  and fails open: it never blocks the mouse before its first hit-test sample over opaque content.
- The click-through state freezes during slow or hung frames, because sampling runs on the render thread.
- With the idle throttle, a move from a transparent to an opaque pixel is only noticed at the next idle tick (at most
  1/idleFps later), so a quick click there can still go to the window behind.
- With `--overlay-bounds monitor`, any mouse motion anywhere on the primary monitor keeps idle mode at the full rate.
- The default `--overlay-background camera` with an opaque camera clear gives a full-monitor window that blocks clicks
  and has no taskbar button. Use `transparent`, or scenes that clear with alpha 0.
- Display, resolution and DPI changes are not handled until subproject 3.
- Subproject 3a added the per-probe `exStyle` after `Apply`, the `dpi`, `dpiAwareness` and `windowRect` fingerprint
  fields, per-window fingerprints and the overlay+idle scenario. The net still cannot see the monitor-bounds
  resolution or the live cursor path (see "Overlay + idle scenario").
- A probe in the manifest that is not `x,y` with base-10 coordinates is a `FAIL` (re-record required); the player is
  not launched with it.

## Overlay + idle scenario

The overlay window and the idle throttle are both opt-in, and they meet in one path: an idle overlay samples the
cursor only at idle ticks. To prove that the combination still renders the same frame, still goes idle and still
toggles click-through, `-Record` and `-Verify` run the smoke scene once more, after the overlay scenario, with the
usual 30-frame arguments plus:

```
--window-mode overlay --overlay-bounds profile --overlay-background transparent --idle-throttle on --idle-after-ms 1 --idle-fps 10 --hit-test-probe <transparent probe>
```

The capture goes to `<WorkRoot>\captures\overlayIdle\<scene>.bmp`. The run must:

- exit with code 0 and write its capture and `HOST_FINGERPRINT` lines;
- describe the overlay window in every window's fingerprint (`windowMode=overlay`, `alpha=1` and the overlay `exStyle`
  bits `0x00280088`), checked on its own because these fingerprints are never compared field by field;
- pass `check-idle` on every window with the minimums of the manifest's `idle` entry (today 25 idle frames and
  2400 ms);
- pass `check-premultiplied <capture.bmp> 0.01 0.01`;
- match the overlay golden `regression\golden\<scene>.overlay.png` with `compare-rgba` (neither the throttle nor the
  overlay may change the frame). On failure `-Record` writes the diff to
  `<WorkRoot>\captures\overlayIdle\<scene>.diff.png` and `-Verify` to `<WorkRoot>\diffs\<scene>.overlayIdle.diff.png`;
- log exactly `HIT_TEST x=<x> y=<y> alpha=<a> clickThrough=on exStyle=<transparentExStyle>` with bit `0x20` set.

Its fingerprints are **not** compared: the idle and active frame counts and the elapsed time differ from a normal
overlay run by design, and `check-idle` already checks them. `-Record` runs it right after the two probe runs, with
the transparent probe and `exStyle` it just observed, and adds
`{ "id": "<smoke scene>", "kind": "overlayIdle", "fingerprints": { "main": { ... } } }` to `manifest.json`, the
fingerprints kept for reference only. `-Verify` takes the probe and `exStyle` from the overlay entry and the minimums
from the idle entry. A manifest without an overlayIdle entry prints
`FAIL overlayIdle <scene> overlayIdle entry missing from manifest (re-record required)`; when the overlay or idle entry
is incomplete, the scenario is not run and prints `FAIL overlayIdle <scene> not run: ...`. Check lines are
`PASS|FAIL overlayIdle <scene> <detail>`.

A manual run on 2026-09-26 (the functions on the Task 1 player, no `-Record`) logged
`HIT_TEST x=2 y=2 alpha=0 clickThrough=on exStyle=0x002800A8`, passed `check-idle` with `idleFrames=29 elapsedMs=3062`
(fingerprint `idleThrottle=on idleFrames=29 activeFrames=1 windowMode=overlay window=main dpi=96
dpiAwareness=unaware windowRect=0,0,640,360`) and passed `check-premultiplied`.

**Blind spot: the live cursor.** In normal use an idle overlay wakes and re-samples when the cursor moves over it, and
the click-through state follows the cursor. That path cannot be automated without moving the mouse, which the net never
does, so the probe replaces the cursor with a fixed pixel. Whether cursor motion over a click-through overlay wakes the
idle throttle, and how quickly a move from a transparent to an opaque pixel is noticed, stays a manual check.

## What this net does NOT catch

- Resize and `ResizeBuffers` paths: the window is never resized during a run.
- On-screen composition beyond the back buffer: what a DirectComposition commit or the DWM finally shows is not
  captured, only the swap chain's back buffer before Present. For the overlay window this includes whether it is
  visible over other windows and whether real clicks pass through its transparent pixels (see "Overlay scenario";
  only the manual proofs cover that).
- The live cursor path of the overlay window: cursor-driven hit testing and the idle wake on cursor motion over a
  click-through overlay (the probe uses a fixed pixel instead; see "Overlay + idle scenario").
- Exact frame pacing: `elapsedMs` only produces a coarse `WARN pacing` when it moves by more than a factor of three.
- The Release configuration: the net builds and runs the Debug player only.
- Different GPUs and drivers: the goldens are only valid on the machine that recorded them.
- UI and menu flows: the smoke scene is `axis_test`, and DemoDisc's menu and gameplay scenes are not built.

## Current record

The committed `manifest.json` below still has the old single-`fingerprint` shape, has no `window`, `dpi`,
`dpiAwareness` or `windowRect` fields and no overlayIdle entry, so `-Verify` fails it with "re-record required" until
the one-time re-record described in "Migration (re-record once)".

The goldens were first recorded on 2026-09-24 from commit `f8a92cb`, and re-recorded on 2026-09-25 from commit
`d7defda` (feature/regression-safety-net) to add the host fingerprints, the executed-test counts and the provenance
hashes. They were re-recorded again on 2026-09-25 on the owner's development machine, from commit `b8e1197`
(feature/idle-throttle), because the fingerprint gained `idleThrottle`, `idleFrames` and `activeFrames` and the
manifest gained the idle entry. They were re-recorded once more on 2026-09-25 on the same machine, from commit
`7089c4d` (feature/overlay-window), because the fingerprint gained `windowMode` and the manifest gained the overlay
entry and the overlay golden `axis_test.overlay.png`. They were re-recorded a last time on 2026-09-26 on the same
machine, from commit `f807ee6` (feature/overlay-window), because the overlay window is now created click-through
(`WS_EX_TRANSPARENT`), which changed only the overlay entry's `exStyle` from `0x00280088` to `0x002800A8`: every golden
PNG (including `axis_test.overlay.png`) was byte-identical, every normal fingerprint differed only in `elapsedMs`, and
the probes stayed `2,2` and `393,2`. DemoDisc was at `5cc124eec06b8db729b2f9b15d99d26cb1ca8cc3` and
helengine at `dd9ca693403d91b5e2fc52860a3511e6358ed776` with uncommitted changes (`helengineDirty: true`); the
provenance hashes are unchanged from the idle-throttle record.

- All 11 rendering scenes were stable across the two record runs (0 differing pixels), so every scene has a golden
  and no scene is marked `unstable`. The re-recorded golden PNGs are byte-identical to the first record
  (`git diff --stat -- regression/golden/*.png` was empty after the overlay record; only `axis_test.overlay.png` is
  new).
- Every scene's fingerprint: `format=87 alpha=3 swapEffect=4 buffers=2 scaling=0 style=0x14CF0000
  exStyle=0x00000100 client=640x360 presentCount=30 presentFailures=0 frames=30 idleThrottle=off idleFrames=0
  activeFrames=30 windowMode=normal`, `elapsedMs` 118 to 127. Apart from the new `windowMode=normal`, every field is
  the same as in the idle-throttle record.
- The idle scenario on `axis_test` passed with `idleFrames=29 elapsedMs=3053`, and its capture matched the golden
  with 0 differing pixels.
- The overlay scenario on `axis_test`: fingerprint `format=87 alpha=1 swapEffect=4 buffers=2 scaling=0
  style=0x94000000 exStyle=0x002800A8 client=640x360 presentCount=30 presentFailures=0 frames=30 idleThrottle=off
  idleFrames=0 activeFrames=30 windowMode=overlay`, `elapsedMs` 119 (`exStyle` was `0x00280088` before the
  click-through-at-creation change); `check-premultiplied` reported 65.4% fully
  transparent and 29.8% fully opaque pixels; `find-probes` chose `transparentProbe` `2,2` and `opaqueProbe` `393,2`,
  which logged `alpha=0 clickThrough=on` and `alpha=255 clickThrough=off`, and both probe runs matched the overlay
  golden with 0 differing pixels.
- Baselines: `helengine.editor.tests` 50 failing of 3134 executed, `helengine.render.validation.tests` 1 failing of
  1 executed, `helengine.windows.builder.tests` 0 failing of 164 executed (161 before the final-review source tests). All five known-flaky keyboard-focus tests
  (the four `SceneHierarchyPanelKeyboardFocusTests` arrow-key tests and
  `EditorSessionUndoRedoIntegrationTests.Keyboard_focus_update_component_routes_delete_into_the_session_handler`)
  happened to fail during this record, so they are all in the editor baseline (49 -> 50); they are also in the
  flaky list, and `-Verify` reports them as `FIXED` when they pass.
- DemoDisc's `user_settings\generated_code` holds only `obj` files, which the copy leaves out, so
  `generatedCodeHash` is the SHA-256 of an empty list.
- After the record, `-Verify` passed three times with every golden at 0 differing pixels, every fingerprint
  matching and the idle and overlay scenarios passing. With the overlay scenario's `--overlay-background transparent`
  changed locally to `camera`, `-Verify` failed (`RESULT: FAIL (6 failing)`) with
  `FAIL overlay axis_test transparent run premultiplied: FAIL transparent 0 below 0.01`, the transparent probe
  reporting `alpha=255 clickThrough=off`, both captures differing from the overlay golden in 65.4% of their pixels,
  and the builder source test `RegressionScript_RunsTheOverlayScenarioWithAlphaAwareChecks` failing on the changed
  script. The earlier record history: the idle-throttle record also passed `-Verify` twice, and with its
  `--idle-fps 10` changed locally to 30, `-Verify` failed with `FAIL idle axis_test elapsedMs=1279 is below 2400`.
- Manual overlay proofs (no screenshots, the mouse was never moved): a full-monitor overlay
  (`--window-mode overlay --overlay-bounds monitor --overlay-background transparent --scene axis_test`, no
  `--frames`) left `GetForegroundWindow` unchanged, reported exStyle `0x002800A8` (TOPMOST, TOOLWINDOW, LAYERED,
  NOREDIRECTIONBITMAP plus the click-through `WS_EX_TRANSPARENT`, because the cursor sat over a transparent pixel),
  and `WindowFromPoint` at the cursor returned the browser window behind the overlay, not the overlay. A
  no-argument boot was alive after 5 s, loaded `axis_test` and logged no overlay lines.

## How to run

```powershell
powershell -File scripts\run-regression.ps1 -Verify
```

The run builds the player (through `helengine\scripts\build-platform.ps1`), runs every scene through
`scripts\launch_in_emulator.ps1` (with `-Wait -TimeoutSeconds 120`), and runs the three test suites. It prints one
line per check (`PASS|FAIL|SKIP|WARN <kind> <name> <detail>`), then `RESULT: PASS` or `RESULT: FAIL (<n> failing)`.
Only `FAIL` lines count toward the result. It exits 0 only on PASS.

Optional parameters: `-HelengineRoot` (default `C:\dev\helworks\helengine`), `-ProjectSource` (default
`C:\dev\helprojs\demodisc`), `-WorkRoot` (default `C:\dev\helworks\builds\helengine-windows\regression`) and
`-PlatformsManifestPath` (default `<HelengineRoot>\user_settings\platforms.json`; see "Building against a helengine
worktree"). The
work root must not overlap the project source, the helengine checkout or this checkout. The script marks every work
root it uses with a `.helengine-regression-workroot` file and refuses a non-empty folder without that marker,
because it deletes and rewrites folders under the work root.

Useful outputs under the work root: `captures\verify\*.bmp`, `diffs\*.diff.png`, `trx\<suite>.trx`,
`trx\<suite>.log`, `record-staging\` (the last record) and `player\helengine_windows.startup.log`. When the player
exits with a non-zero code or times out, the script prints the last 20 lines of the startup log.

While the net runs:

- Do not use the keyboard or mouse on the player window; input changes what the scenes render.
- Do not interact with the player window (mouse or keyboard) during Record/Verify; the idle scenario counts activity,
  and input during its run keeps the window awake and can fail its idle-frame and elapsed-time minimums.
- If you run it from the main checkout (not a worktree), close the editor first: the script rebuilds
  `builder\bin\Debug\net9.0\helengine.windows.builder.dll`, which the real `platforms.json` also points at.
- The net builds and runs the **Debug** player only.

## Project pinning

The goldens show DemoDisc as it was at one commit. If the net built whatever DemoDisc's HEAD is today, any
DemoDisc change (for example regenerated scenes) would look like a player regression. So **`-Verify` is pinned**: it
always builds the `projectCommit` recorded in `regression\golden\manifest.json` (`git archive <projectCommit>`), and
it never follows the project's HEAD.

- When DemoDisc's HEAD differs from the pin, `-Verify` prints
  `WARN project changed since record: HEAD is <head>, but Verify built the pinned recorded commit <pin>` and carries on.
  Every check still runs against the pinned commit.
- A manifest without `projectCommit` stops the run with
  `The manifest has no projectCommit to pin the project to (re-record required)`, and a missing manifest stops it with
  `... but the manifest is missing (re-record required)`.
- A pinned commit the project source does not have (for example a history rewrite, or a different clone) stops the
  run with `The recorded DemoDisc commit '<sha>' was not found in the project source <path> (git cat-file -e failed).`
  Fetch the commit into the project source, or re-record.
- `-ProjectCommit` is refused with `-Verify`.
- Only the committed project is pinned. The git-ignored `user_settings` folder (its build config and generated code)
  is still copied from the project's working tree, and the `buildConfigSourceHash` and `generatedCodeHash` WARNs
  below still report when it changed.

**Moving DemoDisc forward, deliberately.** `-Record` builds `-ProjectCommit <sha>` (default: DemoDisc's HEAD) and
records that commit as the new pin. To move the pin, run `-Record -ProjectCommit <sha>` for a specific commit or a
plain `-Record` for the current HEAD. Then follow "Re-recording (deliberately)": look at the diffs first, because
every golden may change with the project. `-BuildOnly` also accepts `-ProjectCommit` (default HEAD) and prints the
built commit as `PROJECT_COMMIT=`. To reproduce the player a `-Verify` builds, pass the manifest's `projectCommit`.
Every mode also prints `PROJECT_HEAD=<DemoDisc HEAD>`.

## Provenance warnings

`manifest.json` records the provenance of the goldens and baselines:

- `projectSource` and `projectCommit` (DemoDisc's HEAD);
- `buildConfigSourceHash`: the SHA-256 of DemoDisc's own `user_settings\build_config.json`, read before the copy's
  build config is overridden;
- `generatedCodeHash`: the SHA-256 of DemoDisc's `user_settings\generated_code` tree (every file's relative path and
  SHA-256, sorted, then hashed; `bin` and `obj` folders are left out as in the copy);
- `helengineCommit` (`git rev-parse HEAD` of `-HelengineRoot`) and `helengineDirty` (whether
  `git status --porcelain` was non-empty);
- `helengineWorkingTreeHash`: the SHA-256 of `git diff HEAD` plus the sorted `git ls-files --others --exclude-standard`
  list of `-HelengineRoot`, so two different uncommitted states can be told apart.

`-Verify` prints:

- `WARN project changed since record: HEAD is <head>, but Verify built the pinned recorded commit <pin>` when
  DemoDisc's HEAD has moved (see "Project pinning");
- `WARN helengine changed since record: ...` when the helengine commit or its dirty state differs;
- `WARN <input> changed since record ...` for each of `buildConfigSourceHash`, `generatedCodeHash` and
  `helengineWorkingTreeHash` that differs.

A WARN does not fail the run, and every comparison still runs. It tells you that a failure may come from the
project or from helengine rather than from this checkout. The untracked-file part of `helengineWorkingTreeHash`
covers only the file names, not their contents.

## Re-recording (deliberately)

```powershell
powershell -File scripts\run-regression.ps1 -Record
```

Only re-record when a rendering or host change is **intended**, or when the WARN lines above explain a failure
(DemoDisc or helengine moved on purpose). Never re-record just to make a failing `-Verify` pass.

1. Run `-Verify` first and look at the `diffs\*.diff.png` images to confirm that the changes are the intended ones.
2. Run `-Record`. It runs every scene twice (`captures\a` and `captures\b`). A scene whose two captures do not match
   within the thresholds is marked `unstable` in the manifest and gets no golden; `-Verify` then prints
   `SKIP golden <scene> unstable` for it. Record writes everything (goldens, `manifest.json`, and each suite's
   `<suite>.failing.txt` and `<suite>.executed.txt`) to `<WorkRoot>\record-staging` first. Only when the whole
   record had no `FAIL` does it replace `regression\golden\*.png` and `manifest.json` and overwrite the baseline
   files (the hand-maintained `<suite>.flaky.txt` lists are kept). On any `FAIL` the committed files stay untouched
   and the script prints the staging path. A suite run that errored or aborted is a `FAIL` and is never recorded.
3. Run `-Verify` twice to confirm that the new goldens pass with no false failures.
4. Commit `regression\golden\*.png`, `regression\golden\manifest.json`, `regression\baselines\*.failing.txt` and
   `regression\baselines\*.executed.txt` in a commit of their own, and say in the message why they changed.

## Building against a helengine worktree

To build the player against a clean helengine revision (for example the commit Helena's main checkout is on) without
touching her main checkout or its uncommitted files, build against a detached helengine worktree. Creating the
worktree adds git metadata only; everything else happens inside the worktree and the regression work root.

1. Record the main checkout's state, so you can prove afterwards that it did not change:

   ```powershell
   git -C C:\dev\helworks\helengine rev-parse HEAD
   git -C C:\dev\helworks\helengine status --porcelain > <work root>\main-status-before.txt
   Get-FileHash <work root>\main-status-before.txt
   ```

2. Create the detached worktree at the main checkout's HEAD (stop if the path already exists):

   ```powershell
   git -C C:\dev\helworks\helengine worktree add --detach .worktrees\regression-reference <HEAD>
   ```

3. Initialize the two submodules the build needs, inside the worktree only (uses the pinned commits and your SSH keys):

   ```powershell
   git -C C:\dev\helworks\helengine\.worktrees\regression-reference submodule update --init engine/vendor/csharpcodegen engine/vendor/bepuphysics2
   ```

   **If the pinned submodule commit is not on origin, fetch it from the main checkout's submodule.** This happens
   when helengine pins a csharpcodegen commit that was never pushed (`upload-pack: not our ref <sha>`). Reading the
   main checkout's submodule as a fetch source writes nothing there:

   ```powershell
   git -C C:\dev\helworks\helengine\.worktrees\regression-reference\engine\vendor\csharpcodegen fetch C:\dev\helworks\helengine\engine\vendor\csharpcodegen <pinned sha>
   git -C C:\dev\helworks\helengine\.worktrees\regression-reference submodule update engine/vendor/csharpcodegen
   git -C C:\dev\helworks\helengine\.worktrees\regression-reference submodule status
   ```

   `submodule status` must show both pinned commits with no leading `+`. Never copy files instead.

4. Build. `user_settings\platforms.json` is git-ignored, so the worktree has none; pass the main checkout's file with
   `-PlatformsManifestPath` (default `<HelengineRoot>\user_settings\platforms.json`). Its relative paths are made
   absolute against that file's folder. Generated output paths in it (`generatedCoreCppRootPath`) that lie under the
   main checkout are moved to the same relative path under `-HelengineRoot` (and created, because the engine reports a
   platform whose generated-core folder is missing as not installed), and the script prints the one the windows build
   uses as `GENERATED_CORE_ROOT=`, so nothing points the build into the main checkout. (Today's engine writes the
   generated core itself into its build cache under `C:\dev\helworks\builds\helengine\cache`.) A default run
   (the manifest belongs to `-HelengineRoot`) writes exactly the same isolated `platforms.json` as before. Pass the
   manifest's `projectCommit` so the build matches the goldens:

   ```powershell
   powershell -NoProfile -File scripts\run-regression.ps1 -BuildOnly -HelengineRoot C:\dev\helworks\helengine\.worktrees\regression-reference -PlatformsManifestPath C:\dev\helworks\helengine\user_settings\platforms.json -ProjectCommit <manifest projectCommit> > <log> 2>&1
   ```

   It must exit 0 and print `PLAYER=`, and `GENERATED_CORE_ROOT=` must lie under the worktree.

5. Check one scene against its golden with that player (write the profile first, because a build may remove it):

   ```powershell
   $player = 'C:\dev\helworks\builds\helengine-windows\regression\player'
   [System.IO.File]::WriteAllText("$player\profile.json", '{"resolutionWidth":640,"resolutionHeight":360}')
   & scripts\launch_in_emulator.ps1 -ArtifactPath "$player\helengine_windows.exe" -ArgumentList @('--scene', 'axis_test', '--frames', '30', '--fixed-delta', '0.016666', '--capture', '<capture>.bmp') -Wait -TimeoutSeconds 120
   & tools\regression\bin\Release\net9.0-windows\helengine.windows.regression.exe compare <capture>.bmp regression\golden\axis_test.png <diff>.png
   ```

   The compare must print `PASS 0`.

6. Check that the main checkout's `rev-parse HEAD` and the hash of its `status --porcelain` are unchanged. Keep the
   worktree for later runs, or remove it with
   `git -C C:\dev\helworks\helengine worktree remove --force .worktrees\regression-reference`.

## Known limitations

- The build goes through helengine's canonical build script, which regenerates core output under `helengine\tmp`.
  That folder is shared with normal builds, so do not run the regression net while another helengine build is
  running.
- The editor-side suites run against `-HelengineRoot` as it is on disk, including uncommitted work there. Their
  bin/obj output is written into that checkout as for any normal `dotnet test`.
- The project source must not use Git LFS: `git archive` would export pointer files, so the script stops when the
  extracted copy's `.gitattributes` contains `filter=lfs`.
- Known-flaky tests are tolerated only when they are listed explicitly (see below).

## Known flaky tests

`regression\baselines\<suite>.flaky.txt` (optional, one fully qualified test name per line, the same format as
`<suite>.failing.txt`) lists tests that pass in some full-suite runs and fail in others for reasons outside this
checkout. When one of them fails but is not in the baseline, `-Verify` prints
`WARN suite <suite> known-flaky test failed: <name>`, both inline and in the summary. A WARN never changes
`RESULT`. Every other new failure still fails the run. The tool command is
`compare-failing <baseline.txt> <current.txt> [<flaky.txt>]`.

`helengine.editor.tests.flaky.txt` lists five keyboard-focus tests: the four arrow-key tests in
`SceneHierarchyPanelKeyboardFocusTests` and
`EditorSessionUndoRedoIntegrationTests.Keyboard_focus_update_component_routes_delete_into_the_session_handler`.
In repeated full runs each of them sometimes passed and sometimes failed; run on their own, they always pass.

- **Root cause (in helengine):** `TextBoxComponent.FocusedTextEntry` (`helengine.core`) is a process-wide static.
  A text box that an earlier test in the same process left focused keeps it set. While it is set,
  `EditorKeyboardFocusUpdateComponent` treats text entry as active and ignores the arrow keys and Delete, which
  are exactly the keys these tests press.
- **Proper fix:** it belongs in helengine (for example, clear the focused text box when the text box or the
  `Core` is disposed). This checkout treats helengine as read-only.
- **Remove entries once fixed:** as soon as helengine fixes the leak, delete the entries from the flaky list (and
  the file when it is empty), so that these tests are fully checked again. Never add a test to a flaky list to
  hide a real regression. Add one only after you have seen it both pass and fail with no change, and document
  the cause here.
