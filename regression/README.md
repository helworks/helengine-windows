# Windows player regression net

`scripts\run-regression.ps1` builds **this checkout's** Windows player against an isolated copy of DemoDisc's
committed HEAD. It then checks that the player still renders what it rendered when the goldens were recorded, and
that the editor-side test suites have no new failures.

## What is checked

| Check | Scenes / suites | Passes when |
|---|---|---|
| `smoke` | The first rendering scene in build order (currently `axis_test`) | The player exits with code 0, writes a capture, and the capture is not blank. |
| `golden` | Every DemoDisc rendering scene in the committed windows scene package (`scenes/rendering/*.helen`), including the smoke scene | The capture matches `regression\golden\<scene>.png` within the thresholds below. |
| `suite` | `helengine.editor.tests`, `helengine.render.validation.tests` (both from `-HelengineRoot`), `helengine.windows.builder.tests` (this checkout) | The set of failing tests adds no test that is missing from `regression\baselines\<suite>.failing.txt`. Known failures stay tolerated; fixed ones are only reported. Tests in `<suite>.flaky.txt` only produce a `WARN` (see "Known flaky tests"). |
| `manifest` | `regression\golden\manifest.json` | It exists and was recorded with the same run settings. |

Every scene runs in a 640x360 window with `--frames 30 --fixed-delta 0.016666 --capture <bmp>`, so the captured
frame does not depend on wall-clock time.

## Thresholds

- A pixel **differs** when any of its B, G or R channels differs by more than 8 levels. Alpha is ignored because the
  swap chain uses `ALPHA_MODE_IGNORE`: the tool reads every capture as fully opaque, so goldens are opaque PNGs.
- A golden check **fails** when more than 0.1% of the pixels differ, or when the sizes differ. On failure, a diff
  image (differing pixels magenta, the rest greyscale) is written to `<WorkRoot>\diffs\<scene>.diff.png`.
- A frame is **blank** when one color covers at least 99.9% of its pixels.

Goldens are machine-local: they are only valid on the machine, GPU and driver that recorded them, and other GPUs or
drivers are not expected to match.

## Current record

The committed goldens and baselines were recorded on 2026-09-24 on the owner's development machine, from this
branch at commit `f8a92cb` (feature/regression-safety-net), with DemoDisc at `5cc124eec06b8db729b2f9b15d99d26cb1ca8cc3`
and helengine at `d98d00208a040dd00352a0b6417eed27d387af07` with uncommitted changes (`helengineDirty: true`).

- All 11 rendering scenes were stable across the two record runs (0 differing pixels), so every scene has a golden
  and no scene is marked `unstable`.
- Baselines: `helengine.editor.tests` 46 failing, `helengine.render.validation.tests` 1 failing,
  `helengine.windows.builder.tests` 0 failing.
- After the record, `-Verify` passed with every golden at 0 differing pixels on repeated runs.

## How to run

```powershell
powershell -File scripts\run-regression.ps1 -Verify
```

The run builds the player (through `helengine\scripts\build-platform.ps1`), runs every scene through
`scripts\launch_in_emulator.ps1`, and runs the three test suites. It prints one line per check
(`PASS|FAIL|SKIP|WARN <kind> <name> <detail>`), then `RESULT: PASS` or `RESULT: FAIL (<n> failing)`. Only `FAIL`
lines count toward the result. It exits 0 only on PASS.

Optional parameters: `-HelengineRoot` (default `C:\dev\helworks\helengine`), `-ProjectSource` (default
`C:\dev\helprojs\demodisc`) and `-WorkRoot` (default `C:\dev\helworks\builds\helengine-windows\regression`). The
work root must not overlap the project source, the helengine checkout or this checkout; the script refuses to run
if it does.

Useful outputs under the work root: `captures\verify\*.bmp`, `diffs\*.diff.png`, `trx\<suite>.trx`,
`trx\<suite>.log`, and `player\helengine_windows.startup.log`. When the player exits with a non-zero code, the
script prints the last 20 lines of the startup log.

## Provenance warnings

`manifest.json` records the provenance of the goldens and baselines: `projectSource`, `projectCommit` (DemoDisc's
HEAD), `helengineCommit` (`git rev-parse HEAD` of `-HelengineRoot`) and `helengineDirty` (whether
`git status --porcelain` was non-empty). `-Verify` prints:

- `WARN project changed since record: <old> -> <new>` when DemoDisc's HEAD has moved;
- `WARN helengine changed since record: ...` when the helengine commit or its dirty state differs.

A WARN does not fail the run, and every comparison still runs. It tells you that a failure may come from the
project or from helengine rather than from this checkout. Two different uncommitted helengine states both read as
`dirty=True`, so they do not produce a WARN.

## Re-recording (deliberately)

```powershell
powershell -File scripts\run-regression.ps1 -Record
```

Only re-record when a rendering change is **intended**, or when the WARN lines above explain a failure (DemoDisc or
helengine moved on purpose). Never re-record just to make a failing `-Verify` pass.

1. Run `-Verify` first and look at the `diffs\*.diff.png` images to confirm that the changes are the intended ones.
2. Run `-Record`. It runs every scene twice (`captures\a` and `captures\b`). A scene whose two captures do not match
   within the thresholds is marked `unstable` in the manifest and gets no golden; `-Verify` then prints
   `SKIP golden <scene> unstable` for it. Record writes the goldens and `manifest.json` (listing the unstable scenes)
   before it runs the test suites, then writes each suite's baseline after that suite, and still exits 0 when every
   build and run succeeded.
3. Run `-Verify` twice to confirm that the new goldens pass with no false failures.
4. Commit `regression\golden\*.png`, `regression\golden\manifest.json` and `regression\baselines\*.failing.txt`
   in a commit of their own, and say in the message why they changed.

## Known limitations

- The build goes through helengine's canonical build script, which regenerates core output under `helengine\tmp`.
  That folder is shared with normal builds, so do not run the regression net while another helengine build is
  running.
- The editor-side suites run against `-HelengineRoot` as it is on disk, including uncommitted work there. Their
  bin/obj output is written into that checkout as for any normal `dotnet test`.
- The net covers the Windows player host and DemoDisc's rendering scenes only; DemoDisc's menu and gameplay scenes
  are not built.
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
