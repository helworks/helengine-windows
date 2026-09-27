# Opt-in Per-Monitor DPI Awareness (3b) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The Windows player can opt into Per-Monitor v2 DPI awareness (`--dpi-awareness permonitorv2` or profile `dpiAwareness`). Profile resolution then means physical pixels. The default path stays byte-identical.

**Architecture:**
- An enum, a names class, a flag, an optional profile field and a settings resolver follow the `windowMode` pattern exactly.
- `CreateMainWindow` calls `SetProcessDpiAwarenessContext` before any monitor query or HWND, only when PerMonitorV2 is resolved.
- `Win32DpiWindowSizing` holds the sizing math.
- `Win32Window` corrects its outer size after creation and handles `WM_DPICHANGED`, both only when aware.
- The regression net gains a `dpiAware` scenario.

**Tech Stack:** C++20/Win32 (helengine-windows host), PowerShell regression script, C# builder.tests source tests, and the C# regression tool.

**Spec:** `docs/superpowers/specs/2026-09-27-per-monitor-dpi-design.md`

## Global Constraints

- **Where to work.** Worktree `C:\dev\helworks\helengine-windows\.worktrees\per-monitor-dpi`, branch `feature/per-monitor-dpi` (base main 37e0360). Never modify:
  - the main helengine-windows checkout (it has the owner's uncommitted builder files);
  - `C:\dev\helworks\helengine` (main checkout or worktrees);
  - `C:\dev\helprojs\demodisc`.
- **Behavior without the option.** With no `--dpi-awareness` and no profile `dpiAwareness`, the player makes exactly today's Win32 calls:
  - no `SetProcessDpiAwarenessContext`;
  - no `GetDpiForWindow` or `SetWindowPos` in window creation;
  - no new log lines;
  - profile.json read and rewritten byte-identically.
- **Accepted values.** Exactly `unaware` and `permonitorv2`, case-sensitive. The command line wins over the profile. The default is `unaware`.
- **Errors.**
  - An invalid or duplicate `--dpi-awareness` is an invalid argument, exiting with code 2 through `Win32ExitRequest`, like `--window-mode`.
  - An invalid profile `dpiAwareness` is a `RuntimePlayerProfileConfigurationError`, like `windowMode`.
  - A `SetProcessDpiAwarenessContext` failure throws `std::runtime_error` naming the call and `GetLastError()`.
- **Sizing semantics.** With PerMonitorV2, the profile resolution is the client size in physical pixels.
  - `WM_DPICHANGED`, normal window: move to the suggested top-left and keep the client pixel size.
  - `WM_DPICHANGED`, overlay: keep the rectangle.
  - The handler returns 0 in both cases.
- **AGENTS.md rules.**
  - One class per file.
  - `///` XML-style doc comments on every member, repeated on the .cpp definitions.
  - PascalCase fields, no tuples (use `SIZE`/`RECT` or a named struct), no local helper functions or lambdas-as-helpers.
  - Throw instead of defaulting.
  - Opening braces on the same line.
- **Build and launch.**
  - Build only via `powershell -NoProfile -File scripts\run-regression.ps1 -BuildOnly -ProjectCommit 5cc124eec06b8db729b2f9b15d99d26cb1ca8cc3 > <log> 2>&1`.
  - Launch only via `scripts\launch_in_emulator.ps1` called in-process with `&`.
  - Write `profile.json` `{"resolutionWidth":640,"resolutionHeight":360}` next to the player before manual runs if it is missing.
  - Run dotnet from inside the worktree, with `-p:HelEngineRoot=C:\dev\helworks\helengine` for builder.tests.
- **Safety.** Never move the mouse, take screenshots, or change display scaling.

## Review Focus

1. **A profile.json without `dpiAwareness` must stay byte-identical**, on both the load path and the repair (rewrite) path. Task 1 test: `WriteProfile` emits the field only when `DpiAwarenessFieldPresent`.
2. **Awareness must be applied before the first monitor query or HWND.** `ResolveOverlayRectangle`'s `GetMonitorInfoW` must run after `SetProcessDpiAwarenessContext`, or the overlay gets virtualized bounds on scaled displays. Task 2 test: pin the order inside `CreateMainWindow`.
3. **The unaware creation path must not gain calls.** `CreateNormalWindow` and `CreateOverlayWindow` stay byte-identical. The post-create correction runs only when aware. Task 2 test.
4. **A normal-mode `WM_DPICHANGED` must keep the client pixel size.** The new outer size must use the *new* DPI from `HIWORD(wParam)`, the suggested rectangle's top-left, and `Win32DpiWindowSizing::OuterSizeForClient`. Task 2 test.
5. **Verify must not silently pass a skipped `dpiAware` check.** A missing entry, an old manifest without it, or a run reporting `dpiAwareness` other than `permonitorv2` are all FAILs, in both Record and Verify. Task 3 tests.

---

### Task 1: Configuration (enum, names, flag, profile field, resolver)

**Files:**
- Create `src/platform/windows/win32/win32_dpi_awareness.hpp`: `enum class Win32DpiAwareness { Unaware, PerMonitorV2 };` with a doc comment.
- Create `win32_dpi_awareness_names.hpp/.cpp`: class `Win32DpiAwarenessNames` with `static bool TryParse(const std::string& text, Win32DpiAwareness& dpiAwareness)` (exactly "unaware" / "permonitorv2"; leaves the out-param unchanged on failure) and `static const char* ToText(Win32DpiAwareness dpiAwareness)`. Model it on `Win32WindowModeNames`.
- Create `win32_dpi_awareness_settings.hpp/.cpp`: class `Win32DpiAwarenessSettings`.
  - `static Win32DpiAwarenessSettings Resolve(const RuntimePlayerProfile& profile, const Win32CommandLineOptions& commandLineOptions)`: the command line wins; otherwise the profile value if present; otherwise `Unaware`. An unparsable profile value throws `std::invalid_argument`, as `Win32WindowModeSettings` does.
  - `Win32DpiAwareness GetDpiAwareness() const`.
  - `bool IsPerMonitorV2() const`.
  - `std::string Describe() const`, returning `dpiAwareness=<text> source=<profile|commandLine|default>`.
- Modify `win32_command_line_options.hpp/.cpp`:
  - `--dpi-awareness <value>` with fields `DpiAwarenessSupplied` and `DpiAwareness`;
  - `bool HasDpiAwareness() const` and `Win32DpiAwareness GetDpiAwareness() const`;
  - a duplicate flag throws `std::invalid_argument("Command-line flag --dpi-awareness was given more than once.")`;
  - a bad value throws `std::invalid_argument("Command-line flag --dpi-awareness requires \"unaware\" or \"permonitorv2\"...")`.
  - Update the Parse doc comment's flag list.
- Modify `runtime/runtime_player_profile.hpp`: `std::string DpiAwareness = "unaware";` and `bool DpiAwarenessFieldPresent = false;`, both documented.
- Modify `runtime/runtime_player_profile_loader.cpp`:
  - `TryParseOptionalString(fileContents, "dpiAwareness", …)` sets the present flag;
  - a `ValidateDpiAwarenessField` member, declared in the .hpp, throws `RuntimePlayerProfileConfigurationError` on an unparsable value;
  - the load path and the repair path copy the value and flag exactly like the window-mode fields;
  - `WriteProfile` emits `"dpiAwareness": "<v>"` only when `DpiAwarenessFieldPresent`, placed after the window-mode fields and with correct comma handling.
  - Read `WriteProfile` first to match its comma logic.
- Modify `CMakeLists.txt` if sources are listed explicitly (check `HELENGINE_WINDOWS_SOURCES`).
- Tests: extend `builder.tests/Win32CommandLineOptionsSourceTests.cs` and `RuntimePlayerProfileSourceTests.cs`, and add `builder.tests/Win32DpiAwarenessSettingsSourceTests.cs`. Follow the existing source-test style: read the source text and assert on its structure.

**Interfaces produced:** `Win32DpiAwareness`, `Win32DpiAwarenessNames::TryParse/ToText`, `Win32DpiAwarenessSettings::Resolve/GetDpiAwareness/IsPerMonitorV2/Describe`, `Win32CommandLineOptions::HasDpiAwareness/GetDpiAwareness`, `RuntimePlayerProfile::DpiAwareness/DpiAwarenessFieldPresent`.

- [ ] **Write the source tests first.** Cover:
  - the enum values;
  - the names table with exact strings;
  - the flag parse, the duplicate check and the bad-value message;
  - the profile field parse, validation, load and repair copying;
  - `WriteProfile` emitting the field only under `DpiAwarenessFieldPresent` (Review Focus 1);
  - Resolve precedence, including the default `Unaware`;
  - `Describe` sources.
- [ ] Run them and see them fail.
- [ ] Implement.
- [ ] Run the full builder.tests: all green, with the count reported.
- [ ] Run `-BuildOnly` (exit 0, `PLAYER=`), then prove:
  - `--frames 5 --dpi-awareness bogus` exits with code 2 and its log names the flag;
  - a no-argument boot is unchanged (alive after 5 s, default scene, no new log lines);
  - with no profile field, profile.json is byte-identical before and after a run (hash it).
- [ ] Commit: `feat(player): parse the opt-in dpiAwareness setting from the command line and profile`.

### Task 2: Apply awareness, window sizing and `WM_DPICHANGED`

**Files:**
- Create `src/platform/windows/win32/win32_dpi_window_sizing.hpp/.cpp`: class `Win32DpiWindowSizing`, static methods only.
  - `static SIZE OuterSizeForClient(int clientWidth, int clientHeight, DWORD style, DWORD exStyle, UINT dpi)`: `AdjustWindowRectExForDpi` on `{0,0,w,h}`, throwing `std::runtime_error` if it fails.
  - `static RECT PlacementForDpiChange(const RECT& suggestedRectangle, int clientWidth, int clientHeight, DWORD style, DWORD exStyle, UINT newDpi)`: returns the suggested left and top plus the `OuterSizeForClient` size.
- Modify `win32_window.hpp/.cpp`:
  - Add a `Win32DpiAwareness DpiAwareness` field, set through a new `void SetDpiAwareness(Win32DpiAwareness dpiAwareness)` called before `Create()`, default `Unaware`.
  - **After creation**, only when PerMonitorV2 and in normal mode: compute `OuterSizeForClient(Width, Height, WS_OVERLAPPEDWINDOW, 0, GetDpiForWindow(Handle))`, compare it with the current `GetWindowRect` size, and only if different call `SetWindowPos(Handle, nullptr, 0, 0, cx, cy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE)`, then `RefreshClientSize()`.
  - Put this in a new member `CorrectNormalWindowSizeForDpi()` called from `Create()`. `CreateNormalWindow` and `CreateOverlayWindow` bodies must stay byte-identical.
  - **`HandleMessage` `case WM_DPICHANGED`:**
    - normal mode: `RECT placement = Win32DpiWindowSizing::PlacementForDpiChange(*reinterpret_cast<RECT*>(lParam), GetClientWidth(), GetClientHeight(), WS_OVERLAPPEDWINDOW, 0, HIWORD(wParam));`, then `SetWindowPos(Handle, nullptr, placement.left, placement.top, width, height, SWP_NOZORDER | SWP_NOACTIVATE)`;
    - overlay: do nothing;
    - return 0 in both modes.
    - The `ActivityTracker->ObserveMessage` call at the top already sees it (the tracker handles `WM_DPICHANGED` as activity).
- Modify `win32_application.cpp/.hpp`, in `CreateMainWindow`:
  - Immediately after `ResolveRuntimePlayerProfile()` and `ResolveWindowModeSettings(profile)`, and **before** `ResolveOverlayRectangle` and any `Win32Window` creation, call a new member `ApplyDpiAwareness(const Win32DpiAwarenessSettings&)`. It calls `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)` only when `IsPerMonitorV2()`, throws on failure, and logs `"DPI awareness configured: " + Describe()`.
  - Resolve through a `ResolveDpiAwarenessSettings(profile)` member that converts `std::invalid_argument` into `Win32ExitRequest(2, …)`, like `ResolveWindowModeSettings`.
  - Pass the value to `MainWindow->SetDpiAwareness(...)` before `Create()`.
- Tests: add `builder.tests/Win32DpiAwarenessApplySourceTests.cs`, or extend `Win32OverlayWindowSourceTests.cs`, following the style of the existing tests.

**Interfaces consumed:** Task 1's `Win32DpiAwarenessSettings`. **Produced:** `Win32DpiWindowSizing::OuterSizeForClient/PlacementForDpiChange`, `Win32Window::SetDpiAwareness`.

- [ ] **Write the source tests first.** Cover:
  - `SetProcessDpiAwarenessContext` appears exactly once, inside the `IsPerMonitorV2()` branch of `ApplyDpiAwareness`, and it throws on failure with `GetLastError`;
  - in `CreateMainWindow`, `ApplyDpiAwareness(` comes before `ResolveOverlayRectangle(` and before the first `std::make_unique<Win32Window>` (Review Focus 2);
  - the `CreateNormalWindow` and `CreateOverlayWindow` bodies equal their pre-change text, pinned as literal expected bodies (Review Focus 3);
  - `CorrectNormalWindowSizeForDpi` is guarded by PerMonitorV2 and normal mode, and uses `GetDpiForWindow` and `SWP_NOMOVE`;
  - the `WM_DPICHANGED` case uses `HIWORD(wParam)`, `PlacementForDpiChange`, the overlay no-op and `return 0` (Review Focus 4);
  - `OuterSizeForClient` uses `AdjustWindowRectExForDpi` and throws on failure.
- [ ] Run them and see them fail.
- [ ] Implement.
- [ ] Run the full builder.tests.
- [ ] Run `-BuildOnly`, then prove each of the following with log excerpts in the report:
  - `--frames 30 --fixed-delta 0.016666 --capture <p> --scene axis_test --dpi-awareness permonitorv2` has a fingerprint with `dpiAwareness=permonitorv2 dpi=96 client=640x360`, and `compare` against `regression/golden/axis_test.png` gives PASS 0;
  - the same run without the flag has `dpiAwareness=unaware`;
  - overlay (`--window-mode overlay --overlay-bounds profile --overlay-background transparent --hit-test-probe 2,2`) with `--dpi-awareness permonitorv2` has `dpiAwareness=permonitorv2 windowRect=0,0,640,360` and `HIT_TEST … clickThrough=on exStyle=0x002800A8`;
  - a no-argument boot is unchanged.
- [ ] Commit: `feat(player): apply opt-in per-monitor v2 DPI awareness and keep client pixels across DPI changes`.

### Task 3: Regression net `dpiAware` scenario and README

**Files:** `scripts/run-regression.ps1`, `builder.tests/RegressionScriptSourceTests.cs`, `regression/README.md`. The tool already parses `dpiAwareness`, so no tool change is expected; confirm this.

**Behavior:**
- A new manifest entry with `kind='dpiAware'` holds two runs:
  - **`normal`**, on the smoke scene: `--dpi-awareness permonitorv2 --frames 30 --fixed-delta 0.016666 --capture`.
    - `compare` against the existing smoke-scene golden (not a new golden).
    - A per-window `fingerprints` map, compared like the rendering scenes.
  - **`overlay`**: the overlay scenario's arguments plus `--dpi-awareness permonitorv2`, using the recorded transparent probe.
    - `compare-rgba` against the overlay golden and `check-premultiplied 0.01 0.01`.
    - An exact `HIT_TEST` match against the overlay entry's recorded transparent line and exStyle.
    - A per-window `fingerprints` map.
- **Record** refuses (FAILs) if any window's `dpiAwareness` is not `permonitorv2`.
- **Verify** FAILs in these cases:
  - the entry is missing ("re-record required");
  - either run is missing;
  - a `dpiAwareness` is not `permonitorv2`;
  - a fingerprint mismatch per window, where the FAIL names the run and the window.
- Reuse the existing functions: `Invoke-OverlayProbeRun`, `Test-CaptureMatchesGolden`, the per-window fingerprint map builder and `Test-OverlayFingerprintShape` (or a sibling with `-Kind dpiAware`). Do not duplicate them.
- **README:** document the scenario, the blind spot (dpi ≠ 96, `WM_DPICHANGED` and cross-monitor moves cannot be exercised on the owner's single 96-dpi monitor) and the manual check for a scaled display:
  - run with `--dpi-awareness permonitorv2 --frames 30 --capture …`;
  - expect the fingerprint `dpi` to equal the monitor DPI, `client=` to equal the profile resolution, and the capture to be the profile resolution.
  - Also document the new flag and profile field.

- [ ] **Write the source tests first** (Review Focus 5).
- [ ] Implement.
- [ ] Run the source tests and the full builder.tests.
- [ ] **Manual proof on the built player, without `-Record`** (and without `-Verify`, because the committed manifest has no `dpiAware` entry yet):
  - drive the new function(s) once from a small script, or dot-source them if the script supports that (otherwise explain the method used);
  - show both runs passing their checks, and a forced FAIL when `dpiAwareness=unaware` is fed in.
- [ ] Commit: `feat(regression): add the dpiAware scenario for opt-in per-monitor DPI`.

### Task 4: Acceptance

- [ ] Report `FreePhysicalMemory`.
- [ ] Run `-Record -ProjectCommit 5cc124eec06b8db729b2f9b15d99d26cb1ca8cc3` with the default `-HelengineRoot`.
  - `git diff --stat -- regression/golden/*.png` must be empty. If not, restore it and report BLOCKED.
  - The manifest diff may only contain the new `dpiAware` entry and `elapsedMs` or idle-counter jitter.
  - Baseline changes must be explained: the builder.tests executed count rises by the number of new tests; any flaky-list movement must be named.
- [ ] Run `-Verify` twice; both must PASS.
- [ ] Commit the re-record: `test(regression): re-record with the dpiAware scenario`.
- [ ] **Negative check.** Edit the committed manifest's `dpiAware` normal-run fingerprint `dpiAwareness` to `unaware`, run Verify, and confirm it FAILs naming `dpiAware`. Then `git checkout -- regression/golden/manifest.json`, confirm `git status` is clean, and confirm Verify PASSes once more.
- [ ] **No-argument boot.** The player is alive after 5 s, the default scene loads, the log has no `HOST_FINGERPRINT`, `HIT_TEST` or `DPI awareness configured` lines, and the process is terminated afterwards.
