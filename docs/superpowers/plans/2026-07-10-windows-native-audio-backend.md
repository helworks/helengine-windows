# Windows Native Audio Backend Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a native Windows `IAudioBackend` to `helengine-windows` so cooked audio assets play in the native City Windows build, starting with main-menu music.

**Architecture:** The generated runtime already emits `AudioAsset`, `AudioPlaybackRequest`, and `IAudioBackend`, and the City menu scene already requests playback through `AudioSourceComponent`. The missing piece is native host wiring, so the Windows player will get a small WinMM-backed `Win32AudioBackend` that can play PCM bytes through `waveOut`, expose the generated `IAudioBackend` surface, and be registered from `Win32Application::InitializeEngineCore()`.

**Tech Stack:** C++20, WinMM (`waveOut*`), generated engine interfaces, xUnit source-level builder tests, `build-platform.ps1`

---

## File Structure

- Create: `builder.tests/Win32ApplicationAudioBackendSourceTests.cs`
  Verifies the native Windows host owns, initializes, registers, and destroys the audio backend.
- Create: `src/platform/windows/win32/win32_audio_backend.hpp`
  Declares the concrete `Win32AudioBackend` class, voice state, helper methods, and generated `IAudioBackend` overrides.
- Create: `src/platform/windows/win32/win32_audio_backend.cpp`
  Implements WinMM playback, bus gain/pause control, voice lifetime management, and backend update logic.
- Modify: `src/platform/windows/win32/win32_application.hpp`
  Adds the audio backend forward declaration and owning field.
- Modify: `src/platform/windows/win32/win32_application.cpp`
  Includes the audio backend header, creates the backend during engine startup, registers it with `EngineCore`, and deletes it during shutdown.
- Modify: `CMakeLists.txt`
  Compiles the new backend source and links `winmm`.

### Task 1: Cover Windows Host Audio Wiring

**Files:**
- Create: `builder.tests/Win32ApplicationAudioBackendSourceTests.cs`
- Test: `builder.tests/helengine.windows.builder.tests.csproj`

- [ ] **Step 1: Write the failing test**

```csharp
using System;
using System.IO;
using Xunit;

namespace Helengine.Windows.Builder.Tests;

public sealed class Win32ApplicationAudioBackendSourceTests
{
    [Fact]
    public void Win32Application_owns_and_registers_audio_backend()
    {
        var repositoryRootPath = ResolveWindowsRepositoryRootPath();
        var headerPath = Path.Combine(repositoryRootPath, "src", "platform", "windows", "win32", "win32_application.hpp");
        var sourcePath = Path.Combine(repositoryRootPath, "src", "platform", "windows", "win32", "win32_application.cpp");

        var headerContents = File.ReadAllText(headerPath);
        var sourceContents = File.ReadAllText(sourcePath);

        Assert.Contains("class Win32AudioBackend;", headerContents, StringComparison.Ordinal);
        Assert.Contains("Win32AudioBackend* EngineAudioBackend;", headerContents, StringComparison.Ordinal);
        Assert.Contains("#include \"platform/windows/win32/win32_audio_backend.hpp\"", sourceContents, StringComparison.Ordinal);
        Assert.Contains("EngineAudioBackend = new Win32AudioBackend();", sourceContents, StringComparison.Ordinal);
        Assert.Contains("EngineCore->SetAudioBackend(EngineAudioBackend);", sourceContents, StringComparison.Ordinal);
        Assert.Contains("delete EngineAudioBackend;", sourceContents, StringComparison.Ordinal);
    }

    private static string ResolveWindowsRepositoryRootPath()
    {
        var currentDirectoryPath = AppContext.BaseDirectory;
        var candidatePath = Path.GetFullPath(Path.Combine(currentDirectoryPath, "..", "..", "..", ".."));

        if (Directory.Exists(Path.Combine(candidatePath, "src", "platform", "windows", "win32")))
        {
            return candidatePath;
        }

        throw new DirectoryNotFoundException("Unable to resolve helengine-windows repository root from builder.tests output.");
    }
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `rtk dotnet test C:\dev\helworks\helengine-windows\builder.tests\helengine.windows.builder.tests.csproj --filter FullyQualifiedName~Win32ApplicationAudioBackendSourceTests -v minimal`

Expected: `FAIL` because `win32_application.hpp` and `win32_application.cpp` do not yet mention `Win32AudioBackend`.

- [ ] **Step 3: Commit the failing-test change**

```bash
rtk git -C C:\dev\helworks\helengine-windows add builder.tests/Win32ApplicationAudioBackendSourceTests.cs
rtk git -C C:\dev\helworks\helengine-windows commit -m "test: cover Windows native audio backend wiring"
```

### Task 2: Add the Native WinMM Audio Backend

**Files:**
- Create: `src/platform/windows/win32/win32_audio_backend.hpp`
- Create: `src/platform/windows/win32/win32_audio_backend.cpp`
- Modify: `CMakeLists.txt`
- Test: `builder.tests/Win32ApplicationAudioBackendSourceTests.cs`

- [ ] **Step 1: Declare the backend API**

```cpp
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <windows.h>
#include <mmsystem.h>

#include "generated/core/interfaces/IAudioBackend.hpp"

class AudioAsset;
class AudioPlaybackRequest;

class Win32AudioBackend final : public IAudioBackend
{
public:
    Win32AudioBackend();
    ~Win32AudioBackend() override;

    bool IsPlaying(int32_t voiceId) override;
    int32_t Play(::AudioAsset* asset, ::AudioPlaybackRequest* request) override;
    void SetBusGain(std::string busId, float gain) override;
    void SetBusPaused(std::string busId, bool paused) override;
    void Stop(int32_t voiceId) override;
    void Update() override;

private:
    struct VoiceState
    {
        HWAVEOUT DeviceHandle;
        WAVEHDR Header;
        std::vector<std::uint8_t> EncodedBytes;
        std::string BusId;
        float BaseGain;
        bool Loop;
        bool Paused;
        bool Completed;
    };

    static void CALLBACK HandleWaveOutCallback(HWAVEOUT waveOutHandle, UINT message, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR param2);

    static std::string NormalizeBusId(const std::string& busId);
    static float ClampGain(float gain);
    static WAVEFORMATEX BuildWaveFormat(const AudioAsset& asset);

    void ApplyVoiceState(int32_t voiceId, VoiceState& voice);
    void ApplyVoiceVolume(VoiceState& voice);
    void ApplyVoicePlaybackState(VoiceState& voice);
    void DisposeVoice(VoiceState& voice);

    std::unordered_map<int32_t, VoiceState> VoicesById;
    std::unordered_map<std::string, float> BusGainsById;
    std::unordered_map<std::string, bool> BusPausedById;
    int32_t NextVoiceId;
};
```

- [ ] **Step 2: Implement minimal PCM playback and bus state**

```cpp
int32_t Win32AudioBackend::Play(::AudioAsset* asset, ::AudioPlaybackRequest* request)
{
    if (asset == nullptr || asset->GetEncodedBytes() == nullptr)
    {
        return -1;
    }

    if (asset->GetSampleRate() <= 0 || asset->GetChannels() <= 0)
    {
        return -1;
    }

    auto* encodedBytes = asset->GetEncodedBytes();
    if (encodedBytes->Count() == 0)
    {
        return -1;
    }

    auto voiceId = NextVoiceId++;
    auto& voice = VoicesById[voiceId];
    voice.DeviceHandle = nullptr;
    ZeroMemory(&voice.Header, sizeof(WAVEHDR));
    voice.EncodedBytes.assign(encodedBytes->begin(), encodedBytes->end());
    voice.BusId = NormalizeBusId(request != nullptr ? request->GetBusId() : asset->GetDefaultBusId());
    voice.BaseGain = ClampGain(request != nullptr ? request->GetGain() : 1.0f);
    voice.Loop = request != nullptr ? request->GetLoop() : asset->GetDefaultLoop();
    voice.Paused = false;
    voice.Completed = false;

    auto format = BuildWaveFormat(*asset);
    auto result = waveOutOpen(&voice.DeviceHandle, WAVE_MAPPER, &format, reinterpret_cast<DWORD_PTR>(&HandleWaveOutCallback), 0, CALLBACK_FUNCTION);
    if (result != MMSYSERR_NOERROR)
    {
        VoicesById.erase(voiceId);
        return -1;
    }

    voice.Header.lpData = reinterpret_cast<LPSTR>(voice.EncodedBytes.data());
    voice.Header.dwBufferLength = static_cast<DWORD>(voice.EncodedBytes.size());
    voice.Header.dwUser = static_cast<DWORD_PTR>(voiceId);

    result = waveOutPrepareHeader(voice.DeviceHandle, &voice.Header, sizeof(WAVEHDR));
    if (result != MMSYSERR_NOERROR)
    {
        DisposeVoice(voice);
        VoicesById.erase(voiceId);
        return -1;
    }

    ApplyVoiceState(voiceId, voice);
    result = waveOutWrite(voice.DeviceHandle, &voice.Header, sizeof(WAVEHDR));
    if (result != MMSYSERR_NOERROR)
    {
        DisposeVoice(voice);
        VoicesById.erase(voiceId);
        return -1;
    }

    return voiceId;
}
```

- [ ] **Step 3: Add the backend source to the native build**

```cmake
set(HELENGINE_WINDOWS_GENERATED_CORE_SOURCES
    src/platform/windows/win32/win32_input_bridge.cpp
    src/platform/windows/win32/win32_render_bridge.cpp
    src/platform/windows/win32/win32_audio_backend.cpp
)

target_link_libraries(helengine_windows PRIVATE
    d3d11
    d3dcompiler
    dbghelp
    dxgi
    gdi32
    psapi
    user32
    winmm
)
```

- [ ] **Step 4: Run the source-level test again**

Run: `rtk dotnet test C:\dev\helworks\helengine-windows\builder.tests\helengine.windows.builder.tests.csproj --filter FullyQualifiedName~Win32ApplicationAudioBackendSourceTests -v minimal`

Expected: still `FAIL`, but only because the host has not yet been wired to own and register `Win32AudioBackend`.

- [ ] **Step 5: Commit the backend implementation**

```bash
rtk git -C C:\dev\helworks\helengine-windows add src/platform/windows/win32/win32_audio_backend.hpp src/platform/windows/win32/win32_audio_backend.cpp CMakeLists.txt
rtk git -C C:\dev\helworks\helengine-windows commit -m "feat: add native Windows audio backend"
```

### Task 3: Wire the Backend into the Native Windows Host

**Files:**
- Modify: `src/platform/windows/win32/win32_application.hpp`
- Modify: `src/platform/windows/win32/win32_application.cpp`
- Test: `builder.tests/Win32ApplicationAudioBackendSourceTests.cs`
- Test: `builder.tests/helengine.windows.builder.tests.csproj`

- [ ] **Step 1: Add host ownership in the header**

```cpp
class Win32AudioBackend;
class Win32InputBackend;
class Win32RenderManager2D;
class Win32RenderManager3D;

class Win32Application final
{
    // ...
private:
    EngineCore* EngineCoreInstance;
    Win32AudioBackend* EngineAudioBackend;
    Win32InputBackend* EngineInputBackend;
    Win32RenderManager2D* EngineRenderManager2D;
    Win32RenderManager3D* EngineRenderManager3D;
};
```

- [ ] **Step 2: Create, register, and destroy the backend**

```cpp
#include "platform/windows/win32/win32_audio_backend.hpp"

Win32Application::Win32Application()
    : EngineCoreInstance(nullptr),
      EngineAudioBackend(nullptr),
      EngineInputBackend(nullptr),
      EngineRenderManager2D(nullptr),
      EngineRenderManager3D(nullptr)
{
}

Win32Application::~Win32Application()
{
    delete EngineCoreInstance;
    delete EngineAudioBackend;
    delete EngineInputBackend;
    delete EngineRenderManager2D;
    delete EngineRenderManager3D;
}

void Win32Application::InitializeEngineCore()
{
    EngineCoreInstance = new EngineCore();
    EngineAudioBackend = new Win32AudioBackend();
    EngineInputBackend = new Win32InputBackend();
    EngineRenderManager2D = new Win32RenderManager2D();
    EngineRenderManager3D = new Win32RenderManager3D();

    EngineCoreInstance->Initialize(EngineRenderManager2D, EngineRenderManager3D, EngineInputBackend);
    EngineCoreInstance->SetAudioBackend(EngineAudioBackend);
}
```

- [ ] **Step 3: Run the focused source-level test**

Run: `rtk dotnet test C:\dev\helworks\helengine-windows\builder.tests\helengine.windows.builder.tests.csproj --filter FullyQualifiedName~Win32ApplicationAudioBackendSourceTests -v minimal`

Expected: `PASS`

- [ ] **Step 4: Run the full Windows builder test suite**

Run: `rtk dotnet test C:\dev\helworks\helengine-windows\builder.tests\helengine.windows.builder.tests.csproj -v minimal`

Expected: full suite `PASS`

- [ ] **Step 5: Build and launch the City Windows player**

Run: `rtk powershell -NoProfile -File C:\dev\helworks\helengine\artifacts\build-platform.ps1 -Project C:\dev\helprojs\city\project.heproj -Platform windows -Output C:\dev\helprojs\city\windows-build -Configuration Debug`

Expected build output:
- `Build completed for platform 'windows'`
- `cooked artifacts completed`
- output rooted at `C:\dev\helprojs\city\windows-build`

Run: `rtk powershell -NoProfile -Command "Start-Process -FilePath 'C:\dev\helprojs\city\windows-build\helengine_windows.exe'"`

Expected runtime result:
- main menu loads
- input continues to work
- menu music plays audibly on startup

- [ ] **Step 6: Commit the host wiring**

```bash
rtk git -C C:\dev\helworks\helengine-windows add src/platform/windows/win32/win32_application.hpp src/platform/windows/win32/win32_application.cpp
rtk git -C C:\dev\helworks\helengine-windows commit -m "feat: wire Windows runtime audio backend"
```

## Self-Review

- Spec coverage: native Windows audio backend, host wiring, build integration, builder regression coverage, and end-to-end City build verification are all covered by Tasks 1-3.
- Placeholder scan: no `TODO`, `TBD`, or implicit “write tests later” steps remain.
- Type consistency: all tasks use `IAudioBackend`, `AudioAsset`, `AudioPlaybackRequest`, `EngineCore::SetAudioBackend`, and `Win32AudioBackend` consistently.
