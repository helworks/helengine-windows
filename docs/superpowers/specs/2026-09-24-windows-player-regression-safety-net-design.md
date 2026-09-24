# Windows Player Regression Safety Net — Design

Date: 2026-09-24
Status: Draft for review (Helena)
Series: subproject 0 of 4 (0 safety net → 1 render on demand → 2 transparent DirectComposition windows → 3 per-monitor DPI + multi-window). Golden rule for the whole series: every change is additive and opt-in; default player and editor behavior must stay identical, and this safety net is how we prove it.

## Goal

Detect any regression in the native Windows player (`helengine_windows.exe`) and in the editor's renderer tests before a change is merged. Today nothing does: `builder.tests` only asserts source text, and `helengine.render.validation` never runs the native player.

## Decisions already made (with Helena)

- Check style **C**: a smoke run of the bootstrap scene, plus golden-image comparison for the static rendering micro-scenes, plus the existing editor test suites.
- Delivery: one PowerShell script with `-Record` and `-Verify` modes.
- Scene selection: the script works on a **copy** of `helengine\test-project`; Helena's project is never modified. The player gets an opt-in `--scene` flag.

## Non-goals

- CI integration, non-Windows platforms, Vulkan.
- Pixel-exact determinism across different GPUs or drivers. Goldens are machine-local and are recorded on Helena's machine.
- Any change to `helengine` core, editor or codegen. This subproject touches only `helengine-windows`.

## 1. Player command-line options (opt-in)

A new `Win32CommandLineOptions` class parses `CommandLineToArgvW(GetCommandLineW())` once at startup. `Win32Application` reads it. **With no arguments, every code path is identical to today's**: the same first catalog scene, an endless loop, wall-clock `Core.Update()`, and no capture.

| Flag | Effect |
|---|---|
| `--scene <sceneId>` | Loads the catalog entry whose id equals `<sceneId>` (for example `Scenes/rendering/opaque-basics.helen`) instead of `catalogEntries[0]`. An unknown id writes an error to the log and exits with code 2. |
| `--frames <N>` | Runs exactly N iterations of the frame loop (`N >= 1`), then exits cleanly with code 0. |
| `--fixed-delta <seconds>` | Calls `EngineCore->Update(seconds)` (the existing `Core.Update(double)` overload) instead of the wall-clock `Update()`, which makes the frames deterministic. It must be greater than 0. |
| `--capture <path>` | After the last frame's Draw and before its Present, copies the back buffer to a staging texture and writes a 32-bit BMP to `<path>`. Requires `--frames`. |

- Invalid or unknown flags write a clear error to the log and exit with code 2. They are never silently ignored.
- A capture or I/O failure exits with code 3.

## 2. Back-buffer capture

- A new `DirectX11BackBufferCapture` class does this: `CopyResource` from the swap chain's back buffer (B8G8R8A8) into a CPU-readable staging texture, `Map`, then hand the rows to a new `BmpImageWriter` class (a top-down 32-bit BMP with no compression and no external library).
- Both classes are only constructed when `--capture` is given.

## 3. Regression tool (C#)

This is a new console project, `tools/regression/helengine.windows.regression` (net9.0-windows), with unit tests in `tools/regression.tests`. It contains:

- `BmpImageReader`, which reads the player's BMP.
- `PngGoldenStore`, which saves and loads goldens as PNG through System.Drawing so the repository stays small.
- `ImageComparer`. A pixel **differs** when any channel differs by more than 8 levels. A scene **fails** when more than 0.1% of its pixels differ, or when the sizes differ. The comparer writes a diff PNG in which differing pixels are magenta and the rest are greyscale.
- `BlankFrameDetector` (for the smoke check). A frame is "blank" if 99.9% of its pixels share one color.
- `TestResultSetComparer`. It reads `dotnet test` TRX files and compares the set of failing test names with a recorded baseline set.

## 4. The script: `scripts/run-regression.ps1`

Parameters:
- `-Record` or `-Verify` (exactly one is required).
- `-HelengineRoot`, default `C:\dev\helworks\helengine`.
- `-WorkRoot`, default `C:\dev\helworks\builds\helengine-windows\regression`. This follows the `AGENTS.md` rule that build outputs never go to %TEMP%.

Steps:
1. **Prepare the project copy.** Mirror `helengine\test-project` into `<WorkRoot>\project`. Write that copy's `user_settings\build_config.json` so the windows platform packages `Scenes/Bootstrap.helen` plus every `Scenes/rendering/*.helen` scene, with Bootstrap first. Write its player profile at a fixed 640×360 window.
2. **Build** through the canonical `helengine\scripts\build-platform.ps1 -Project <copy>\project.heproj -Platform windows -Output <WorkRoot>\player -Configuration Debug`. This follows the `build-platform-builds` skill. No direct CMake or dotnet publish calls.
3. **Run each scene** through `scripts\launch_in_emulator.ps1`, which gains optional `-ArgumentList` and `-Wait` parameters. Its current behavior without those parameters is unchanged. Each run passes `--scene <id> --frames 30 --fixed-delta 0.016666 --capture <WorkRoot>\captures\<scene>.bmp` and requires exit code 0.
   - **Smoke check (Bootstrap):** exit code 0, a capture exists, and the frame is not blank.
   - **Golden check (rendering scenes):** compare against `regression\golden\<scene>.png`.
4. **Run the editor-side suites** with `dotnet test` and a TRX logger: `helengine\engine\helengine.editor.tests`, `helengine\engine\helengine.render.validation.tests`, and `helengine-windows\builder.tests`. Compare each suite's failing set with the recorded baseline, so known failures stay tolerated and new ones fail.
5. **`-Record`:**
   - It runs every scene **twice**. A scene whose two captures fail the comparer is excluded as unstable, recorded as such, and reported. It never silently becomes a golden.
   - It then writes `regression\golden\*.png`, `regression\golden\manifest.json` (scene list, frame count, fixed delta, window size, and the stable/unstable status of each scene), and `regression\baselines\<suite>.failing.txt`.
   - The goldens and baselines are committed.
6. **`-Verify`:**
   - It prints one line per check (PASS/FAIL, the percentage of differing pixels, and the diff image path) and a final summary.
   - It exits 0 only if every check passes.

## 5. Build isolation (the script always builds its own checkout)

Facts found while planning:
- The editor CLI reads platform installations from the **main** helengine checkout's `user_settings\platforms.json`, even when it runs from a worktree. The only exception is when the env var `HELENGINE_ENGINE_USER_SETTINGS_ROOT` is set (`EditorSourceBuildWorkspaceLocator.cs:198-206`).
- Relative paths in that file resolve against the file's own folder. Absolute paths are used as-is.
- The project's `requiredEngineVersion` must equal the entry's `engineVersion` exactly. test-project currently declares a different version from the main manifest.
- `builder` and `builder.tests` need `-p:HelEngineRoot=<helengine>` when they are built from a worktree. `builderAssemblyPath` points at a prebuilt builder DLL.

Therefore the script, and never Helena's files, does the following:
1. It builds `<this checkout>\builder` with `-p:HelEngineRoot=<HelengineRoot>`.
2. It writes `<WorkRoot>\engine-user-settings\platforms.json`, a copy of `<HelengineRoot>\user_settings\platforms.json` in which the windows entry's `builderAssemblyPath` and `playerSourceRootPath` are **absolute paths into the checkout the script lives in** (`$PSScriptRoot\..`).
3. It sets `HELENGINE_ENGINE_USER_SETTINGS_ROOT` to that folder, for the build process only.
4. It rewrites the **copy's** `project.heproj` `requiredEngineVersion` to the windows entry's `engineVersion`.

Consequences:
- No helengine worktree is needed, and helengine is never modified. `-HelengineRoot` (default `C:\dev\helworks\helengine`) supplies the editor, `build-platform.ps1` and `test-project`.
- The script has a `-BuildOnly` switch, used while developing the native changes, and it prints the resolved `playerSourceRootPath` so a reader can see which checkout was built.
- Every `builder`/`builder.tests` build and test run passes `-p:HelEngineRoot`.
- `launch_in_emulator.ps1` keeps the strings its existing tests assert (`[string]$ArtifactPath`, `.exe`, `Start-Process -FilePath $resolvedArtifactPath`).

**Window size:** the copy's `build_config.json` sets `selectedGraphicsOptionValues` `default-width` 640 and `default-height` 360. The script also writes `<WorkRoot>\player\profile.json` (`{"resolutionWidth":640,"resolutionHeight":360}`) before every run, because a `profile.json` left from an earlier run would otherwise win.

**Other details:**
- The copy also includes `user_settings\generated_code`.
- `--frames` counts only frames that actually render, because RenderFrame skips minimized or zero-size frames.
- Captures ignore the alpha channel: the swap chain uses `ALPHA_MODE_IGNORE`, so alpha is undefined.

## 6. Order of work and safety

1. Build the regression tool with unit tests (pure C#, no player).
2. Add the player flags and the capture, then run the unmodified player path (no arguments) manually once to confirm it starts as before. The `builder.tests` source assertions must still pass.
3. Write the script. Run `-Record` on the **unchanged** `main` scene behavior, then run `-Verify` twice to prove the net is stable (zero false failures).
4. Merge only after `-Verify` passes on the worktree build.

## 7. Testing

- **Unit tests** for the comparer (identical images, one-pixel difference, threshold edge cases, size mismatch), the blank detector, the BMP reader round-trip against the writer's format, the TRX parser, the failing-set comparison, and the build-config writer.
- **`builder.tests` source-text tests** for the new C++ classes, in the style of the existing ones: the flag names are present, and the capture is only constructed when the flag is set.
- **End to end:** `-Record` followed by two `-Verify` runs, all passing, is this subproject's acceptance test.

## Revision 1 (2026-09-24): test project changed to DemoDisc

`helengine\test-project` cannot be built at the current helengine `main`. All of its scenes are asset format 24, while the editor now requires format 25 (`EditorAssetBinarySerializer.CurrentVersion = 25`). Helena chose to use DemoDisc instead. This replaces every `test-project` reference above:

- **Source.** The source project is `C:\dev\helprojs\demodisc` at its **committed HEAD**, not its working tree, which contains Helena's uncommitted work.
  - The script extracts `git -C <demodisc> archive HEAD` into `<WorkRoot>\project`, then copies the git-ignored `user_settings\` folder (and `user_settings\generated_code`) from the working tree.
  - Only the copy is ever written. DemoDisc itself is never modified.
  - If DemoDisc stores assets in Git LFS (it has `filter=lfs` in `.gitattributes`), stop and report, because `git archive` would export pointer files.
- **Parameters.** The script gets `-ProjectSource` (default `C:\dev\helprojs\demodisc`). `-HelengineRoot` is still used for the editor, `build-platform.ps1`, the platforms manifest and the editor test suites.
- **Smoke scene.** `DemoDiscMainMenu.helen` replaces Bootstrap and is ordered first.
- **Golden scenes.** Every scene in `assets\scenes\rendering\*.helen`. Scene ids use DemoDisc's own id format; the script derives it the same way DemoDisc's existing `build_config.json` entries for other platforms spell their ids (project-relative path under `assets\`, forward slashes, original casing).
- **Engine version.** DemoDisc's `requiredEngineVersion` already matches the main manifest. The script still enforces the equality by copying the windows entry's `engineVersion` into the copy, which is harmless when they are already equal.
- **Manifest provenance.** `manifest.json` also records `projectSource` and `projectCommit` (the DemoDisc HEAD). `-Verify` prints a WARN line when DemoDisc's HEAD differs from the recorded commit. Goldens are then stale by design, and re-recording is a deliberate act documented in `regression\README.md`.
