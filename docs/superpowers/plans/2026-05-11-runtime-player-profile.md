# Runtime Player Profile Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a persisted `profile.json` beside `helengine_windows.exe` so the Windows player seeds resolution from deployment defaults on first launch and repairs invalid profile content automatically on later launches.

**Architecture:** Keep the generated runtime player-settings manifest as the deployment-default source, then add a thin native runtime profile model plus a file-backed loader in the Windows host. `win32_application.cpp` should consume one resolved runtime profile instead of reaching into generated defaults directly.

**Tech Stack:** C#, xUnit, .NET 9, C++20, Win32 host bootstrap, CMake

---

## File Structure

### Existing Files To Modify

- `builder.tests/WindowsRuntimeNativeManifestWriterTests.cs`
  Keeps the deployment-default manifest coverage green while the runtime profile feature is added.
- `CMakeLists.txt`
  Needs to compile the new native runtime player-profile sources.
- `src/platform/windows/win32/win32_application.hpp`
  Needs one new helper declaration for runtime profile resolution.
- `src/platform/windows/win32/win32_application.cpp`
  Must stop sizing the window from manifest functions directly and instead use the resolved runtime profile loader path.

### New Files To Create

- `src/platform/windows/runtime/runtime_player_profile.hpp`
  Holds the generic runtime profile model with width and height.
- `src/platform/windows/runtime/runtime_player_profile.cpp`
  Owns simple validation logic for the runtime profile model.
- `src/platform/windows/runtime/runtime_player_profile_loader.hpp`
  Declares the loader that resolves, writes, repairs, and returns `profile.json`.
- `src/platform/windows/runtime/runtime_player_profile_loader.cpp`
  Implements JSON parsing, validation, repair, and log-friendly status messages.
- `docs/superpowers/plans/2026-05-11-runtime-player-profile.md`
  This plan document.

### Notes

- There is no existing native C++ unit-test harness in this repo today.
- Keep the new C++ loader small and deterministic.
- Use `builder.tests` to protect the generated default-manifest contract, then verify runtime behavior with a live player run after code lands.

---

### Task 1: Lock The Default Manifest Contract Before Runtime Changes

**Files:**
- Modify: `builder.tests/WindowsRuntimeNativeManifestWriterTests.cs`
- Test: `builder.tests/WindowsRuntimeNativeManifestWriterTests.cs`

- [ ] **Step 1: Write the failing test**

Add a test that asserts the runtime player-settings manifest still emits the exact default width and height symbols that the future profile loader will consume as its deployment seed:

```csharp
/// <summary>
/// Ensures runtime player profile seeding keeps using the generated deployment defaults.
/// </summary>
[Fact]
public void Write_when_graphics_options_define_default_resolution_emits_runtime_profile_seed_values() {
    PlatformBuildManifest manifest = new(
        2,
        "project",
        "1.0.0",
        "1.0.0",
        "DemoDiscMainMenu",
        [
            new PlatformBuildScene(
                "DemoDiscMainMenu",
                "DemoDiscMainMenu",
                "cooked/scenes/DemoDiscMainMenu.hasset",
                [],
                [new KeyValuePair<string, string>("cooked-relative-path", "cooked/scenes/DemoDiscMainMenu.hasset")])
        ],
        [],
        [],
        [],
        [],
        new PlatformContainerWritePlan(string.Empty, []));
    Dictionary<string, string> graphicsOptionValues = new(StringComparer.OrdinalIgnoreCase) {
        ["default-width"] = "640",
        ["default-height"] = "480"
    };

    WindowsRuntimeNativeManifestWriter writer = new();
    writer.Write(GeneratedCoreRootPath, manifest, graphicsOptionValues);

    string runtimeRootPath = Path.Combine(GeneratedCoreRootPath, "runtime");
    string settingsSourcePath = Path.Combine(runtimeRootPath, "runtime_player_settings_manifest.cpp");
    string settingsSource = File.ReadAllText(settingsSourcePath);

    Assert.Contains("kRuntimeDefaultWindowWidth = 640", settingsSource, StringComparison.Ordinal);
    Assert.Contains("kRuntimeDefaultWindowHeight = 480", settingsSource, StringComparison.Ordinal);
    Assert.Contains("he_get_runtime_default_window_width", settingsSource, StringComparison.Ordinal);
    Assert.Contains("he_get_runtime_default_window_height", settingsSource, StringComparison.Ordinal);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```powershell
rtk dotnet test builder.tests\helengine.windows.builder.tests.csproj --filter Write_when_graphics_options_define_default_resolution_emits_runtime_profile_seed_values
```

Expected: FAIL because the new test does not exist yet, or because the current assertions are not present.

- [ ] **Step 3: Write the minimal test implementation**

Update `WindowsRuntimeNativeManifestWriterTests.cs` by adding the new focused test near the existing resolution-manifest coverage and keep the original broad test or fold its assertions into the new name if that keeps the file cleaner:

```csharp
Assert.Contains("kRuntimeDefaultWindowWidth = 640", settingsSource, StringComparison.Ordinal);
Assert.Contains("kRuntimeDefaultWindowHeight = 480", settingsSource, StringComparison.Ordinal);
Assert.Contains("he_get_runtime_default_window_width", settingsSource, StringComparison.Ordinal);
Assert.Contains("he_get_runtime_default_window_height", settingsSource, StringComparison.Ordinal);
```

- [ ] **Step 4: Run test to verify it passes**

Run:

```powershell
rtk dotnet test builder.tests\helengine.windows.builder.tests.csproj --filter Write_when_graphics_options_define_default_resolution_emits_runtime_profile_seed_values
```

Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add builder.tests/WindowsRuntimeNativeManifestWriterTests.cs
git commit -m "test: lock runtime profile seed manifest defaults"
```

---

### Task 2: Add The Native Runtime Player Profile Model And Loader

**Files:**
- Create: `src/platform/windows/runtime/runtime_player_profile.hpp`
- Create: `src/platform/windows/runtime/runtime_player_profile.cpp`
- Create: `src/platform/windows/runtime/runtime_player_profile_loader.hpp`
- Create: `src/platform/windows/runtime/runtime_player_profile_loader.cpp`
- Modify: `CMakeLists.txt`
- Test: `builder.tests/WindowsRuntimeNativeManifestWriterTests.cs`

- [ ] **Step 1: Write the failing test**

Add a builder-facing regression that documents the runtime bootstrap expectation: the generated manifest still only provides deployment defaults, and the runtime profile feature must not replace that interface with ad hoc literals.

Use a narrow test name if Task 1 reused the existing test:

```csharp
[Fact]
public void Write_when_graphics_options_define_default_resolution_keeps_runtime_default_getters_for_profile_loader() {
    // same setup as Task 1

    Assert.Contains("int he_get_runtime_default_window_width()", settingsSource, StringComparison.Ordinal);
    Assert.Contains("int he_get_runtime_default_window_height()", settingsSource, StringComparison.Ordinal);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```powershell
rtk dotnet test builder.tests\helengine.windows.builder.tests.csproj --filter Write_when_graphics_options_define_default_resolution_keeps_runtime_default_getters_for_profile_loader
```

Expected: FAIL until the new test is added.

- [ ] **Step 3: Add the runtime profile model**

Create `runtime_player_profile.hpp` with a generic model:

```cpp
#pragma once

namespace helengine::windows {
    /// Stores one resolved runtime player profile for the native host.
    struct RuntimePlayerProfile {
        /// Stores the resolved startup window width in pixels.
        int ResolutionWidth;

        /// Stores the resolved startup window height in pixels.
        int ResolutionHeight;
    };
}
```

Create `runtime_player_profile.cpp` with one validation helper:

```cpp
#include "platform/windows/runtime/runtime_player_profile.hpp"

#include <stdexcept>

namespace helengine::windows {
    /// Validates one runtime player profile before the host consumes it.
    void ValidateRuntimePlayerProfile(const RuntimePlayerProfile& profile) {
        if (profile.ResolutionWidth <= 0) {
            throw std::runtime_error("Runtime player profile width must be positive.");
        } else if (profile.ResolutionHeight <= 0) {
            throw std::runtime_error("Runtime player profile height must be positive.");
        }
    }
}
```

- [ ] **Step 4: Add the runtime profile loader**

Create `runtime_player_profile_loader.hpp`:

```cpp
#pragma once

#include <filesystem>
#include <string>

#include "platform/windows/runtime/runtime_player_profile.hpp"

namespace helengine::windows {
    /// Loads and repairs the persisted runtime player profile beside the executable.
    class RuntimePlayerProfileLoader {
    public:
        /// Resolves one runtime player profile from disk or deployment defaults.
        RuntimePlayerProfile LoadOrCreateProfile(
            const std::filesystem::path& applicationDirectoryPath,
            int defaultResolutionWidth,
            int defaultResolutionHeight,
            std::string& lifecycleMessage) const;

    private:
        /// Resolves the profile path beside the executable.
        std::filesystem::path ResolveProfilePath(const std::filesystem::path& applicationDirectoryPath) const;
    };
}
```

Create `runtime_player_profile_loader.cpp` with small, explicit parsing and repair logic:

```cpp
#include "platform/windows/runtime/runtime_player_profile_loader.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace helengine::windows {
    RuntimePlayerProfile RuntimePlayerProfileLoader::LoadOrCreateProfile(
        const std::filesystem::path& applicationDirectoryPath,
        int defaultResolutionWidth,
        int defaultResolutionHeight,
        std::string& lifecycleMessage) const {
        RuntimePlayerProfile defaultProfile {
            defaultResolutionWidth,
            defaultResolutionHeight
        };
        ValidateRuntimePlayerProfile(defaultProfile);

        std::filesystem::path profilePath = ResolveProfilePath(applicationDirectoryPath);
        if (!std::filesystem::exists(profilePath)) {
            WriteProfile(profilePath, defaultProfile);
            lifecycleMessage = "profile.json was seeded from deployment defaults.";
            return defaultProfile;
        }

        try {
            RuntimePlayerProfile loadedProfile = ReadProfile(profilePath);
            ValidateRuntimePlayerProfile(loadedProfile);
            lifecycleMessage = "profile.json loaded successfully.";
            return loadedProfile;
        } catch (const std::exception&) {
            WriteProfile(profilePath, defaultProfile);
            lifecycleMessage = "profile.json was invalid and was recreated from deployment defaults.";
            return defaultProfile;
        }
    }
}
```

Keep helper functions file-local in the `.cpp` only if the repo already permits that pattern in C++ here. If not, add them as private member methods on the loader class.

- [ ] **Step 5: Compile the new runtime sources**

Update `CMakeLists.txt`:

```cmake
set(HELENGINE_WINDOWS_SOURCES
    src/main.cpp
    src/platform/windows/runtime/runtime_player_profile.cpp
    src/platform/windows/runtime/runtime_player_profile_loader.cpp
    src/platform/windows/win32/win32_application.cpp
    src/platform/windows/win32/win32_window.cpp
    src/platform/windows/directx11/directx11_bootstrap.cpp
    src/platform/windows/directx11/directx11_presenter.cpp
)
```

- [ ] **Step 6: Run builder tests to verify they pass**

Run:

```powershell
rtk dotnet test builder.tests\helengine.windows.builder.tests.csproj --filter WindowsRuntimeNativeManifestWriterTests
```

Expected: PASS

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt src/platform/windows/runtime/runtime_player_profile.hpp src/platform/windows/runtime/runtime_player_profile.cpp src/platform/windows/runtime/runtime_player_profile_loader.hpp src/platform/windows/runtime/runtime_player_profile_loader.cpp builder.tests/WindowsRuntimeNativeManifestWriterTests.cs
git commit -m "feat: add runtime player profile loader"
```

---

### Task 3: Wire The Loader Into The Win32 Bootstrap

**Files:**
- Modify: `src/platform/windows/win32/win32_application.hpp`
- Modify: `src/platform/windows/win32/win32_application.cpp`
- Test: manual live player startup verification

- [ ] **Step 1: Write the failing test**

Because there is no native test harness today, capture the failure as a manual reproducible check before changing code:

```text
1. Delete profile.json beside helengine_windows.exe
2. Launch the player
3. Observe that no profile.json is created and startup still sizes the window directly from manifest getters
```

Expected current failure:

```text
profile.json is absent after launch
```

- [ ] **Step 2: Add the bootstrap-facing helper declaration**

Update `win32_application.hpp`:

```cpp
/// Resolves the runtime player profile that controls initial window sizing.
RuntimePlayerProfile ResolveRuntimePlayerProfile() const;
```

Include the runtime profile header:

```cpp
#include "platform/windows/runtime/runtime_player_profile.hpp"
```

- [ ] **Step 3: Implement bootstrap profile resolution**

Update `win32_application.cpp` imports:

```cpp
#include "platform/windows/runtime/runtime_player_profile_loader.hpp"
```

Add the new helper:

```cpp
RuntimePlayerProfile Win32Application::ResolveRuntimePlayerProfile() const {
    int defaultWindowWidth = 1280;
    int defaultWindowHeight = 720;

#if __has_include("runtime/runtime_player_settings_manifest.hpp")
    defaultWindowWidth = he_get_runtime_default_window_width();
    defaultWindowHeight = he_get_runtime_default_window_height();
#endif

    RuntimePlayerProfileLoader loader;
    std::string lifecycleMessage;
    RuntimePlayerProfile profile = loader.LoadOrCreateProfile(
        ResolveApplicationDirectoryPath(),
        defaultWindowWidth,
        defaultWindowHeight,
        lifecycleMessage);

    if (!lifecycleMessage.empty()) {
        WriteLifecycleLog(lifecycleMessage.c_str());
    }

    return profile;
}
```

Then update `CreateMainWindow()`:

```cpp
RuntimePlayerProfile profile = ResolveRuntimePlayerProfile();

MainWindow = std::make_unique<Win32Window>(
    L"HelEngine Windows Host",
    profile.ResolutionWidth,
    profile.ResolutionHeight);
MainWindow->Create();
MainWindow->Show();
```

Update the resolution log to print `profile.ResolutionWidth` and `profile.ResolutionHeight`.

- [ ] **Step 4: Build the player to verify the native path compiles**

Run:

```powershell
rtk dotnet test builder.tests\helengine.windows.builder.tests.csproj --filter WindowsPlatformAssetBuilderTests
```

Expected: PASS on the existing Windows build pipeline tests, proving the builder still compiles and stages the native host correctly.

- [ ] **Step 5: Perform the manual startup verification**

Run a real Windows export, launch it, and verify:

```text
profile.json is created beside helengine_windows.exe
startup log mentions the seeded profile
window size matches the seeded width and height
```

- [ ] **Step 6: Commit**

```bash
git add src/platform/windows/win32/win32_application.hpp src/platform/windows/win32/win32_application.cpp
git commit -m "feat: load startup resolution from profile json"
```

---

### Task 4: Verify Profile Repair And Persisted Overrides

**Files:**
- Modify: `src/platform/windows/runtime/runtime_player_profile_loader.cpp`
- Test: manual live player verification

- [ ] **Step 1: Write the failing manual reproduction**

Before the repair logic is considered complete, reproduce two bad-profile cases beside a built player:

```text
Case A: profile.json contains malformed JSON like "{"
Case B: profile.json contains {"resolutionWidth":0,"resolutionHeight":480}
```

Expected current failure before the final repair behavior is complete:

```text
player throws or fails to repair the file cleanly
```

- [ ] **Step 2: Tighten the loader repair path**

Make sure `LoadOrCreateProfile(...)` catches malformed or invalid content, rewrites the file from generated defaults, and returns the repaired profile:

```cpp
try {
    RuntimePlayerProfile loadedProfile = ReadProfile(profilePath);
    ValidateRuntimePlayerProfile(loadedProfile);
    lifecycleMessage = "profile.json loaded successfully.";
    return loadedProfile;
} catch (const std::exception&) {
    WriteProfile(profilePath, defaultProfile);
    lifecycleMessage = "profile.json was invalid and was recreated from deployment defaults.";
    return defaultProfile;
}
```

Write the file in the exact flat shape:

```json
{
  "resolutionWidth": 640,
  "resolutionHeight": 480
}
```

- [ ] **Step 3: Verify persisted overrides manually**

Edit `profile.json` to:

```json
{
  "resolutionWidth": 800,
  "resolutionHeight": 600
}
```

Launch the player and verify:

```text
the startup log reports the profile was loaded successfully
the window opens at 800x600
the file is not rewritten
```

- [ ] **Step 4: Verify repair behavior manually**

Rerun the malformed and invalid-value cases and verify:

```text
the player starts successfully
profile.json is rewritten to deployment defaults
the startup log reports the repair
```

- [ ] **Step 5: Run the builder test suite again**

Run:

```powershell
rtk dotnet test builder.tests\helengine.windows.builder.tests.csproj --no-restore
```

Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add src/platform/windows/runtime/runtime_player_profile_loader.cpp
git commit -m "fix: repair invalid runtime player profiles"
```

---

## Spec Coverage Check

- Deployment defaults remain the source of truth: covered by Task 1 and Task 2.
- Generic runtime profile model and loader shape: covered by Task 2.
- `profile.json` beside the executable: covered by Task 2 and Task 3.
- Seed missing file from deployment defaults: covered by Task 2 and Task 3.
- Repair malformed or invalid files from deployment defaults: covered by Task 4.
- Clear startup logging for seed, load, and repair: covered by Task 3 and Task 4.
- Future platform seam without cross-platform module work now: covered by Task 2 architecture and file split.

## Placeholder Scan

- No `TODO` or `TBD` placeholders remain.
- All tasks reference exact files.
- All run commands are explicit.
- Manual verification tasks are explicit where no native unit harness exists yet.

## Type Consistency Check

- `RuntimePlayerProfile`
- `RuntimePlayerProfileLoader`
- `ResolveRuntimePlayerProfile()`
- `ResolutionWidth`
- `ResolutionHeight`

These names are used consistently across the plan.
