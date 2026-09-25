# Windows player regression net

`scripts\run-regression.ps1` builds **this checkout's** Windows player against an isolated copy of DemoDisc's
committed HEAD. It then checks that the player still renders what it rendered when the goldens were recorded, that
its host layer (swap chain, window, Present) is unchanged, and that the editor-side test suites have no new failures.

## What is checked

| Check | Scenes / suites | Passes when |
|---|---|---|
| `scene` | Every scene run | The player exits within 120 seconds. A hung player is killed and reported as `FAIL scene <id> timeout`. |
| `smoke` | The first rendering scene in build order (currently `axis_test`) | The player exits with code 0, writes a capture, and the capture is not blank. |
| `golden` | Every DemoDisc rendering scene in the committed windows scene package (`scenes/rendering/*.helen`), including the smoke scene | The capture matches `regression\golden\<scene>.png` within the thresholds below. |
| `fingerprint` | Every scene run | The run's `HOST_FINGERPRINT` line matches the recorded one field for field, `presentCount` is at least `frames` and `presentFailures` is 0 (see "Host fingerprint"). |
| `pacing` | Every scene run | Never fails. `WARN pacing <scene> recorded=<x>ms actual=<y>ms` when the run took more than 3 times, or less than a third of, the recorded wall-clock time. |
| `suite` | `helengine.editor.tests`, `helengine.render.validation.tests` (both from `-HelengineRoot`), `helengine.windows.builder.tests` (this checkout) | The run neither errored nor aborted, executed at least 95% of the tests recorded in `<suite>.executed.txt` (any other difference is a `WARN`), and its set of failing tests adds no test that is missing from `regression\baselines\<suite>.failing.txt`. Known failures stay tolerated; fixed ones are only reported. Tests in `<suite>.flaky.txt` only produce a `WARN` (see "Known flaky tests"). |
| `idle` | The smoke scene, once more, with the idle throttle on (see "Idle-throttle scenario") | The manifest has an idle entry, `check-idle` passes (throttle on, no Present failures, at least 25 idle frames, at least 2400 ms elapsed) and the capture matches the smoke scene's golden. |
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

With `--frames`, and only then, the player writes one line to `helengine_windows.startup.log` after the last frame is
presented and before it quits:

```
HOST_FINGERPRINT format=87 alpha=3 swapEffect=4 buffers=2 scaling=0 style=0x14CF0000 exStyle=0x00000100 client=640x360 presentCount=30 presentFailures=0 frames=30 idleThrottle=off idleFrames=0 activeFrames=30 elapsedMs=111
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

`-Record` stores every field except `elapsedMs` under the scene's `fingerprint` entry in `manifest.json`, and
`elapsedMs` next to it. It also requires run a to be healthy and run b to match run a. `-Verify` prints
`FAIL fingerprint <scene> <field> recorded=<x> actual=<y>` for every differing field. Without arguments the player
never creates the fingerprint and never writes the line.

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

- The run's `HOST_FINGERPRINT` line goes through the regression tool's
  `check-idle "<fingerprint line>" <minIdleFrames> <minElapsedMs>` command with `25` and `2400`. It prints
  `PASS idleFrames=<n> elapsedMs=<n>`, or one `FAIL <reason>` line per failed check (`idleThrottle` is not `on`,
  `presentFailures` is not 0, too few idle frames, or too little elapsed time, which means the throttle did not
  sleep), and exits 0, 1, or 2 on a usage or parse error.
- The capture is compared with the smoke scene's golden using the normal thresholds; on failure the diff is written
  to `<WorkRoot>\captures\idle\<scene>.diff.png`.
- The fingerprint is **not** compared field by field with a recorded one: its idle and active frame counts and its
  elapsed time differ from a normal run by design and depend on load timing, so only the minimums above are checked.
- `-Record` adds `{ "id": "<smoke scene>", "kind": "idle", "minIdleFrames": 25, "minElapsedMs": 2400 }` to
  `manifest.json` (the other `-Verify` checks ignore this kind). `-Verify` against a manifest without that entry
  prints `FAIL idle <scene> idle entry missing from manifest (re-record required)`.
- Check lines are `PASS|FAIL|SKIP idle <scene> <detail>`.

**Physics caveat.** A scene whose physics steps every update never goes idle: the throttle keeps the window awake
while `PredictedPhysicsStepSeconds` is above 0 or while a scene transition or pending scene operation is reported. The
idle scenario therefore needs a scene without physics. The smoke scene `axis_test` qualifies: its scene file
(`assets\scenes\rendering\axis_test.helen`) holds only camera, light, mesh, text, sprite, viewport, FPS and DemoDisc
rendering and menu components, with no rigid body, collider or other physics component, and its startup log has no
physics-runtime line. Two manual idle-scenario runs on 2026-09-25 reported `idleFrames=29 activeFrames=1` with
`elapsedMs` 3050 and 3052, and their captures matched the `axis_test` golden with 0 differing pixels. If the smoke
scene ever gains physics, point the idle scenario at the first golden scene without physics and update this section.

**Re-record once.** The fingerprint gained `idleThrottle`, `idleFrames` and `activeFrames`, and the manifest gained the
idle entry, so goldens recorded before the idle throttle must be re-recorded once with `-Record`: an older manifest
fails every fingerprint check (missing fields) and the idle check (`idle entry missing from manifest`).

## What this net does NOT catch

- Resize and `ResizeBuffers` paths: the window is never resized during a run.
- On-screen composition beyond the back buffer: what a DirectComposition commit or the DWM finally shows is not
  captured, only the swap chain's back buffer before Present.
- Exact frame pacing: `elapsedMs` only produces a coarse `WARN pacing` when it moves by more than a factor of three.
- The Release configuration: the net builds and runs the Debug player only.
- Different GPUs and drivers: the goldens are only valid on the machine that recorded them.
- UI and menu flows: the smoke scene is `axis_test`, and DemoDisc's menu and gameplay scenes are not built.

## Current record

The goldens were first recorded on 2026-09-24 from commit `f8a92cb`, and re-recorded on 2026-09-25 from commit
`d7defda` (feature/regression-safety-net) to add the host fingerprints, the executed-test counts and the provenance
hashes. They were re-recorded again on 2026-09-25 on the owner's development machine, from commit `b8e1197`
(feature/idle-throttle), because the fingerprint gained `idleThrottle`, `idleFrames` and `activeFrames` and the
manifest gained the idle entry. DemoDisc was at `5cc124eec06b8db729b2f9b15d99d26cb1ca8cc3` and helengine at
`dd9ca693403d91b5e2fc52860a3511e6358ed776` with uncommitted changes (`helengineDirty: true`).

- All 11 rendering scenes were stable across the two record runs (0 differing pixels), so every scene has a golden
  and no scene is marked `unstable`. The re-recorded golden PNGs are byte-identical to the first record.
- Every scene's fingerprint: `format=87 alpha=3 swapEffect=4 buffers=2 scaling=0 style=0x14CF0000
  exStyle=0x00000100 client=640x360 presentCount=30 presentFailures=0 frames=30 idleThrottle=off idleFrames=0
  activeFrames=30`, `elapsedMs` 119 to 128.
- The idle scenario on `axis_test` passed with `idleFrames=29 elapsedMs=3058`, and its capture matched the golden
  with 0 differing pixels.
- Baselines: `helengine.editor.tests` 49 failing of 3134 executed, `helengine.render.validation.tests` 1 failing of
  1 executed, `helengine.windows.builder.tests` 0 failing of 115 executed. The four known-flaky
  `SceneHierarchyPanelKeyboardFocusTests` arrow-key tests happened to fail during this record, so they entered the
  editor baseline (45 -> 49); they are also in the flaky list, and `-Verify` reports them as `FIXED` when they pass.
- DemoDisc's `user_settings\generated_code` holds only `obj` files, which the copy leaves out, so
  `generatedCodeHash` is the SHA-256 of an empty list.
- After the record, `-Verify` passed twice with every golden at 0 differing pixels, every fingerprint matching and
  the idle scenario passing. With the idle scenario's `--idle-fps 10` changed locally to 30, `-Verify` failed with
  `FAIL idle axis_test elapsedMs=1279 is below 2400`.

## How to run

```powershell
powershell -File scripts\run-regression.ps1 -Verify
```

The run builds the player (through `helengine\scripts\build-platform.ps1`), runs every scene through
`scripts\launch_in_emulator.ps1` (with `-Wait -TimeoutSeconds 120`), and runs the three test suites. It prints one
line per check (`PASS|FAIL|SKIP|WARN <kind> <name> <detail>`), then `RESULT: PASS` or `RESULT: FAIL (<n> failing)`.
Only `FAIL` lines count toward the result. It exits 0 only on PASS.

Optional parameters: `-HelengineRoot` (default `C:\dev\helworks\helengine`), `-ProjectSource` (default
`C:\dev\helprojs\demodisc`) and `-WorkRoot` (default `C:\dev\helworks\builds\helengine-windows\regression`). The
work root must not overlap the project source, the helengine checkout or this checkout. The script marks every work
root it uses with a `.helengine-regression-workroot` file and refuses a non-empty folder without that marker,
because it deletes and rewrites folders under the work root.

Useful outputs under the work root: `captures\verify\*.bmp`, `diffs\*.diff.png`, `trx\<suite>.trx`,
`trx\<suite>.log`, `record-staging\` (the last record) and `player\helengine_windows.startup.log`. When the player
exits with a non-zero code or times out, the script prints the last 20 lines of the startup log.

While the net runs:

- Do not use the keyboard or mouse on the player window; input changes what the scenes render.
- If you run it from the main checkout (not a worktree), close the editor first: the script rebuilds
  `builder\bin\Debug\net9.0\helengine.windows.builder.dll`, which the real `platforms.json` also points at.
- The net builds and runs the **Debug** player only.

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

- `WARN project changed since record: <old> -> <new>` when DemoDisc's HEAD has moved;
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
