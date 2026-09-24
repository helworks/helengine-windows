# Windows Player Regression Safety Net Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A `scripts/run-regression.ps1 -Record | -Verify` safety net that builds this checkout's Windows player, runs the test-project scenes deterministically, compares captures with goldens, and compares editor/builder test failures with baselines.

**Architecture:**
- The player gains four opt-in flags (`--scene`, `--frames`, `--fixed-delta`, `--capture`). With no arguments, behavior is unchanged.
- A C# console tool does the image and test-result comparisons.
- A PowerShell script orchestrates the whole run on a copy of `helengine\test-project`. It builds through the canonical `build-platform.ps1`, with an isolated engine user-settings root so it always builds its own checkout.

**Tech Stack:**
- C++20/Win32/D3D11 (`helengine-windows/src`), CMake/Ninja via the canonical build.
- C# net9.0-windows console + xunit.
- PowerShell 5.1.

**Spec:** `docs/superpowers/specs/2026-09-24-windows-player-regression-safety-net-design.md`

## Global Constraints

- Only this repo (`helengine-windows`) changes. Never modify anything under `C:\dev\helworks\helengine`, including `test-project` and `user_settings`, or the main helengine-windows checkout (`C:\dev\helworks\helengine-windows`, which has unrelated uncommitted work).
- Work in `C:\dev\helworks\helengine-windows\.worktrees\regression-safety-net` on branch `feature/regression-safety-net`.
- Default player behavior with no command-line arguments must be identical to today:
  - first catalog scene;
  - endless `while (PumpMessages()) RenderFrame();`;
  - wall-clock `EngineCore->Update()`;
  - no capture objects constructed.
- AGENTS.md rules:
  - one class per file;
  - `///` doc comments on every member, repeated on the .cpp definitions;
  - PascalCase fields, same-line braces, no tuples;
  - no local helper functions;
  - nullable disabled;
  - throw instead of defaulting;
  - byte-cap command output.
- C++ files use snake_case names in `src/platform/windows/win32/` or `.../directx11/`, `namespace helengine::windows`, `#pragma once`, and `"platform/windows/..."` includes. Every new `.cpp` goes into the explicit `HELENGINE_WINDOWS_SOURCES` list in `CMakeLists.txt`.
- Build outputs go under `C:\dev\helworks\builds\helengine-windows\regression\...`, never %TEMP%.
- Build the player only through `C:\dev\helworks\helengine\scripts\build-platform.ps1`. Launch it only through `scripts\launch_in_emulator.ps1`.
- Every `dotnet build`/`dotnet test` of `builder` or `builder.tests` passes `-p:HelEngineRoot=C:\dev\helworks\helengine`.
- Exit codes:
  - player: 0 ok, 1 unhandled exception (existing), 2 invalid arguments or unknown scene, 3 capture failure;
  - regression tool: 0 pass, 1 check failed, 2 usage error.
- Comparison rules: a pixel differs if any B/G/R channel delta is > 8 (alpha is ignored). A scene fails if the differing fraction is > 0.001, or if the sizes differ. A frame is blank if one color covers >= 99.9% of its pixels.
- Run the fixed scenes with `--frames 30 --fixed-delta 0.016666 --capture <path>` at 640×360.

## Review Focus

- **No arguments at all.** Double-clicking the exe, or the editor's normal launch, must behave exactly as before. Covered in Task 3 (`Win32Application_without_arguments_keeps_default_startup_path`), plus the manual no-args boot in Task 6.
- **Minimized or zero-size window while `--frames` is set.** Skipped frames must not count, so the run cannot exit before rendering. Covered in Task 3 (the counter increments after Present only).
- **A stale `profile.json` in the output folder** from an earlier run with a different size. Covered in Task 5: the script overwrites it before each run.
- **A build that silently uses the main checkout instead of this worktree.** Covered in Task 2: the script prints and asserts the resolved `playerSourceRootPath`.
- **Known pre-existing test failures** in editor or builder suites must not fail `-Verify`; only new ones fail it. Covered in Task 1 (`FailingSetComparer` tests) and Task 5.

---

### Task 1: Regression comparison tool (C#)

**Files:**
- Create: `tools/regression/helengine.windows.regression.csproj`
- Create: `tools/regression/RegressionImage.cs`
- Create: `tools/regression/BmpImageReader.cs`
- Create: `tools/regression/PngImageStore.cs`
- Create: `tools/regression/ImageComparer.cs`
- Create: `tools/regression/ImageComparison.cs`
- Create: `tools/regression/BlankFrameDetector.cs`
- Create: `tools/regression/TrxFailingTestReader.cs`
- Create: `tools/regression/FailingSetComparer.cs`
- Create: `tools/regression/FailingSetComparison.cs`
- Create: `tools/regression/RegressionCommandRunner.cs`
- Create: `tools/regression/Program.cs`
- Create: `tools/regression.tests/helengine.windows.regression.tests.csproj`
- Create: `tools/regression.tests/ImageComparerTests.cs`
- Create: `tools/regression.tests/BmpImageReaderTests.cs`
- Create: `tools/regression.tests/BlankFrameDetectorTests.cs`
- Create: `tools/regression.tests/TrxFailingTestReaderTests.cs`
- Create: `tools/regression.tests/FailingSetComparerTests.cs`
- Create: `tools/regression.tests/RegressionCommandRunnerTests.cs`

**Interfaces (produced):**
- `sealed class RegressionImage`:
  - constructor `(int width, int height, byte[] bgra)`, where `bgra` is top-down with 4 bytes per pixel;
  - properties `Width`, `Height`, `Bgra`.
- `static class BmpImageReader` with `RegressionImage Read(string path)`. It accepts 32-bit BI_RGB, bottom-up or top-down, and throws `InvalidDataException` otherwise.
- `static class PngImageStore` with `void Save(RegressionImage image, string path)` and `RegressionImage Load(string path)` (System.Drawing, `Format32bppArgb`, via LockBits).
- `static class ImageComparer`:
  - `const int ChannelTolerance = 8`;
  - `const double MaxDifferingFraction = 0.001`;
  - `ImageComparison Compare(RegressionImage expected, RegressionImage actual)`.
- `sealed class ImageComparison`:
  - `bool SizesMatch`, `long DifferingPixels`, `long TotalPixels`, `double DifferingFraction`, `bool Passed`;
  - `RegressionImage DiffImage`: magenta for differing pixels, greyscale luminance of `actual` otherwise; null only when sizes differ.
- `static class BlankFrameDetector`:
  - `const double BlankFraction = 0.999`;
  - `bool IsBlank(RegressionImage image)`, which exact-matches B, G and R.
- `static class TrxFailingTestReader` with `IReadOnlyList<string> Read(string trxPath)`: sorted, distinct `testName` of every `UnitTestResult` whose `outcome="Failed"`.
- `static class FailingSetComparer` with `FailingSetComparison Compare(IReadOnlyList<string> baseline, IReadOnlyList<string> current)`.
- `sealed class FailingSetComparison` with `IReadOnlyList<string> NewFailures`, `IReadOnlyList<string> FixedFailures`, `bool Passed` (true when `NewFailures.Count == 0`).
- `sealed class RegressionCommandRunner` with `int Run(string[] args, TextWriter output)`. It implements the commands below and prints one result line each. `Program.Main` returns `new RegressionCommandRunner().Run(args, Console.Out)`.

CLI commands (exact):

| Command | Behavior | Exit |
|---|---|---|
| `compare <capture.bmp> <golden.png> <diff.png>` | Prints `PASS <fraction>` or `FAIL <fraction> <diff.png>`, or `FAIL size <wxh> vs <wxh>`. Writes the diff PNG only on failure. | 0/1 |
| `record-golden <capture.bmp> <golden.png>` | Converts the capture into a golden. | 0 |
| `stable <captureA.bmp> <captureB.bmp>` | `STABLE` when the comparison passes, otherwise `UNSTABLE <fraction>`. | 0/1 |
| `blank-check <capture.bmp>` | `BLANK` or `NOT_BLANK`. | 1/0 |
| `trx-failing <result.trx> <out.txt>` | Writes one name per line, sorted, with `\n` line endings. | 0 |
| `compare-failing <baseline.txt> <current.txt>` | `PASS`, or `FAIL` plus one `NEW <name>` line per new failure. It also prints `FIXED <name>` lines, which are informational only. | 0/1 |
| anything else | `USAGE ...` | 2 |

- [ ] **Step 1: Create the two csproj files.**
  - The tool project: `<OutputType>Exe</OutputType>`, `<TargetFramework>net9.0-windows</TargetFramework>`, `<UseWindowsForms>true</UseWindowsForms>` (for System.Drawing, as `helengine.render.validation.csproj` does), `<Nullable>disable</Nullable>`, `<ImplicitUsings>enable</ImplicitUsings>`, `<IsPackable>false</IsPackable>`, `<NuGetAudit>false</NuGetAudit>`.
  - The test project: the same framework settings, xunit 2.9.3, xunit.runner.visualstudio 3.1.4, Microsoft.NET.Test.Sdk 17.14.1, a global `<Using Include="Xunit" />`, and a `ProjectReference` to the tool.
- [ ] **Step 2: Write the failing tests.** Minimum cases:
  - `ImageComparerTests`: identical images pass with 0 differing; a single pixel with delta 9 on G in a 100×100 image passes (0.0001); 11 pixels differing in 100×100 fail (0.0011); delta 8 does not count as differing; alpha differences are ignored; a size mismatch gives `SizesMatch == false`, `Passed == false` and `DiffImage == null`; a diff pixel is magenta `(B=255,G=0,R=255)`.
  - `BmpImageReaderTests`:
    - write a 2×2 32-bit top-down BMP by hand (54-byte header, negative height) and read it back, checking the exact pixels;
    - the same for bottom-up (positive height, rows reversed);
    - a 24-bit BMP throws `InvalidDataException`.
  - `BlankFrameDetectorTests`: a uniform image is blank; one differing pixel in 10×10 (99%) is not blank; 1 differing pixel in 1000×1 (99.9%) is blank.
  - `TrxFailingTestReaderTests`: a minimal TRX XML with the namespace `http://microsoft.com/schemas/VisualStudio/TeamTest/2010` and two failed, one passed and one duplicate failed results returns the two failed names, sorted.
  - `FailingSetComparerTests`: baseline {A,B}, current {B,C} gives New {C}, Fixed {A}, Passed false; current ⊆ baseline passes.
  - `RegressionCommandRunnerTests`: an unknown command returns 2 and prints `USAGE`; `compare-failing` writes the `NEW` lines; `compare` on two identical temp BMPs returns 0 and prints `PASS`. Temp files go under `Path.Combine(AppContext.BaseDirectory, "test-output")`, not %TEMP%.
- [ ] **Step 3: Run the tests and confirm they fail.** Run `dotnet test tools\regression.tests\helengine.windows.regression.tests.csproj 2>&1 | Select-Object -Last 15`. Expected: a compile failure.
- [ ] **Step 4: Implement the classes.** Key logic:

```csharp
// ImageComparer.Compare (core loop)
long differing = 0;
byte[] diff = new byte[actual.Bgra.Length];
for (int offset = 0; offset < actual.Bgra.Length; offset += 4) {
    int deltaB = Math.Abs(expected.Bgra[offset] - actual.Bgra[offset]);
    int deltaG = Math.Abs(expected.Bgra[offset + 1] - actual.Bgra[offset + 1]);
    int deltaR = Math.Abs(expected.Bgra[offset + 2] - actual.Bgra[offset + 2]);
    if (deltaB > ChannelTolerance || deltaG > ChannelTolerance || deltaR > ChannelTolerance) {
        differing++;
        diff[offset] = 255; diff[offset + 1] = 0; diff[offset + 2] = 255; diff[offset + 3] = 255;
    } else {
        byte luminance = (byte)((actual.Bgra[offset] * 29 + actual.Bgra[offset + 1] * 150 + actual.Bgra[offset + 2] * 77) >> 8);
        diff[offset] = luminance; diff[offset + 1] = luminance; diff[offset + 2] = luminance; diff[offset + 3] = 255;
    }
}
```

```csharp
// BmpImageReader.Read (header rules)
// bytes 0-1 "BM"; pixel data offset = UInt32 at 10; header size UInt32 at 14 (>= 40);
// width Int32 at 18; height Int32 at 22 (negative = top-down); planes UInt16 at 26 == 1;
// bitCount UInt16 at 28 must be 32; compression UInt32 at 30 must be 0 (BI_RGB) or 3 (BI_BITFIELDS with BGRA masks).
// Row stride = width * 4. For bottom-up, copy source row (height - 1 - y) into destination row y.
```

- [ ] **Step 5: Run the tests and confirm they pass.** Same command. Expected: all pass, with no warnings in the output.
- [ ] **Step 6: Commit** with `feat(regression): add the image and test-result comparison tool`.

---

### Task 2: Launcher arguments, and the script's isolated build stage (`-BuildOnly`)

**Files:**
- Modify: `scripts/launch_in_emulator.ps1`
- Create: `scripts/run-regression.ps1` (build stage only in this task)
- Create: `builder.tests/RegressionScriptSourceTests.cs`
- Modify: `builder.tests/WindowsLauncherScriptTests.cs` (only to add assertions)

**Interfaces (produced):**
- `launch_in_emulator.ps1` parameters:
  - `-ArtifactPath` (unchanged, mandatory);
  - `[string[]]$ArgumentList` (optional);
  - `[switch]$Wait`.

  With `-Wait`, it waits for the process to exit and prints `EXIT_CODE=<n>` after `PROCESS_ID=`. It then exits with the process's exit code. Without the new parameters, its output and behavior are unchanged.
- `run-regression.ps1` parameters:
  - `[switch]$Record`, `[switch]$Verify`, `[switch]$BuildOnly` (exactly one is required);
  - `[string]$HelengineRoot = 'C:\dev\helworks\helengine'`;
  - `[string]$WorkRoot = 'C:\dev\helworks\builds\helengine-windows\regression'`.

  In this task only `-BuildOnly` is implemented. `-Record` and `-Verify` throw `"Not implemented until the verify stage is added."`, and Task 5 replaces that.

`-BuildOnly` must do the following, in order (all paths absolute; `$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path`):
1. `$ErrorActionPreference='Stop'`. Validate that `$HelengineRoot\scripts\build-platform.ps1` and `$HelengineRoot\test-project\project.heproj` exist.
2. **Project copy:**
   - Remove and recreate `$WorkRoot\project`.
   - Copy `$HelengineRoot\test-project` into it, excluding folders named `bin`, `obj`, `cache` and `.worktrees`, using `robocopy /MIR /XD bin obj cache .worktrees /NFL /NDL /NJH /NJS`. Robocopy exit codes below 8 mean success.
3. **Engine user settings:**
   - Read `$HelengineRoot\user_settings\platforms.json` and select the `platformId == 'windows'` entry. Throw if it is missing.
   - Set its `builderAssemblyPath` to `$RepoRoot\builder\bin\Debug\net9.0\helengine.windows.builder.dll` and its `playerSourceRootPath` to `$RepoRoot`.
   - Make every other relative path in **all** entries (`builderAssemblyPath`, `playerSourceRootPath`, `generatedCoreCppRootPath`, `codegenToolPath`, `pluginManifestPath`) absolute against `$HelengineRoot\user_settings`, so they still resolve from the new location.
   - Write the result as UTF-8 without a BOM to `$WorkRoot\engine-user-settings\platforms.json`.
4. **Project version:** in the copy's `project.heproj`, set `requiredEngineVersion` to the windows entry's `engineVersion`.
5. **Build config:** write the copy's `user_settings\build_config.json` from the original, with the windows entry changed:
   - `selectedSceneIds` = `Scenes/Bootstrap.helen` followed by each `assets\Scenes\rendering\*.helen` as `Scenes/rendering/<file>`, sorted ordinal;
   - `sceneOrders` numbered 1..N in that order;
   - `outputDirectoryPath` = `$WorkRoot\player` with forward slashes;
   - `selectedGraphicsOptionValues` = `{ "default-width": "640", "default-height": "360" }`.
6. **Builder:** `dotnet build "$RepoRoot\builder\helengine.windows.builder.csproj" -c Debug -p:HelEngineRoot=$HelengineRoot`. Throw on a non-zero exit.
7. **Build:**
   - Set `$env:HELENGINE_ENGINE_USER_SETTINGS_ROOT = "$WorkRoot\engine-user-settings"`.
   - Run `& powershell -NoProfile -ExecutionPolicy Bypass -File "$HelengineRoot\scripts\build-platform.ps1" -Project "$WorkRoot\project\project.heproj" -Platform windows -Output "$WorkRoot\player" -Configuration Debug`, then restore the previous env value in a `finally`.
   - Throw on a non-zero exit, including the exit code in the message.
8. Assert that `$WorkRoot\player\helengine_windows.exe` exists and was written after the build started.
9. Print `PLAYER_SOURCE_ROOT=<RepoRoot>` and `PLAYER=<exe path>`.

- [ ] **Step 1: Write the failing source tests.**
  - In `RegressionScriptSourceTests`, read `scripts/run-regression.ps1` and assert it contains:
    - `HELENGINE_ENGINE_USER_SETTINGS_ROOT`;
    - `requiredEngineVersion`;
    - `build-platform.ps1`;
    - `-p:HelEngineRoot=`;
    - `default-width`;
    - `PLAYER_SOURCE_ROOT=`;
    - `[switch]$BuildOnly`.
  - Assert it does **not** contain `cmake` or `dotnet publish`.
  - Add launcher assertions: `[string[]]$ArgumentList`, `[switch]$Wait`, `EXIT_CODE=`. The existing assertions stay.
- [ ] **Step 2: Run the tests and confirm they fail.** Run `dotnet test builder.tests\helengine.windows.builder.tests.csproj -p:HelEngineRoot=C:\dev\helworks\helengine --filter "FullyQualifiedName~RegressionScriptSourceTests|FullyQualifiedName~WindowsLauncherScriptTests" 2>&1 | Select-Object -Last 15`.
- [ ] **Step 3: Implement the launcher change.** Keep the existing lines verbatim; add the parameters and:

```powershell
$startArguments = @{ FilePath = $resolvedArtifactPath; WorkingDirectory = (Split-Path -Path $resolvedArtifactPath -Parent); PassThru = $true }
if ($ArgumentList -and $ArgumentList.Count -gt 0) { $startArguments['ArgumentList'] = $ArgumentList }
```

  Keep the literal `Start-Process -FilePath $resolvedArtifactPath` in the no-argument branch so the existing test string is still present. After it prints `PROCESS_ID`: `if ($Wait) { $process.WaitForExit(); Write-Output ("EXIT_CODE=" + $process.ExitCode); exit $process.ExitCode }`.
- [ ] **Step 4: Implement `run-regression.ps1` `-BuildOnly`** as specified above, with a comment block at the top describing the modes.
- [ ] **Step 5: Run the source tests and confirm they pass.**
- [ ] **Step 6: Real isolation proof.**
  1. Add the temporary lifecycle log line `WriteLifecycleLog("Regression worktree build marker.");` at the start of `Run()` after `InitializeFileLog()`.
  2. Run `powershell -File scripts\run-regression.ps1 -BuildOnly 2>&1 | Select-Object -Last 20`.
  3. Launch through the launcher with `-Wait`. Because the player has no exit flag yet, launch without `-Wait`, give it 3 seconds, then `Stop-Process` the printed PID.
  4. Confirm the marker is in `$WorkRoot\player\helengine_windows.startup.log`.
  5. **Remove the marker line** before committing.

  Record the evidence (the command output and the log line) in the report. If the marker is missing, the build did not use this worktree: report BLOCKED with the log.
- [ ] **Step 7: Commit** with `feat(regression): build this checkout's player in an isolated project copy`.

---

### Task 3: Player options: `--scene`, `--frames`, `--fixed-delta` (C++)

**Files:**
- Create: `src/platform/windows/win32/win32_command_line_options.hpp`
- Create: `src/platform/windows/win32/win32_command_line_options.cpp`
- Modify: `src/platform/windows/win32/win32_application.hpp` (a new member)
- Modify: `src/platform/windows/win32/win32_application.cpp`: ctor init list (~501-527), `Run()` (~551-607), `LoadPackagedStartupScene()` (~1164-1200), `RenderFrame()` (~1627-1716)
- Modify: `CMakeLists.txt` (add the .cpp to `HELENGINE_WINDOWS_SOURCES`; add `shell32` to `target_link_libraries`)
- Create: `builder.tests/Win32CommandLineOptionsSourceTests.cs`

**Interfaces (produced):**
- `class Win32CommandLineOptions`:
  - `static Win32CommandLineOptions Parse(int argumentCount, wchar_t** arguments)`, where `arguments[0]` is the exe path and is skipped. It throws `std::invalid_argument` with a readable message on any error.
  - Accessors: `bool HasScene() const`, `const std::string& GetSceneId() const` (UTF-8), `bool HasFrameLimit() const`, `int GetFrameLimit() const`, `bool HasFixedDelta() const`, `double GetFixedDeltaSeconds() const`, `bool HasCapturePath() const`, `const std::wstring& GetCapturePath() const`.
  - `static Win32CommandLineOptions ParseProcessCommandLine()`, which calls `CommandLineToArgvW(GetCommandLineW(), &count)`, then `Parse`, then `LocalFree`.
  - Parsing rules:
    - Each flag takes exactly one value.
    - A flag repeated, or given without its value, throws.
    - An unknown flag throws.
    - `--frames` must parse fully as an int >= 1.
    - `--fixed-delta` must parse fully as a double > 0.
    - `--capture` without `--frames` throws.
    - The `--scene` value is converted to UTF-8 with `WideCharToMultiByte(CP_UTF8, ...)`.
- `Win32Application`:
  - new member `Win32CommandLineOptions CommandLineOptions;`, declared after `BepuDebugSnapshotStatusLogCount` and initialized in declaration order in the ctor init list;
  - new member `int RenderedFrameCount;` (0).

Behavior changes:
- **In `Run()`**, right after `InitializeFileLog()`: `try { CommandLineOptions = Win32CommandLineOptions::ParseProcessCommandLine(); } catch (const std::invalid_argument& error) { WriteLifecycleLog(error.what()); return 2; }`.
- **In `LoadPackagedStartupScene()`**: when `CommandLineOptions.HasScene()`, find the entry whose `get_SceneId()` equals the id. If none matches, log `"Requested scene was not found in the packaged catalog: <id>"` and throw a new `Win32ExitRequest` (see below) with code 2. Otherwise load it instead of `[0]`. Without the flag, the `[0]` path stays byte-identical.
- **In `RenderFrame()`**: replace `EngineCore->Update();` with `if (CommandLineOptions.HasFixedDelta()) { EngineCore->Update(CommandLineOptions.GetFixedDeltaSeconds()); } else { EngineCore->Update(); }`. After `Presenter->RenderFrame();`, run `RenderedFrameCount++; if (CommandLineOptions.HasFrameLimit() && RenderedFrameCount >= CommandLineOptions.GetFrameLimit()) { PostQuitMessage(0); }`. The counter only increments on frames that reached Present.
- **Exit-code plumbing.** Create `src/platform/windows/win32/win32_exit_request.hpp/.cpp`: `class Win32ExitRequest : public std::runtime_error { public: Win32ExitRequest(int exitCode, const std::string& message); int GetExitCode() const; ... }`. In `Run()`, add `catch (const Win32ExitRequest& request) { WriteLifecycleLog(request.what()); return request.GetExitCode(); }` **before** the existing `std::exception` catch.
  - Check how `InitializeEngineCore` wraps `LoadPackagedStartupScene` (~723): it logs and rethrows. Make sure the rethrow preserves the type (`throw;`, not `throw std::runtime_error(...)`). If it does not, adjust only that rethrow.

- [ ] **Step 1: Write the failing source tests.** They follow `Win32ApplicationRuntimeBootstrapSourceTests.cs` and read the .hpp/.cpp files. Assert:
  - the parser contains `"--scene"`, `"--frames"`, `"--fixed-delta"`, `"--capture"`, `CommandLineToArgvW`, `LocalFree`, `std::invalid_argument`;
  - `CMakeLists.txt` contains `win32_command_line_options.cpp`, `win32_exit_request.cpp` and `shell32`;
  - `win32_application.cpp` contains `ParseProcessCommandLine()` and `GetFixedDeltaSeconds()`;
  - `PostQuitMessage(0)` appears after `Presenter->RenderFrame();`, checked with IndexOf ordering;
  - the catch for `Win32ExitRequest` appears before `catch (const std::exception`.

  Test `Win32Application_without_arguments_keeps_default_startup_path` asserts that `(*catalogEntries)[0]` is still present and that `EngineCore->Update();` still exists inside the else branch.
- [ ] **Step 2: Run the tests and confirm they fail.** Run `dotnet test builder.tests\helengine.windows.builder.tests.csproj -p:HelEngineRoot=C:\dev\helworks\helengine --filter "FullyQualifiedName~Win32CommandLineOptionsSourceTests" 2>&1 | Select-Object -Last 15`.
- [ ] **Step 3: Implement.** Every member gets a `///` doc comment in both the .hpp and the .cpp.
- [ ] **Step 4: Run the source tests plus the whole `builder.tests`, and confirm they pass.** Record the full-suite failing names; any failure must also fail on the task's base commit, which you check with `git stash`.
- [ ] **Step 5: Compile for real.** Run `powershell -File scripts\run-regression.ps1 -BuildOnly 2>&1 | Select-Object -Last 20`, and fix compile errors in the source (never in generated code). Then manual checks through the launcher with `-Wait`:
  - `--scene Scenes/Bootstrap.helen --frames 10 --fixed-delta 0.016666` → `EXIT_CODE=0`;
  - `--scene Nope --frames 10` → `EXIT_CODE=2`, with the message in the startup log;
  - `--bogus 1` → `EXIT_CODE=2`;
  - no arguments → launch without `-Wait`, confirm the process is still alive after 5 seconds, then `Stop-Process`.

  Record all of it in the report.
- [ ] **Step 6: Commit** with `feat(player): add opt-in scene, frame-limit and fixed-delta command-line options`.

---

### Task 4: `--capture`: back-buffer capture to BMP (C++)

**Files:**
- Create: `src/platform/windows/directx11/directx11_back_buffer_capture.hpp/.cpp`
- Create: `src/platform/windows/win32/bmp_image_writer.hpp/.cpp`
- Modify: `src/platform/windows/win32/win32_application.hpp/.cpp`: member `std::unique_ptr<DirectX11BackBufferCapture> BackBufferCapture;`, constructed in `CreateGraphicsBootstrap()` only when `CommandLineOptions.HasCapturePath()`
- Modify: `CMakeLists.txt` (both .cpp files)
- Create: `builder.tests/DirectX11BackBufferCaptureSourceTests.cs`

**Interfaces (produced):**
- `class BmpImageWriter` with `static void WriteTopDownBgra32(const std::wstring& path, int width, int height, const std::vector<uint8_t>& bgraRows)`. It writes the 14-byte file header and the 40-byte BITMAPINFOHEADER (biHeight = -height, 32 bpp, BI_RGB), then the rows with stride width*4. It opens the file with `std::ofstream` in binary mode and throws `std::runtime_error` if the open or write fails.
- `class DirectX11BackBufferCapture`:
  - constructor `explicit DirectX11BackBufferCapture(DirectX11Bootstrap& bootstrap)`;
  - `void CaptureToBmp(const std::wstring& path)`:
    - get the back buffer with `GetSwapChain()->GetBuffer(0, __uuidof(ID3D11Texture2D), ...)`;
    - read its desc and create a staging texture with the same Width/Height/Format, `Usage = D3D11_USAGE_STAGING`, `CPUAccessFlags = D3D11_CPU_ACCESS_READ`, `BindFlags = 0`, `MipLevels = 1`, `ArraySize = 1`, `SampleDesc.Count = 1`;
    - `CopyResource`, then `Map(D3D11_MAP_READ)`;
    - copy each row honoring `RowPitch` into a tight buffer, then `Unmap`;
    - call `BmpImageWriter::WriteTopDownBgra32`.
  - It throws `std::runtime_error` on any failed HRESULT, with the HRESULT in hex, and on a format other than `DXGI_FORMAT_B8G8R8A8_UNORM`.
- In `RenderFrame()`:
  - Between `EngineCore->Draw();` and `Presenter->RenderFrame();`: `if (BackBufferCapture && CommandLineOptions.HasFrameLimit() && RenderedFrameCount + 1 == CommandLineOptions.GetFrameLimit()) { BackBufferCapture->CaptureToBmp(CommandLineOptions.GetCapturePath()); }`. That captures the last frame before its Present.
  - Wrap the capture in a try that catches `std::exception` and throws `Win32ExitRequest(3, <message>)`.

- [ ] **Step 1: Write the failing source tests.** Assert:
  - `D3D11_USAGE_STAGING`, `D3D11_CPU_ACCESS_READ`, `CopyResource`, `RowPitch`, `DXGI_FORMAT_B8G8R8A8_UNORM`;
  - that `biHeight` is written negative (look for `-height`);
  - that `BackBufferCapture` is only constructed inside a `HasCapturePath()` condition;
  - that `CaptureToBmp` appears between `EngineCore->Draw();` and `Presenter->RenderFrame();` (IndexOf ordering);
  - the CMake entries.
- [ ] **Step 2: Run the tests and confirm they fail.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run the source tests and the whole `builder.tests`,** with the same pre-existing-failure rule as Task 3.
- [ ] **Step 5: Compile for real** with `-BuildOnly`. Then use `--scene Scenes/rendering/opaque-basics.helen --frames 30 --fixed-delta 0.016666 --capture <WorkRoot>\captures\manual.bmp` through the launcher with `-Wait`:
  - expect `EXIT_CODE=0`;
  - run `dotnet run --project tools\regression -- blank-check <bmp>` and expect `NOT_BLANK`;
  - check the BMP is 640×360 with `BmpImageReader`, via a one-off `dotnet run` or a unit test in the tool project;
  - run twice and use `stable` on the two captures; record the result.
- [ ] **Step 6: Commit** with `feat(player): capture the final frame's back buffer to BMP when requested`.

---

### Task 5: `-Record` and `-Verify`

**Files:**
- Modify: `scripts/run-regression.ps1`
- Modify: `builder.tests/RegressionScriptSourceTests.cs`
- Create: `regression/README.md`: what the net checks, how to run it, how to re-record deliberately, and the thresholds

**Behavior** (after the shared build stage from Task 2, which `-Record` and `-Verify` also run first):
1. Publish the tool once: `dotnet build "$RepoRoot\tools\regression\helengine.windows.regression.csproj" -c Release`, and run it by the built exe path.
2. `$Scenes` = the same ordered list written into `build_config.json`.
3. For each scene:
   - write `$WorkRoot\player\profile.json` = `{"resolutionWidth":640,"resolutionHeight":360}`;
   - launch through `launch_in_emulator.ps1 -ArtifactPath <exe> -Wait -ArgumentList @('--scene', $id, '--frames', '30', '--fixed-delta', '0.016666', '--capture', $bmp)`;
   - parse `EXIT_CODE=`; a non-zero code is a FAIL, and the startup log tail (last 20 lines) is printed;
   - `$bmp = "$WorkRoot\captures\<run>\<sanitized id>.bmp"`, where `<run>` is `a`/`b` for Record and `verify` for Verify, and the sanitized id replaces `/` with `__`.
4. **Bootstrap (smoke):** it passes when the exit code is 0 and `blank-check` returns `NOT_BLANK`. No golden is used.
5. **Rendering scenes:**
   - **Record:** run each scene twice (runs `a` and `b`) and call `stable a b`. If stable, `record-golden a → regression\golden\<sanitized>.png`; otherwise add the scene to `unstable`.
   - **Verify:** skip the scenes the manifest lists as unstable (print `SKIP unstable`), then `compare <verify bmp> <golden> $WorkRoot\diffs\<sanitized>.diff.png`.
6. **Test suites.** Run each of these with `dotnet test <csproj> --logger "trx;LogFileName=<suite>.trx" --results-directory "$WorkRoot\trx" -m:1`:
   - `$HelengineRoot\engine\helengine.editor.tests\helengine.editor.tests.csproj`
   - `$HelengineRoot\engine\helengine.render.validation.tests\helengine.render.validation.tests.csproj`
   - `$RepoRoot\builder.tests\helengine.windows.builder.tests.csproj`, with `-p:HelEngineRoot=$HelengineRoot`

   Then run `trx-failing` into `$WorkRoot\trx\<suite>.failing.txt`.
   - **Record** copies that file to `regression\baselines\<suite>.failing.txt`.
   - **Verify** runs `compare-failing regression\baselines\<suite>.failing.txt <current>`.

   Note: `dotnet test` returns non-zero when tests fail. Do not throw on that; only throw if no TRX file was produced.
7. **Record** writes `regression\golden\manifest.json`: `{ "frames":30, "fixedDelta":0.016666, "width":640, "height":360, "scenes":[{"id":..., "kind":"smoke|golden", "status":"stable|unstable"}] }`.
8. **Summary:** one line per check (`PASS|FAIL|SKIP <kind> <name> <detail>`), then `RESULT: PASS` or `RESULT: FAIL (<n> failing)`. Exit 0 only on PASS. Record exits 0 when every build and run succeeded, even if some scenes were marked unstable, and it prints them.

- [ ] **Step 1: Extend the source tests.** Assert the script contains:
  - `launch_in_emulator.ps1`, `-Wait`, `--fixed-delta`, `0.016666`, `--frames`, `'30'`, `record-golden`, `compare-failing`, `trx-failing`, `stable`, `manifest.json`, `profile.json`, `RESULT:`;
  - `helengine.editor.tests.csproj` and `helengine.render.validation.tests.csproj`.
- [ ] **Step 2: Run the tests, confirm they fail, implement, then run them again and confirm they pass.**
- [ ] **Step 3: Dry run** `-Verify` before any goldens exist: it must fail clearly with `golden missing: <path>` and `baseline missing: <path>`, not crash.
- [ ] **Step 4: Commit** with `feat(regression): record and verify goldens, smoke runs and test-failure baselines`.

---

### Task 6: Acceptance: record on current behavior, then verify twice

**Files:**
- Create or update: `regression/golden/*.png`, `regression/golden/manifest.json`, `regression/baselines/*.failing.txt`
- Modify: `regression/README.md` (the recorded date, the machine note, the unstable scenes list)

- [ ] **Step 1:** Run `powershell -File scripts\run-regression.ps1 -Record 2>&1 | Select-Object -Last 60`. Expected: builds, 11 scene runs × 2, three suites, and goldens written. Record the output.
- [ ] **Step 2:** Run `-Verify` → `RESULT: PASS`. Record the output.
- [ ] **Step 3:** Run `-Verify` again → `RESULT: PASS`. This proves no flakiness. If a scene fails intermittently, mark it unstable in the manifest by re-recording, and explain why in the README. Never loosen the thresholds.
- [ ] **Step 4: Negative check.**
  1. Temporarily change the clear color or one material value in the **worktree copy of the project** (`$WorkRoot\project`, not Helena's project) for one rendering scene.
  2. Rebuild with `-Verify`: expect `FAIL golden <that scene>` with a diff PNG.
  3. Revert the change and verify that the run passes again.

  Record both results.
- [ ] **Step 5: No-arguments boot.** Launch the built player through the launcher without arguments. It must stay running (check after 5 s), and its startup log must show the default startup scene. Then `Stop-Process`. Record the result.
- [ ] **Step 6: Commit** with `test(regression): record the Windows player goldens and test-failure baselines`.
