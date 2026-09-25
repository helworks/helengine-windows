# Windows Player Idle Throttle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** When idle throttling is enabled (off by default), the Windows player drops to a low frame rate and sleeps between frames while idle. Input, window activity and keep-awake conditions bring it back to full rate at once.

**Architecture:** The host changes, and helengine does not:
- optional profile fields and CLI flags feed a `Win32IdleThrottleSettings`;
- a `Win32ActivityTracker` is attached to `Win32Window` only when throttling is enabled;
- a pure `Win32IdleFramePacer` decides between active and idle;
- an opt-in loop waits with `MsgWaitForMultipleObjectsEx`;
- the fingerprint gains idle counters;
- the regression net gains an idle scenario.

**Tech Stack:** C++20/Win32 (helengine-windows `src`), C# tool (`tools/regression`), PowerShell (`scripts/run-regression.ps1`), and builder.tests source tests.

**Spec:** `docs/superpowers/specs/2026-09-25-windows-player-idle-throttle-design.md`, including Revision 1.

## Global Constraints

- **Scope.** Only this repo changes, in the worktree `C:\dev\helworks\helengine-windows\.worktrees\idle-throttle` on branch `feature/idle-throttle`. Never modify `C:\dev\helworks\helengine`, `C:\dev\helprojs\demodisc`, or the main checkout `C:\dev\helworks\helengine-windows`, which holds the owner's uncommitted work.
- **Default path unchanged.** With throttling disabled (no profile fields, no idle flags):
  - the loop text is exactly `while (PumpMessages()) {` / `RenderFrame();`;
  - no tracker is attached;
  - no pacer is constructed;
  - the WndProc does one null check and otherwise returns exactly what it returns today.
- **AGENTS.md:**
  - one class per file, with `///` docs on every member, repeated on the .cpp definitions;
  - PascalCase fields;
  - no tuples (use small structs);
  - no local helpers;
  - throw instead of defaulting;
  - nullable disabled;
  - byte-capped command output.
- **Build and launch.**
  - Build only through `scripts\run-regression.ps1 -BuildOnly`.
  - Launch only through `scripts\launch_in_emulator.ps1`, called in-process with `&`. It supports `-ArgumentList`, `-Wait` and `-TimeoutSeconds`.
  - `dotnet` for `builder`/`builder.tests` runs with `-p:HelEngineRoot=C:\dev\helworks\helengine`.
  - Run dotnet from inside the worktree.
- **Values:**
  - `idleAfterMilliseconds` > 0, default 500;
  - `idleFramesPerSecond` 1..30, default 10;
  - an invalid idle configuration exits with code 2.
- **Profile field names:** `idleThrottleEnabled`, `idleAfterMilliseconds`, `idleFramesPerSecond`.
- **CLI flags:** `--idle-throttle on|off`, `--idle-after-ms <n>`, `--idle-fps <n>`. These are known flags, so their presence triggers strict validation.
- **Fingerprint fields added:** `idleThrottle=<on|off>`, `idleFrames=<n>`, `activeFrames=<n>`.
- **Idle scenario:** smoke scene, `--idle-throttle on --idle-after-ms 0 --idle-fps 10 --frames 30 --fixed-delta 0.016666 --capture`. It must satisfy all three:
  - the capture equals the golden;
  - `idleFrames` is at least 25;
  - `elapsedMs` is at least 2400.

## Review Focus

- **No idle config at all.** The player must behave byte-identically. Tasks 1 and 3 add source tests for this (default loop text; tracker only attached when enabled). Task 6 covers it with `-Verify` and a no-argument boot.
- **A `profile.json` with an invalid idle field.** It must exit with code 2 and a message. It must never be silently rewritten. Covered in Task 1 (dedicated exception outside the repair catch) and Task 3 (manual proof).
- **A minimized window with throttling on.** The loop must not spin. Covered in Task 3: the timed wait still happens when `RenderFrame` returns early.
- **Physics scenes never idle.** This is documented and expected. Task 5 verifies that the smoke scene `axis_test` has no physics runtime. If it does, pick the first golden scene without physics, and document the choice.
- **An old manifest without the idle fields.** Verify fails to parse it, so a re-record is required and documented (Task 6).

---

### Task 1: Idle fields in the runtime profile

**Files:**
- Modify: `src/platform/windows/runtime/runtime_player_profile.hpp` / `.cpp`
- Modify: `src/platform/windows/runtime/runtime_player_profile_loader.hpp` / `.cpp`
- Create: `src/platform/windows/runtime/runtime_player_profile_configuration_error.hpp` / `.cpp`
- Modify: `CMakeLists.txt` (the new .cpp)
- Create: `builder.tests/RuntimePlayerProfileSourceTests.cs`

**Interfaces (produced):**
- `RuntimePlayerProfile` gains, with default member initializers, so the existing `{w, h}` aggregate init still compiles:
  - `bool IdleThrottleEnabled = false;`
  - `int IdleAfterMilliseconds = 500;`
  - `int IdleFramesPerSecond = 10;`
  - `bool IdleFieldsPresent = false;` (true when any idle field appeared in the file)
- `class RuntimePlayerProfileConfigurationError : public std::runtime_error` with ctor `(const std::string& message)`.
- Loader:
  - `bool TryParseOptionalInteger(const std::string& json, const char* propertyName, int& value) const`
  - `bool TryParseOptionalBoolean(const std::string& json, const char* propertyName, bool& value) const` (accepts `true`/`false` only)
  - `void ValidateIdleFields(const RuntimePlayerProfile& profile) const`, which throws `RuntimePlayerProfileConfigurationError` when after-ms ≤ 0 or fps is outside 1..30.
  - It parses the idle fields and validates them **before and outside** the existing `catch (const std::exception&)` repair block, so the configuration error propagates. Only resolution problems keep the old repair behavior.
  - `BuildProfileJson` emits the idle fields only when `IdleFieldsPresent`, which keeps seeded files byte-identical.
- `Win32Application::ResolveRuntimePlayerProfile` (cpp ~1251-1272) catches `RuntimePlayerProfileConfigurationError` and throws `Win32ExitRequest(2, message)`.

- [ ] **Step 1: Write failing source tests.** Assert that:
  - the three JSON property names appear;
  - `RuntimePlayerProfileConfigurationError` is thrown in the loader and caught in `win32_application.cpp` alongside `Win32ExitRequest(2`;
  - `BuildProfileJson` guards the idle output with `IdleFieldsPresent`;
  - the idle parse and validate calls appear **before** the `catch (const std::exception` in `LoadOrCreateProfile` (IndexOf ordering, anchored inside that function);
  - `CMakeLists.txt` lists the new .cpp.
- [ ] **Step 2:** Run the tests and confirm they fail: `dotnet test builder.tests\helengine.windows.builder.tests.csproj -p:HelEngineRoot=C:\dev\helworks\helengine --filter "FullyQualifiedName~RuntimePlayerProfileSourceTests" 2>&1 | Select-Object -Last 20`.
- [ ] **Step 3: Implement.** Follow `ParseRequiredInteger`'s substring style for the new parsers. Every member gets a `///` doc.
- [ ] **Step 4:** Run the source tests plus the full builder.tests and confirm they pass. Any failure must be proven pre-existing (stash comparison).
- [ ] **Step 5:** Commit: `feat(player): read optional idle-throttle fields from the runtime profile`.

---

### Task 2: Idle CLI flags

**Files:**
- Modify: `src/platform/windows/win32/win32_command_line_options.hpp` / `.cpp`
- Modify: `builder.tests/Win32CommandLineOptionsSourceTests.cs`

**Interfaces (produced):** Accessors:
- `bool HasIdleThrottle() const`, `bool GetIdleThrottleEnabled() const`
- `bool HasIdleAfterMilliseconds() const`, `int GetIdleAfterMilliseconds() const`
- `bool HasIdleFramesPerSecond() const`, `int GetIdleFramesPerSecond() const`

Parsing rules:
- `IsKnownFlag` (cpp ~189-191) gains the three flags. Update the doc comments at hpp ~14-19 and ~56, and cpp ~28-33.
- Each flag gets an explicit `else if` branch **before** the final `else` (cpp ~97), which is the `--capture` branch.
- Values:
  - `--idle-throttle` accepts `on` or `off` exactly;
  - `--idle-after-ms` is an int > 0 (the `ParseFrameLimit` style, full consumption);
  - `--idle-fps` is an int in 1..30.
- Repeats and missing values throw `std::invalid_argument` (existing behavior), and so exit with code 2.

- [ ] **Step 1: Write failing source tests.** Assert:
  - each flag string is in `IsKnownFlag`;
  - each new `else if` appears before the `--capture` branch;
  - the range checks `1` / `30` and `> 0` are present;
  - `"on"` and `"off"` literals are present.
- [ ] **Step 2:** Run the tests and confirm they fail.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Run the tests and confirm they pass.
- [ ] **Step 5:** Commit: `feat(player): accept idle-throttle command-line flags`.

---

### Task 3: Activity tracker, pacer and opt-in loop

**Files:**
- Create: `src/platform/windows/win32/win32_idle_throttle_settings.hpp` / `.cpp`
- Create: `src/platform/windows/win32/win32_activity_tracker.hpp` / `.cpp`
- Create: `src/platform/windows/win32/win32_idle_frame_decision.hpp` (struct, header-only)
- Create: `src/platform/windows/win32/win32_idle_frame_pacer.hpp` / `.cpp`
- Modify: `src/platform/windows/win32/win32_window.hpp` / `.cpp`
- Modify: `src/platform/windows/win32/win32_application.hpp` / `.cpp`
- Modify: `CMakeLists.txt`
- Create: `builder.tests/Win32IdleThrottleSourceTests.cs`

**Interfaces (produced):**
- `class Win32IdleThrottleSettings` (value type):
  - constructor `(bool enabled, int idleAfterMilliseconds, int idleFramesPerSecond)`, which validates as in Task 1 and throws `std::invalid_argument`;
  - getters;
  - `static Win32IdleThrottleSettings Resolve(const RuntimePlayerProfile& profile, const Win32CommandLineOptions& options)`: each CLI value overrides its profile value; `enabled` comes from the CLI if given, otherwise from the profile;
  - `std::string Describe() const` returns `idleThrottle=on afterMs=<n> fps=<n> source=<profile|commandLine|mixed>`.
- `class Win32ActivityTracker`:
  - `void ObserveMessage(UINT message)`: updates `LastActivity = std::chrono::steady_clock::now()` for exactly the message list in spec §2 and ignores all others;
  - `void MarkActivity()`;
  - `std::chrono::steady_clock::time_point GetLastActivity() const`.
- `struct Win32IdleFrameDecision { bool Active; int WaitMilliseconds; }`
- `class Win32IdleFramePacer` (pure, no Win32 calls):
  - constructor `(const Win32IdleThrottleSettings& settings)`;
  - `Win32IdleFrameDecision Decide(long long nowMs, long long lastActivityMs, long long lastFrameStartMs, bool keepAwake) const`:
    - active when `keepAwake` or `nowMs - lastActivityMs < IdleAfterMilliseconds`;
    - otherwise `WaitMilliseconds = max(0, lastFrameStartMs + 1000 / IdleFramesPerSecond - nowMs)`.
- `Win32Window`:
  - member `Win32ActivityTracker* ActivityTracker` (initialized `nullptr` in the ctor);
  - `void SetActivityTracker(Win32ActivityTracker* tracker)`;
  - in `HandleMessage`, **before** the existing switch: `if (ActivityTracker != nullptr) { ActivityTracker->ObserveMessage(message); }`. The switch and all return values stay unchanged.
- `Win32Application`:
  - members `std::unique_ptr<Win32ActivityTracker> ActivityTracker;`, `std::unique_ptr<Win32IdleFramePacer> IdleFramePacer;`, `int IdleFrameCount;`, `int ActiveFrameCount;`, added after `RenderedFrameCount` in declaration order, with the ctor init list updated;
  - in `CreateMainWindow`: after `ResolveRuntimePlayerProfile()`, resolve the settings. If enabled, create the tracker and pacer, call `MainWindow->SetActivityTracker(ActivityTracker.get())` between `make_unique<Win32Window>` and `Create()`, and log `settings.Describe()`. If disabled, create nothing.
  - `Run()` loop:

    ```cpp
    if (IdleFramePacer) {
        RunIdleThrottledLoop();
    } else {
        while (PumpMessages()) {
            RenderFrame();
        }
    }
    ```

    The `else` body stays textually identical to today's loop.
  - `void RunIdleThrottledLoop()`:
    - Each iteration: compute `keepAwake = IsEngineKeepAwake()` and `Decide(...)` with `steady_clock` milliseconds.
    - If idle and `WaitMilliseconds > 0`: `MsgWaitForMultipleObjectsEx(0, nullptr, WaitMilliseconds, QS_ALLINPUT, MWMO_INPUTAVAILABLE)` inside a `HELENGINE_TRACY_ZONE_N("Frame.PacingAndIdle")`.
    - Then `if (!PumpMessages()) break;`. Messages observed by the tracker update the activity time.
    - Re-decide the mode for counting.
    - Record the frame start, then `RenderFrame()`.
    - Increment `IdleFrameCount` or `ActiveFrameCount`, but only when `CommandLineOptions.HasFrameLimit()`, following the existing counter rule.
    - It must not spin: when `RenderFrame` returns early (minimized), the next iteration's idle wait still applies. If the player is active and minimized, also wait `1000 / IdleFramesPerSecond` before retrying.
  - `bool IsEngineKeepAwake()`:
    - `EngineCore->get_PredictedPhysicsStepSeconds() > 0.0`;
    - or `sceneManager != nullptr && (sceneManager->get_IsSceneTransitionActive() || sceneManager->get_LastTracePendingOperationCount() > 0)`, with `auto sceneManager = EngineCore->get_SceneManager();`.
    - Confirm these getter names against the generated core headers. The newest headers are under `C:\dev\helworks\builds\helengine\cache\v2\...\generated-core\Core.hpp` and `SceneManager.hpp`, and the host already calls the trace getters at `win32_application.cpp` ~1557-1561. If a getter is missing, drop that condition and document it in the report and the spec. Do **not** change helengine.

- [ ] **Step 1: Write failing source tests.** Assert:
  - `win32_window.cpp` contains `if (ActivityTracker != nullptr)` before `switch (message)` inside `HandleMessage`;
  - the ctor initializes `ActivityTracker(nullptr)`;
  - `win32_application.cpp` still contains the exact default loop `while (PumpMessages()) {` followed by `RenderFrame();` inside the `else`;
  - `SetActivityTracker` is only called inside the enabled branch;
  - `MsgWaitForMultipleObjectsEx`, `QS_ALLINPUT`, `MWMO_INPUTAVAILABLE` and `get_PredictedPhysicsStepSeconds` are present;
  - the tracker's message list includes each message from spec §2;
  - the pacer contains `1000 / ` and no Win32 includes (`windows.h` must not appear in `win32_idle_frame_pacer.*`);
  - CMake lists every new .cpp.
- [ ] **Step 2:** Run the tests and confirm they fail.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Run the source tests plus the full builder.tests and confirm they pass.
- [ ] **Step 5: Real build and manual proofs.** Run `-BuildOnly`. Then, through the launcher:
  - (a) no arguments: alive after 5 s, startup log shows the default scene and **no** idle configuration line;
  - (b) `--idle-throttle on --idle-after-ms 0 --idle-fps 5 --scene <smoke> --frames 10 --fixed-delta 0.016666`: `EXIT_CODE=0` and `elapsedMs` in the fingerprint line of at least 1600;
  - (c) a profile with `"idleFramesPerSecond": 99` placed in the output folder's `profile.json`: `EXIT_CODE=2` with a message. Afterwards restore it to `{"resolutionWidth":640,"resolutionHeight":360}`;
  - (d) `--idle-throttle maybe`: `EXIT_CODE=2`.

  Record everything in the report.
- [ ] **Step 6:** Commit: `feat(player): opt-in idle frame pacing driven by window activity`.

---

### Task 4: Fingerprint idle counters (native and tool)

**Files:**
- Modify: `src/platform/windows/directx11/directx11_host_fingerprint.hpp` / `.cpp`
- Modify: `src/platform/windows/win32/win32_application.cpp` (frame-limit block)
- Modify: `builder.tests/DirectX11HostFingerprintSourceTests.cs`
- Modify: `builder.tests/Win32CommandLineOptionsSourceTests.cs`, if its frame-limit regex changes
- Modify: `tools/regression/HostFingerprint.cs`
- Modify: `tools/regression.tests/HostFingerprintTests.cs`, plus any comparer tests with literal lines

**Interfaces:**
- `DirectX11HostFingerprint::Describe(int frameCount, bool idleThrottleEnabled, int idleFrames, int activeFrames) const` appends ` idleThrottle=<on|off> idleFrames=<n> activeFrames=<n>` after `frames=`, before `elapsedMs`.
- The call site in the frame-limit block passes `IdleFramePacer != nullptr`, `IdleFrameCount`, `ActiveFrameCount`. With throttling off, `activeFrames` must equal the frame count, so the default loop also counts active frames in frame-limit mode.
  - Implement this without touching the default loop text: count inside the existing frame-limit block in `RenderFrame`, which is reached from both loops. Use `IdleFramePacer` state to know whether the current frame was idle. `RunIdleThrottledLoop` sets a member `CurrentFrameIsIdle` before calling `RenderFrame`; the default loop leaves it `false`.
- `HostFingerprint.cs`: add `idleThrottle`, `idleFrames` and `activeFrames` to `ComparedFieldNames`, and `idleFrames`/`activeFrames` to `CounterFieldNames`.

- [ ] **Step 1:** Update the existing strict source tests (their exact regexes of the frame-limit block and the `HostFingerprint->` count) to the new text, and add assertions for the three fields. Update the tool tests' `SampleLine` and the any-order test, and add a test that an old line missing the fields fails `Parse`.
- [ ] **Step 2:** Run the tests and confirm they fail.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Run the tests and confirm they pass: builder.tests (full) and `tools\regression.tests` (full).
- [ ] **Step 5:** `-BuildOnly`, then one `--frames 5 --fixed-delta 0.016666` run with no idle flags. The fingerprint must show `idleThrottle=off idleFrames=0 activeFrames=5`.
- [ ] **Step 6:** Commit: `feat(player): report idle and active frame counts in the host fingerprint`.

---

### Task 5: Idle scenario in the regression net

**Files:**
- Modify: `scripts/run-regression.ps1`
- Modify: `tools/regression/RegressionCommandRunner.cs` (new `check-idle` command) and its tests
- Modify: `builder.tests/RegressionScriptSourceTests.cs`
- Modify: `regression/README.md`

**Interfaces:**
- **Tool command** `check-idle "<fingerprint line>" <minIdleFrames> <minElapsedMs>`:
  - prints `PASS idleFrames=<n> elapsedMs=<n>`, or `FAIL <reason>`;
  - exits 0 or 1, and 2 on usage or parse errors;
  - also fails when `idleThrottle` is not `on` or `presentFailures` is not 0.
  - Unit tests are required.
- **Script:**
  - `Invoke-PlayerScene` gains `-FrameCount` (default `'30'`; the literal `'30'` must remain for the existing test) and `-ExtraArguments` (default empty).
  - After the Record loop and after the Verify loop, run the idle scenario once on the smoke scene with the spec's arguments. Record adds a manifest entry `@{ id=<smoke>; kind='idle'; minIdleFrames=25; minElapsedMs=2400 }`, and the existing Verify code ignores unknown kinds.
  - Verify checks:
    - `compare` the idle capture against the smoke scene's golden;
    - `check-idle <line> 25 2400`.

    Summary lines are `PASS|FAIL idle <scene> ...`.
  - Confirm the smoke scene has no physics runtime by searching the startup log for physics registration, or the scene file for physics components. If it has one, use the first golden scene without physics, and document it in the README.
- **README:** document the idle scenario, the physics caveat, and that goldens must be re-recorded once because the fingerprint fields changed.

- [ ] **Step 1:** Write failing tool tests for `check-idle` (pass, too few idle frames, too fast, throttle off, bad usage) and failing source assertions for `check-idle`, `--idle-throttle`, `'2400'`, `kind='idle'`/`"idle"`, and `-ExtraArguments`.
- [ ] **Step 2:** Run the tests and confirm they fail.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Run the tests and confirm they pass.
- [ ] **Step 5:** Do **not** run `-Record` here (that is Task 6). Run the idle scenario manually with the launcher and `check-idle` to prove it passes on the built player. Record the result.
- [ ] **Step 6:** Commit: `feat(regression): verify the opt-in idle throttle scenario`.

---

### Task 6: Acceptance

- [ ] **Step 1:** Run `-Record`, a deliberate re-record because the fingerprint schema changed. Then check with `git diff --stat regression/golden/*.png` that the **golden PNGs are byte-identical**. If any PNG changed, stop and investigate: the default path must render identically.
- [ ] **Step 2:** Run `-Verify` → `RESULT: PASS`, including `PASS idle ...`.
- [ ] **Step 3:** Run `-Verify` again → `RESULT: PASS`.
- [ ] **Step 4: Negative check.**
  1. Temporarily change the idle scenario's `--idle-fps 10` to `--idle-fps 30` in the script (a local edit, not committed).
  2. Run `-Verify`. `check-idle` must FAIL on `elapsedMs`: about 1000 ms, which is below 2400.
  3. Revert the edit.
  4. Run `-Verify` again and confirm it passes.
- [ ] **Step 5: No-argument boot.** It must be alive after 5 s, show the default scene, and log no idle line and no `HOST_FINGERPRINT`.
- [ ] **Step 6:** Commit the manifest, baselines and README updates: `test(regression): re-record with idle fingerprint fields and verify the idle scenario`.
