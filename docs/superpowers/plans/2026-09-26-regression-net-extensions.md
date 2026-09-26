# Regression Net Extensions (3a) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the regression net see real click-through toggles, DPI and window rects, and per-window fingerprints, plus an overlay + idle scenario. Also prove the player builds against a helengine worktree.

**Architecture:**
- The player adds diagnostics, all in `--frames` / probe mode only:
  - probe runs call `Apply`;
  - `HIT_TEST` gains `exStyle`;
  - `HOST_FINGERPRINT` gains `window`, `dpi`, `dpiAwareness` and `windowRect`.
- The tool's fingerprint parser learns the new fields.
- The script keys fingerprints by window and stores one per probe for the overlay.
- A new `overlayIdle` scenario is added.
- A detached helengine worktree with submodules proves builds work from it.

**Tech Stack:** C++20/Win32 (helengine-windows), the C# tool, PowerShell, builder.tests.

**Spec:** `docs/superpowers/specs/2026-09-26-regression-net-extensions-design.md`

## Global Constraints

- **Where to work.** Worktree `C:\dev\helworks\helengine-windows\.worktrees\net-extensions`, branch `feature/net-extensions`. Never modify the main helengine-windows checkout (it holds the owner's uncommitted work), and never modify `C:\dev\helprojs\demodisc`.
- **helengine.** Never modify Helena's main helengine checkout: no tracked or untracked file changes, no checkout. Task 4 may add the detached worktree `C:\dev\helworks\helengine\.worktrees\regression-reference` and initialize submodules **inside that worktree only**.
- **Player behavior.** Without `--frames`, the player must behave exactly as before. The new diagnostics run only in `--frames` mode, and probe `Apply` runs only with `--hit-test-probe`.
- **AGENTS.md rules.**
  - One class per file.
  - `///` doc comments on every member, repeated on the .cpp definitions.
  - PascalCase fields, no tuples, no local helper functions.
  - Throw instead of defaulting.
  - Byte-cap all output.
- **Build and launch.** Build only via `scripts\run-regression.ps1 -BuildOnly` (run through `powershell -NoProfile -File … > log 2>&1`). Launch only via `scripts\launch_in_emulator.ps1` called in-process with `&`. Pass `-p:HelEngineRoot=C:\dev\helworks\helengine` to builder and builder.tests.
- **Field formats.**
  - `HIT_TEST x=<x> y=<y> alpha=<a> clickThrough=<on|off> exStyle=0x<8 uppercase hex>`
  - The fingerprint adds `window=main dpi=<n> dpiAwareness=<unaware|system|permonitor|permonitorv2|unknown> windowRect=<l>,<t>,<r>,<b>|default` after `windowMode`, before `elapsedMs`.
- **Manifest shape.** Each run keeps `fingerprints: { <window>: {fields…} }`. The overlay entry keeps `fingerprintsByProbe: { transparent: {window map}, opaque: {window map} }`. An old shape is a FAIL with "re-record required".

## Review Focus

- **The no-argument boot must stay unchanged.** It is checked in Task 5.
- **Normal-mode window position is not deterministic.** It must print `windowRect=default`, or Verify becomes flaky (Task 1 test).
- **Probe `Apply` must not run without `--hit-test-probe`.** The probe path only runs in `--frames` + overlay mode (Task 1 test).
- **Every existing golden stays byte-identical after the re-record** (Task 5).
- **The helengine worktree step must not touch the owner's main checkout.** `git -C C:\dev\helworks\helengine status` must be unchanged before and after (Task 4).

---

### Task 1: Player diagnostics

**Files:**
- `win32_overlay_hit_test_controller.hpp/.cpp`: in probe mode, call `ClickThroughController.Apply(alpha < 8)`, read `GWL_EXSTYLE`, and add `exStyle=0x…` to `DescribeProbeResult`, formatted `0x%08X`.
- `directx11_host_fingerprint.hpp/.cpp`: new fields per the Global Constraints.
  - DPI comes from `GetDpiForWindow`.
  - Awareness uses `GetWindowDpiAwarenessContext` + `GetAwarenessFromDpiAwarenessContext`, with v2 detected through `AreDpiAwarenessContextsEqual(ctx, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)`.
  - `windowRect` comes from `GetWindowRect` in overlay mode; normal mode prints `default`. Pass the window mode into `Describe`.
- `win32_application.cpp`: the call-site arguments only.
- `builder.tests`: extend the fingerprint and hit-test source tests.

**Steps:**
- [ ] TDD the source tests first:
  - the new fields in order;
  - `windowRect=default` in normal mode;
  - probe `Apply` present;
  - the `exStyle=` log field;
  - `Apply` still never called outside the probe branch or the non-probe cursor branch;
  - existing pins kept.
- [ ] Run the full builder.tests.
- [ ] Run `-BuildOnly`, then prove:
  - normal `--frames 5` shows `window=main dpi=96 dpiAwareness=unaware windowRect=default` (report the actual dpi value);
  - the overlay probe at `2,2` shows `clickThrough=on exStyle=0x002800A8`;
  - the overlay probe at `393,2` shows `clickThrough=off exStyle=0x00280088`;
  - a no-argument boot is unchanged.
- [ ] Commit: `feat(player): report dpi, window rect and real click-through toggles in diagnostics`.

### Task 2: Tool fingerprint fields

**Files:** `tools/regression/HostFingerprint.cs` (add the four compared fields) and its tests (`SampleLine`, the any-order test, and a new test that rejects an old line without the new fields). Update the comparer tests if they contain literal lines.

- [ ] TDD, then run the full tool tests.
- [ ] Commit: `feat(regression): parse window, dpi and window-rect fingerprint fields`.

### Task 3: Script: per-window fingerprints, per-probe overlay fingerprints, HIT_TEST exStyle, overlayIdle scenario

**Files:** `scripts/run-regression.ps1`, `builder.tests/RegressionScriptSourceTests.cs`, `regression/README.md`.

**Behavior:**
- **Fingerprints per window.** Collect all `HOST_FINGERPRINT` lines per run into a map keyed by the `window` field. A duplicate window tag is a FAIL. The manifest stores `fingerprints` as `{window: fields}`.
  - Verify compares the window sets exactly and the fields per window. The FAIL names the window.
  - The old `fingerprint` shape is a FAIL with "re-record required".
- **Overlay.** Record stores `fingerprintsByProbe.transparent` and `fingerprintsByProbe.opaque`, both window maps. Verify compares each probe run with its own entry.
  - The `HIT_TEST` match is exact, including `exStyle`.
  - Expected values: transparent is `clickThrough=on` with bit 0x20 set; opaque is `clickThrough=off` with the bit cleared. Record stores the exact `exStyle` values it observed after checking those bit rules.
- **overlayIdle.** A new function modeled on `Invoke-IdleScenario` plus `Invoke-OverlayScenario`. It uses the overlay arguments and the idle arguments with the recorded transparent probe.
  - Checks: `check-idle` (minimums from the manifest's `idle` entry), `check-premultiplied 0.01 0.01`, `compare-rgba` against the overlay golden, and `HIT_TEST` on plus the 0x20 bit.
  - Manifest entry: `kind='overlayIdle'` with `fingerprints` (the elapsed and idle counters are not compared exactly; only `check-idle`).
  - A missing entry is a FAIL.
- **README.** Document all of the above, the blind spot (live cursor motion), and the migration.

**Steps:**
- [ ] Write the source tests first.
- [ ] Implement.
- [ ] Run the source tests.
- [ ] Manually prove the new functions on the built player, without `-Record`: a probe run whose `HIT_TEST` includes `exStyle`, one overlayIdle run passing `check-idle`, and the per-window map built from a real log.
- [ ] Commit: `feat(regression): per-window fingerprints, per-probe overlay fingerprints and the overlay idle scenario`.

### Task 4: Build-from-helengine-worktree proof

1. Record `git -C C:\dev\helworks\helengine status --porcelain` (hash the output) and `rev-parse HEAD`.
2. Run `git -C C:\dev\helworks\helengine worktree add --detach .worktrees\regression-reference <HEAD>`. If that path already exists, stop and report.
3. Inside the new worktree only, run `git submodule update --init engine/vendor/csharpcodegen engine/vendor/bepuphysics2`. On failure, report BLOCKED with the error.
4. Run `run-regression.ps1 -BuildOnly -HelengineRoot C:\dev\helworks\helengine\.worktrees\regression-reference`. It must exit 0 and print `PLAYER=`. The build must read the platform manifest from the main checkout's user_settings, as today: the script's `platforms.json` source stays `-HelengineRoot`'s user_settings. If the worktree lacks `user_settings\platforms.json`, which is git-ignored, add a script parameter `-PlatformsManifestPath` whose default is `<HelengineRoot>\user_settings\platforms.json`, and pass the main checkout's file explicitly. That parameter is the only script change allowed in this task, and it needs a source test.
5. Run one `--frames 30` capture of axis_test with that player. `compare` against the golden must give PASS 0.
6. Check that the main checkout's `status --porcelain` hash and HEAD are unchanged.
7. Add a README section "Building against a helengine worktree" with the exact commands.

- [ ] Commit: `docs(regression): build the player against a helengine worktree`. Include the parameter if it was needed.

### Task 5: Acceptance

- [ ] Run `-Record`, which is deliberate because the schema changed. Then `git diff --stat -- regression/golden/*.png` must show no change. The manifest must show only the new fields, the new per-window and per-probe shapes, the overlayIdle entry, and `elapsedMs` jitter.
- [ ] Run `-Verify`, which must PASS. Run it again, which must PASS again.
- [ ] **Negative check.** Locally edit the manifest's transparent probe expectation (flip its recorded `exStyle` to the opaque value), run Verify and confirm it FAILs on `HIT_TEST`. Then `git checkout` and confirm Verify PASSes.
- [ ] No-argument boot: the player is alive after 5 s, loads the default scene, and logs no `HOST_FINGERPRINT` or `HIT_TEST` lines.
- [ ] Commit: `test(regression): re-record with per-window fingerprints, dpi fields and the overlay idle scenario`.
