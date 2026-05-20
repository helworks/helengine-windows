# Windows Multi-Input Sources Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add end-to-end Windows player support for embedding any subset of `DirectInput`, `XInput`, and `SteamInput`, then poll and deduplicate those enabled gamepad backends at runtime.

**Architecture:** Keep all feature work inside `helengine-windows`. The builder already exposes platform-defined settings to the editor and the shared `helengine.input` contract already carries `InputGamepadState[]` through `InputFrameState`, so the implementation should extend the Windows builder/native handoff and populate the existing gamepad frame data rather than inventing a new engine-wide input model.

**Tech Stack:** C# (`builder`, `builder.tests`), CMake, Win32 C++, XInput, DirectInput 8, Steamworks `SteamInput`, generated `helengine.input` C++ bindings

---

## File Structure

### Existing files to modify

- `builder/WindowsPlatformDefinitionFactory.cs`
- `builder/WindowsRuntimeNativeManifestWriter.cs`
- `builder/WindowsNativeBuildExecutor.cs`
- `builder/WindowsBuildWorkspace.cs`
- `builder.tests/WindowsPlatformAssetBuilderTests.cs`
- `builder.tests/WindowsRuntimeNativeManifestWriterTests.cs`
- `builder.tests/WindowsNativeBuildExecutorTests.cs`
- `CMakeLists.txt`
- `src/platform/windows/win32/win32_input_bridge.hpp`
- `src/platform/windows/win32/win32_input_bridge.cpp`

### New generated runtime manifest outputs

- `generated/helengine.core/runtime/runtime_input_backends_manifest.hpp`
- `generated/helengine.core/runtime/runtime_input_backends_manifest.cpp`

### New native input files

- `src/platform/windows/input/windows_gamepad_backend_kind.hpp`
- `src/platform/windows/input/windows_gamepad_device_identity.hpp`
- `src/platform/windows/input/windows_gamepad_device_state.hpp`
- `src/platform/windows/input/i_windows_gamepad_backend.hpp`
- `src/platform/windows/input/windows_gamepad_backend_mux.hpp`
- `src/platform/windows/input/windows_gamepad_backend_mux.cpp`
- `src/platform/windows/input/windows_gamepad_dedupe_service.hpp`
- `src/platform/windows/input/windows_gamepad_dedupe_service.cpp`
- `src/platform/windows/input/windows_xinput_backend.hpp`
- `src/platform/windows/input/windows_xinput_backend.cpp`
- `src/platform/windows/input/windows_directinput_backend.hpp`
- `src/platform/windows/input/windows_directinput_backend.cpp`
- `src/platform/windows/input/windows_steam_input_backend.hpp`
- `src/platform/windows/input/windows_steam_input_backend.cpp`

### External dependency note

- Builder tests in this worktree must pass `HelEngineRoot=C:\dev\helworks\helengine` because the repo-local default `..\..\helengine` resolves incorrectly from `C:\dev\worktrees\windows-multi-input-sources`.

### Runtime constants and environment knobs

- New CMake cache booleans:
  - `HELENGINE_WINDOWS_ENABLE_XINPUT`
  - `HELENGINE_WINDOWS_ENABLE_DIRECTINPUT`
  - `HELENGINE_WINDOWS_ENABLE_STEAMINPUT`
- New optional Steamworks cache path:
  - `HELENGINE_STEAMWORKS_SDK_ROOT`
- New native-build environment fallback:
  - `HELENGINE_STEAMWORKS_SDK_ROOT`

### Shared `helengine.input` seam already present

- `C:\dev\helworks\helengine\engine\helengine.input\InputFrameState.cs`
- `C:\dev\helworks\helengine\engine\helengine.input\InputGamepadState.cs`
- `C:\dev\helworks\helengine\engine\helengine.input\InputSystem.cs`

Those files already expose `InputFrameState.Gamepads`, `InputFrameState.GamepadCount`, and `InputSystem.GetGamepadState()`, so this feature should target the Windows host only unless generated C++ bindings are unexpectedly missing the matching setters.

### Task 1: Add Builder-Facing Backend Toggles

**Files:**
- Modify: `builder/WindowsPlatformDefinitionFactory.cs`
- Test: `builder.tests/WindowsPlatformAssetBuilderTests.cs`

- [ ] **Step 1: Write the failing builder-definition test**

```csharp
[Fact]
public void Descriptor_and_definition_expose_input_backend_embedding_toggles() {
    WindowsPlatformAssetBuilder builder = new();

    PlatformGraphicsProfileDefinition graphicsProfile = Assert.Single(
        builder.Definition.GraphicsProfiles,
        profile => profile.ProfileId == "directx11");

    Assert.Contains(graphicsProfile.Settings, setting =>
        setting.SettingId == "embed-xinput" &&
        setting.Kind == PlatformSettingKind.Boolean &&
        setting.DefaultValue == "true");
    Assert.Contains(graphicsProfile.Settings, setting =>
        setting.SettingId == "embed-directinput" &&
        setting.Kind == PlatformSettingKind.Boolean &&
        setting.DefaultValue == "true");
    Assert.Contains(graphicsProfile.Settings, setting =>
        setting.SettingId == "embed-steaminput" &&
        setting.Kind == PlatformSettingKind.Boolean &&
        setting.DefaultValue == "false");
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run:

```powershell
rtk dotnet test builder.tests/helengine.windows.builder.tests.csproj -p:HelEngineRoot=C:\dev\helworks\helengine --filter Descriptor_and_definition_expose_input_backend_embedding_toggles -v minimal
```

Expected: FAIL because the three settings are not yet present in `WindowsPlatformDefinitionFactory`.

- [ ] **Step 3: Add the three platform settings to the Windows graphics profile**

```csharp
new PlatformSettingDefinition(
    "embed-xinput",
    "Embed XInput",
    PlatformSettingKind.Boolean,
    "true",
    true,
    []),
new PlatformSettingDefinition(
    "embed-directinput",
    "Embed DirectInput",
    PlatformSettingKind.Boolean,
    "true",
    true,
    []),
new PlatformSettingDefinition(
    "embed-steaminput",
    "Embed SteamInput",
    PlatformSettingKind.Boolean,
    "false",
    true,
    [])
```

- [ ] **Step 4: Re-run the focused builder-definition test**

Run:

```powershell
rtk dotnet test builder.tests/helengine.windows.builder.tests.csproj -p:HelEngineRoot=C:\dev\helworks\helengine --filter Descriptor_and_definition_expose_input_backend_embedding_toggles -v minimal
```

Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add builder/WindowsPlatformDefinitionFactory.cs builder.tests/WindowsPlatformAssetBuilderTests.cs
git commit -m "feat: add windows input backend build toggles"
```

### Task 2: Emit Runtime Backend Manifests and Native Build Flags

**Files:**
- Modify: `builder/WindowsRuntimeNativeManifestWriter.cs`
- Modify: `builder/WindowsBuildWorkspace.cs`
- Modify: `builder/WindowsNativeBuildExecutor.cs`
- Modify: `builder.tests/WindowsRuntimeNativeManifestWriterTests.cs`
- Modify: `builder.tests/WindowsNativeBuildExecutorTests.cs`

- [ ] **Step 1: Write the failing manifest-writer test for runtime backend emission**

```csharp
[Fact]
public void Write_when_graphics_options_define_input_backends_emits_runtime_input_backend_manifest() {
    Dictionary<string, string> graphicsOptionValues = new(StringComparer.OrdinalIgnoreCase) {
        ["default-width"] = "1280",
        ["default-height"] = "720",
        ["embed-xinput"] = "true",
        ["embed-directinput"] = "false",
        ["embed-steaminput"] = "true"
    };

    WindowsRuntimeNativeManifestWriter writer = new();
    writer.Write(GeneratedCoreRootPath, manifest, graphicsOptionValues);

    string runtimeRootPath = Path.Combine(GeneratedCoreRootPath, "runtime");
    string sourcePath = Path.Combine(runtimeRootPath, "runtime_input_backends_manifest.cpp");
    string source = File.ReadAllText(sourcePath);

    Assert.Contains("he_runtime_embed_xinput", source, StringComparison.Ordinal);
    Assert.Contains("he_runtime_embed_directinput", source, StringComparison.Ordinal);
    Assert.Contains("he_runtime_embed_steaminput", source, StringComparison.Ordinal);
    Assert.Contains("true", source, StringComparison.Ordinal);
    Assert.Contains("false", source, StringComparison.Ordinal);
}
```

- [ ] **Step 2: Write the failing native-build-arguments test for backend flags**

```csharp
[Fact]
public void BuildConfigureArguments_forwards_requested_input_backend_flags() {
    string arguments = InvokePrivateCommandBuilder(
        "BuildConfigureArguments",
        @"C:\repo",
        @"C:\build",
        @"C:\generated",
        @"C:\staged-code",
        @"C:\vs\VsDevCmd.bat",
        new WindowsNativeInputBackendSelection(true, false, true),
        @"C:\steamworks");

    Assert.Contains("-DHELENGINE_WINDOWS_ENABLE_XINPUT=ON", arguments, StringComparison.Ordinal);
    Assert.Contains("-DHELENGINE_WINDOWS_ENABLE_DIRECTINPUT=OFF", arguments, StringComparison.Ordinal);
    Assert.Contains("-DHELENGINE_WINDOWS_ENABLE_STEAMINPUT=ON", arguments, StringComparison.Ordinal);
    Assert.Contains("-DHELENGINE_STEAMWORKS_SDK_ROOT=\"C:\\steamworks\"", arguments, StringComparison.Ordinal);
}
```

- [ ] **Step 3: Run the focused tests to verify they fail**

Run:

```powershell
rtk dotnet test builder.tests/helengine.windows.builder.tests.csproj -p:HelEngineRoot=C:\dev\helworks\helengine --filter "Write_when_graphics_options_define_input_backends_emits_runtime_input_backend_manifest|BuildConfigureArguments_forwards_requested_input_backend_flags" -v minimal
```

Expected: FAIL because neither runtime backend manifests nor backend-specific configure flags exist yet.

- [ ] **Step 4: Implement runtime backend manifest generation**

```csharp
static string BuildInputBackendsManifestHeaderContents() {
    return """
    #pragma once

    bool he_runtime_embed_xinput();
    bool he_runtime_embed_directinput();
    bool he_runtime_embed_steaminput();
    """;
}

static string BuildInputBackendsManifestSourceContents(bool embedXInput, bool embedDirectInput, bool embedSteamInput) {
    return $$"""
    #include "runtime/runtime_input_backends_manifest.hpp"

    bool he_runtime_embed_xinput() { return {{embedXInput.ToString().ToLowerInvariant()}}; }
    bool he_runtime_embed_directinput() { return {{embedDirectInput.ToString().ToLowerInvariant()}}; }
    bool he_runtime_embed_steaminput() { return {{embedSteamInput.ToString().ToLowerInvariant()}}; }
    """;
}
```

- [ ] **Step 5: Add native-build selection plumbing**

```csharp
internal readonly record struct WindowsNativeInputBackendSelection(
    bool EmbedXInput,
    bool EmbedDirectInput,
    bool EmbedSteamInput);
```

```csharp
$"-DHELENGINE_WINDOWS_ENABLE_XINPUT={(selection.EmbedXInput ? "ON" : "OFF")}"
$"-DHELENGINE_WINDOWS_ENABLE_DIRECTINPUT={(selection.EmbedDirectInput ? "ON" : "OFF")}"
$"-DHELENGINE_WINDOWS_ENABLE_STEAMINPUT={(selection.EmbedSteamInput ? "ON" : "OFF")}"
```

- [ ] **Step 6: Re-run the focused tests**

Run:

```powershell
rtk dotnet test builder.tests/helengine.windows.builder.tests.csproj -p:HelEngineRoot=C:\dev\helworks\helengine --filter "Write_when_graphics_options_define_input_backends_emits_runtime_input_backend_manifest|BuildConfigureArguments_forwards_requested_input_backend_flags" -v minimal
```

Expected: PASS

- [ ] **Step 7: Commit**

```bash
git add builder/WindowsRuntimeNativeManifestWriter.cs builder/WindowsBuildWorkspace.cs builder/WindowsNativeBuildExecutor.cs builder.tests/WindowsRuntimeNativeManifestWriterTests.cs builder.tests/WindowsNativeBuildExecutorTests.cs
git commit -m "feat: emit windows input backend runtime manifests"
```

### Task 3: Add Conditional Native Build Inputs and Steamworks Validation

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `builder/WindowsNativeBuildExecutor.cs`

- [ ] **Step 1: Extend CMake with backend toggles and Steamworks validation**

```cmake
option(HELENGINE_WINDOWS_ENABLE_XINPUT "Compile XInput gamepad backend." ON)
option(HELENGINE_WINDOWS_ENABLE_DIRECTINPUT "Compile DirectInput gamepad backend." ON)
option(HELENGINE_WINDOWS_ENABLE_STEAMINPUT "Compile SteamInput gamepad backend." OFF)
set(HELENGINE_STEAMWORKS_SDK_ROOT "" CACHE PATH "Steamworks SDK root used for SteamInput support.")

if(HELENGINE_WINDOWS_ENABLE_STEAMINPUT)
    if("${HELENGINE_STEAMWORKS_SDK_ROOT}" STREQUAL "")
        message(FATAL_ERROR "HELENGINE_STEAMWORKS_SDK_ROOT is required when HELENGINE_WINDOWS_ENABLE_STEAMINPUT=ON.")
    endif()
endif()
```

- [ ] **Step 2: Wire conditional native sources and libraries**

```cmake
if(HELENGINE_WINDOWS_ENABLE_XINPUT)
    list(APPEND HELENGINE_WINDOWS_SOURCES src/platform/windows/input/windows_xinput_backend.cpp)
    target_link_libraries(helengine_windows PRIVATE xinput)
endif()

if(HELENGINE_WINDOWS_ENABLE_DIRECTINPUT)
    list(APPEND HELENGINE_WINDOWS_SOURCES src/platform/windows/input/windows_directinput_backend.cpp)
    target_link_libraries(helengine_windows PRIVATE dinput8 dxguid)
endif()
```

- [ ] **Step 3: Add Steamworks include and library wiring**

```cmake
if(HELENGINE_WINDOWS_ENABLE_STEAMINPUT)
    target_include_directories(helengine_windows PRIVATE "${HELENGINE_STEAMWORKS_SDK_ROOT}/public")
    target_link_directories(helengine_windows PRIVATE "${HELENGINE_STEAMWORKS_SDK_ROOT}/redistributable_bin/win64")
    target_link_libraries(helengine_windows PRIVATE steam_api64)
endif()
```

- [ ] **Step 4: Re-run the backend-argument tests plus the broader builder suite**

Run:

```powershell
rtk dotnet test builder.tests/helengine.windows.builder.tests.csproj -p:HelEngineRoot=C:\dev\helworks\helengine -v minimal
```

Expected: PASS for the managed test suite.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt builder/WindowsNativeBuildExecutor.cs
git commit -m "feat: add conditional windows gamepad backend build flags"
```

### Task 4: Introduce Native Backend Multiplexing and Dedupe Seams

**Files:**
- Create: `src/platform/windows/input/windows_gamepad_backend_kind.hpp`
- Create: `src/platform/windows/input/windows_gamepad_device_identity.hpp`
- Create: `src/platform/windows/input/windows_gamepad_device_state.hpp`
- Create: `src/platform/windows/input/i_windows_gamepad_backend.hpp`
- Create: `src/platform/windows/input/windows_gamepad_backend_mux.hpp`
- Create: `src/platform/windows/input/windows_gamepad_backend_mux.cpp`
- Create: `src/platform/windows/input/windows_gamepad_dedupe_service.hpp`
- Create: `src/platform/windows/input/windows_gamepad_dedupe_service.cpp`
- Modify: `src/platform/windows/win32/win32_input_bridge.hpp`
- Modify: `src/platform/windows/win32/win32_input_bridge.cpp`

- [ ] **Step 1: Define backend identity and per-device state records**

```cpp
enum class WindowsGamepadBackendKind {
    DirectInput = 0,
    XInput = 1,
    SteamInput = 2
};

struct WindowsGamepadDeviceIdentity {
    WindowsGamepadBackendKind BackendKind;
    std::uint64_t BackendDeviceId;
    std::uint32_t VendorId;
    std::uint32_t ProductId;
    std::uint32_t SlotIndex;
};

struct WindowsGamepadDeviceState {
    WindowsGamepadDeviceIdentity Identity;
    InputGamepadState Gamepad;
};
```

- [ ] **Step 2: Define the backend interface and multiplexer surface**

```cpp
class IWindowsGamepadBackend {
public:
    virtual ~IWindowsGamepadBackend() = default;
    virtual WindowsGamepadBackendKind GetBackendKind() const = 0;
    virtual bool TryInitialize(std::string& failureMessage) = 0;
    virtual void CaptureFrame(List<WindowsGamepadDeviceState>& devices) = 0;
};
```

- [ ] **Step 3: Implement fixed-priority dedupe in one isolated service**

```cpp
class WindowsGamepadDedupeService {
public:
    static void MergeAndDedupe(
        const List<WindowsGamepadDeviceState>& candidates,
        List<InputGamepadState>& mergedGamepads);
};
```

```cpp
static int ResolveBackendPriority(WindowsGamepadBackendKind backendKind) {
    switch (backendKind) {
        case WindowsGamepadBackendKind::SteamInput:
            return 3;
        case WindowsGamepadBackendKind::XInput:
            return 2;
        default:
            return 1;
    }
}
```

- [ ] **Step 4: Update `Win32InputBackend` to populate gamepads through the mux**

```cpp
InputFrameState Win32InputBackend::CaptureFrame() {
    InputFrameState frame;
    frame.set_Keyboard(CaptureKeyboardState());
    frame.set_Mouse(CaptureMouseState());

    List<InputGamepadState> mergedGamepads;
    GamepadBackendMux.CaptureFrame(mergedGamepads);
    frame.set_Gamepads(&mergedGamepads);
    frame.set_GamepadCount(mergedGamepads.get_Count());
    return frame;
}
```

- [ ] **Step 5: Build the native host with generated core included**

Run:

```powershell
rtk dotnet test builder.tests/helengine.windows.builder.tests.csproj -p:HelEngineRoot=C:\dev\helworks\helengine -v minimal
```

Expected: PASS for managed tests and no compile errors introduced by the new native seam declarations checked into the repo.

- [ ] **Step 6: Commit**

```bash
git add src/platform/windows/input src/platform/windows/win32/win32_input_bridge.hpp src/platform/windows/win32/win32_input_bridge.cpp
git commit -m "feat: add windows gamepad backend mux seam"
```

### Task 5: Implement XInput and DirectInput Backends

**Files:**
- Create: `src/platform/windows/input/windows_xinput_backend.hpp`
- Create: `src/platform/windows/input/windows_xinput_backend.cpp`
- Create: `src/platform/windows/input/windows_directinput_backend.hpp`
- Create: `src/platform/windows/input/windows_directinput_backend.cpp`
- Modify: `src/platform/windows/input/windows_gamepad_backend_mux.cpp`

- [ ] **Step 1: Implement XInput polling and mapping**

```cpp
for (DWORD userIndex = 0; userIndex < XUSER_MAX_COUNT; userIndex++) {
    XINPUT_STATE state {};
    if (::XInputGetState(userIndex, &state) != ERROR_SUCCESS) {
        continue;
    }

    InputGamepadState gamepad {};
    gamepad.set_Connected(true);
    gamepad.set_LeftStickX(state.Gamepad.sThumbLX);
    gamepad.set_LeftStickY(state.Gamepad.sThumbLY);
    gamepad.set_RightStickX(state.Gamepad.sThumbRX);
    gamepad.set_RightStickY(state.Gamepad.sThumbRY);
    gamepad.set_LeftTrigger(state.Gamepad.bLeftTrigger);
    gamepad.set_RightTrigger(state.Gamepad.bRightTrigger);
    gamepad.SetButtonDown(InputGamepadButton::South, (state.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0);
}
```

- [ ] **Step 2: Implement DirectInput enumeration and polling**

```cpp
BOOL CALLBACK WindowsDirectInputBackend::EnumerateGamepadsCallback(
    const DIDEVICEINSTANCEW* instance,
    VOID* context) {
    WindowsDirectInputBackend* backend = static_cast<WindowsDirectInputBackend*>(context);
    backend->AddDevice(*instance);
    return DIENUM_CONTINUE;
}
```

```cpp
DIJOYSTATE2 state {};
HRESULT result = device->Poll();
if (FAILED(result)) {
    device->Acquire();
    return;
}
device->GetDeviceState(sizeof(DIJOYSTATE2), &state);
```

- [ ] **Step 3: Register both backends in the mux only when their compile flags are enabled**

```cpp
#if defined(HELENGINE_WINDOWS_ENABLE_XINPUT) && HELENGINE_WINDOWS_ENABLE_XINPUT
    Backends.Add(new WindowsXInputBackend());
#endif
#if defined(HELENGINE_WINDOWS_ENABLE_DIRECTINPUT) && HELENGINE_WINDOWS_ENABLE_DIRECTINPUT
    Backends.Add(new WindowsDirectInputBackend(Window));
#endif
```

- [ ] **Step 4: Re-run the managed builder tests**

Run:

```powershell
rtk dotnet test builder.tests/helengine.windows.builder.tests.csproj -p:HelEngineRoot=C:\dev\helworks\helengine -v minimal
```

Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/platform/windows/input/windows_xinput_backend.* src/platform/windows/input/windows_directinput_backend.* src/platform/windows/input/windows_gamepad_backend_mux.cpp
git commit -m "feat: add windows xinput and directinput gamepad backends"
```

### Task 6: Implement SteamInput and End-to-End Runtime Wiring

**Files:**
- Create: `src/platform/windows/input/windows_steam_input_backend.hpp`
- Create: `src/platform/windows/input/windows_steam_input_backend.cpp`
- Modify: `src/platform/windows/input/windows_gamepad_backend_mux.cpp`
- Modify: `src/platform/windows/win32/win32_input_bridge.cpp`

- [ ] **Step 1: Implement SteamInput initialization guarded by build-time SDK support**

```cpp
bool WindowsSteamInputBackend::TryInitialize(std::string& failureMessage) {
    if (!SteamAPI_Init()) {
        failureMessage = "SteamAPI_Init failed.";
        return false;
    }
    if (!SteamInput()->Init(false)) {
        failureMessage = "SteamInput()->Init(false) failed.";
        return false;
    }
    Initialized = true;
    return true;
}
```

- [ ] **Step 2: Poll SteamInput and map handles to `InputGamepadState`**

```cpp
InputHandle_t handles[STEAM_INPUT_MAX_COUNT] {};
int handleCount = SteamInput()->GetConnectedControllers(handles);
for (int index = 0; index < handleCount; index++) {
    InputDigitalActionData_t south = SteamInput()->GetDigitalActionData(handles[index], SouthActionHandle);
    InputAnalogActionData_t leftStick = SteamInput()->GetAnalogActionData(handles[index], LeftStickActionHandle);
}
```

- [ ] **Step 3: Register the Steam backend behind the generated runtime manifest and compile flag**

```cpp
#if defined(HELENGINE_WINDOWS_ENABLE_STEAMINPUT) && HELENGINE_WINDOWS_ENABLE_STEAMINPUT
    if (he_runtime_embed_steaminput()) {
        Backends.Add(new WindowsSteamInputBackend());
    }
#endif
```

- [ ] **Step 4: Verify the final Windows input bridge publishes merged gamepads**

```cpp
List<InputGamepadState> mergedGamepads;
GamepadBackendMux.CaptureFrame(mergedGamepads);
frame.set_Gamepads(&mergedGamepads);
frame.set_GamepadCount(mergedGamepads.get_Count());
```

- [ ] **Step 5: Run the full managed verification suite**

Run:

```powershell
rtk dotnet test builder.tests/helengine.windows.builder.tests.csproj -p:HelEngineRoot=C:\dev\helworks\helengine -v minimal
```

Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add src/platform/windows/input/windows_steam_input_backend.* src/platform/windows/input/windows_gamepad_backend_mux.cpp src/platform/windows/win32/win32_input_bridge.cpp
git commit -m "feat: add steaminput backend to windows player"
```

### Task 7: Verify Build Subsets and Document Environment Prerequisites

**Files:**
- Modify: `README.md`
- Modify: `docs/superpowers/specs/2026-05-20-windows-multi-input-sources-design.md` (only if implementation changed the agreed behavior)

- [ ] **Step 1: Document the runtime/backend build prerequisites**

```md
## Windows Gamepad Backends

- `embed-xinput`: links XInput support into the player
- `embed-directinput`: links DirectInput 8 support into the player
- `embed-steaminput`: requires `HELENGINE_STEAMWORKS_SDK_ROOT` during native build
```

- [ ] **Step 2: Run the full managed verification one last time**

Run:

```powershell
rtk dotnet test builder.tests/helengine.windows.builder.tests.csproj -p:HelEngineRoot=C:\dev\helworks\helengine -v minimal
```

Expected: PASS

- [ ] **Step 3: Record the known environment caveat if Steamworks is unavailable**

```md
Current workspace verification note: SteamInput-native compilation requires a local Steamworks SDK install and `HELENGINE_STEAMWORKS_SDK_ROOT`.
```

- [ ] **Step 4: Commit**

```bash
git add README.md
git commit -m "docs: document windows gamepad backend prerequisites"
```
