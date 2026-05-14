# Shared Debug Runtime Diagnostics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a shared, debug-build-only runtime diagnostics abstraction in engine core, move Windows process-memory sampling behind that abstraction, and fix steady-state scene-id reporting so all-scenes diagnostics can identify unexplained RAM growth accurately.

**Architecture:** Introduce shared core diagnostics types and a provider contract in `helengine.core`, guard the feature behind debug/development compilation, and have Windows implement the first provider. Keep existing Windows asset/resource diagnostics, but source process/runtime snapshot data and loaded-scene ids from the shared service instead of the current host-side latch.

**Tech Stack:** C#, .NET 9 test projects, generated native core integration, C++ Win32/DirectX11 host, CMake, PowerShell, City editor build pipeline

---

### Task 1: Add Shared Debug Diagnostics Types In Core

**Files:**
- Create: `C:\dev\helworks\helengine\engine\helengine.core\diagnostics\RuntimeMemoryDiagnosticsSnapshot.cs`
- Create: `C:\dev\helworks\helengine\engine\helengine.core\diagnostics\RuntimeDiagnosticsMetric.cs`
- Create: `C:\dev\helworks\helengine\engine\helengine.core\diagnostics\IRuntimeDiagnosticsProvider.cs`
- Create: `C:\dev\helworks\helengine\engine\helengine.core\diagnostics\RuntimeDiagnosticsService.cs`
- Modify: `C:\dev\helworks\helengine\engine\helengine.core\CoreInitializationOptions.cs`
- Modify: `C:\dev\helworks\helengine\engine\helengine.core\scene\runtime\SceneManager.cs`
- Test: `C:\dev\helworks\helengine\engine\helengine.editor.tests\serialization\scene\SceneManagerTests.cs`
- Test: `C:\dev\helworks\helengine\engine\helengine.editor.tests\serialization\scene\RuntimeSceneLoadServiceTests.cs`

- [ ] **Step 1: Write the failing shared-core tests**

Add tests that prove:
- the diagnostics service can return a snapshot from a registered provider
- tracked scene ids come from the loaded-scene set instead of the last trace scene id
- no provider means a null/empty snapshot surface without throwing in non-diagnostics flows

```csharp
[Fact]
public void CaptureSnapshot_UsesRegisteredProviderAndLoadedSceneIds() {
    RuntimeMemoryDiagnosticsSnapshot snapshot = new RuntimeMemoryDiagnosticsSnapshot {
        ResidentBytes = 123,
        CommittedBytes = 456,
        AvailablePhysicalBytes = 789
    };

    FakeRuntimeDiagnosticsProvider provider = new FakeRuntimeDiagnosticsProvider(snapshot);
    CoreInitializationOptions options = new CoreInitializationOptions {
        RuntimeDiagnosticsProvider = provider
    };

    Core core = BuildInitializedCore(options);
    SceneManager sceneManager = core.SceneManager;
    sceneManager.LoadScene("DemoDiscMainMenu", SceneLoadMode.Single);

    RuntimeMemoryDiagnosticsSnapshot captured = core.RuntimeDiagnosticsService.CaptureSnapshot();

    Assert.Equal((ulong)123, captured.ResidentBytes);
    Assert.Contains("DemoDiscMainMenu", captured.TrackedSceneIds);
}

[Fact]
public void CaptureSnapshot_DoesNotUseLastTraceSceneIdWhenLoadedScenesDiffer() {
    FakeRuntimeDiagnosticsProvider provider = new FakeRuntimeDiagnosticsProvider(new RuntimeMemoryDiagnosticsSnapshot());
    CoreInitializationOptions options = new CoreInitializationOptions {
        RuntimeDiagnosticsProvider = provider
    };

    Core core = BuildInitializedCore(options);
    SceneManager sceneManager = core.SceneManager;
    sceneManager.LoadScene("Menu", SceneLoadMode.Single);
    sceneManager.SetLastTraceSceneIdForTests("TransientTraceOnly");

    RuntimeMemoryDiagnosticsSnapshot captured = core.RuntimeDiagnosticsService.CaptureSnapshot();

    Assert.DoesNotContain("TransientTraceOnly", captured.TrackedSceneIds);
    Assert.Contains("Menu", captured.TrackedSceneIds);
}
```

- [ ] **Step 2: Run the targeted tests to verify they fail**

Run:

```powershell
dotnet test C:\dev\helworks\helengine\engine\helengine.editor.tests\helengine.editor.tests.csproj --filter "FullyQualifiedName~SceneManagerTests|FullyQualifiedName~RuntimeSceneLoadServiceTests" -v minimal
```

Expected:
- FAIL because `RuntimeDiagnosticsService`, `IRuntimeDiagnosticsProvider`, and `RuntimeDiagnosticsProvider` plumbing do not exist yet.

- [ ] **Step 3: Write the minimal shared diagnostics implementation**

Add the new shared types and initialization seam.

`RuntimeMemoryDiagnosticsSnapshot.cs`

```csharp
namespace HelEngine.Diagnostics;

/// <summary>
/// Stores one portable runtime memory/process snapshot captured during a debug build.
/// </summary>
public class RuntimeMemoryDiagnosticsSnapshot {
    /// <summary>
    /// Gets or sets the current resident memory size in bytes.
    /// </summary>
    public ulong ResidentBytes { get; set; }

    /// <summary>
    /// Gets or sets the peak resident memory size in bytes.
    /// </summary>
    public ulong PeakResidentBytes { get; set; }

    /// <summary>
    /// Gets or sets the current committed/private memory size in bytes.
    /// </summary>
    public ulong CommittedBytes { get; set; }

    /// <summary>
    /// Gets or sets the peak committed/private memory size in bytes.
    /// </summary>
    public ulong PeakCommittedBytes { get; set; }

    /// <summary>
    /// Gets or sets the current available physical memory in bytes when known.
    /// </summary>
    public ulong AvailablePhysicalBytes { get; set; }

    /// <summary>
    /// Gets or sets the current page fault count when known.
    /// </summary>
    public ulong PageFaultCount { get; set; }

    /// <summary>
    /// Gets or sets the currently loaded scene ids represented by this snapshot.
    /// </summary>
    public IReadOnlyList<string> TrackedSceneIds { get; set; } = Array.Empty<string>();

    /// <summary>
    /// Gets or sets platform-specific detail metrics attached to this snapshot.
    /// </summary>
    public IReadOnlyList<RuntimeDiagnosticsMetric> DetailMetrics { get; set; } = Array.Empty<RuntimeDiagnosticsMetric>();
}
```

`IRuntimeDiagnosticsProvider.cs`

```csharp
namespace HelEngine.Diagnostics;

/// <summary>
/// Supplies debug-build runtime diagnostics snapshots for one platform backend.
/// </summary>
public interface IRuntimeDiagnosticsProvider {
    /// <summary>
    /// Captures the current platform memory/process diagnostics snapshot.
    /// </summary>
    RuntimeMemoryDiagnosticsSnapshot CaptureSnapshot();
}
```

`RuntimeDiagnosticsService.cs`

```csharp
namespace HelEngine.Diagnostics;

/// <summary>
/// Coordinates debug-build runtime diagnostics capture for the engine core.
/// </summary>
public class RuntimeDiagnosticsService {
    readonly IRuntimeDiagnosticsProvider Provider;
    readonly SceneManager SceneManager;

    /// <summary>
    /// Creates the service for one initialized engine core.
    /// </summary>
    public RuntimeDiagnosticsService(IRuntimeDiagnosticsProvider provider, SceneManager sceneManager) {
        Provider = provider;
        SceneManager = sceneManager;
    }

    /// <summary>
    /// Captures the current runtime diagnostics snapshot when a provider is available.
    /// </summary>
    public RuntimeMemoryDiagnosticsSnapshot CaptureSnapshot() {
        if (Provider == null) {
            return new RuntimeMemoryDiagnosticsSnapshot {
                TrackedSceneIds = BuildTrackedSceneIds()
            };
        }

        RuntimeMemoryDiagnosticsSnapshot snapshot = Provider.CaptureSnapshot() ?? new RuntimeMemoryDiagnosticsSnapshot();
        snapshot.TrackedSceneIds = BuildTrackedSceneIds();
        return snapshot;
    }

    IReadOnlyList<string> BuildTrackedSceneIds() {
        if (SceneManager == null) {
            return Array.Empty<string>();
        }

        return SceneManager.GetLoadedSceneIds();
    }
}
```

Update `CoreInitializationOptions.cs` to carry:

```csharp
/// <summary>
/// Gets or sets the optional debug-build runtime diagnostics provider supplied by the active platform.
/// </summary>
public IRuntimeDiagnosticsProvider RuntimeDiagnosticsProvider { get; set; }
```

Update `SceneManager.cs` to expose a loaded-scene-id read model:

```csharp
/// <summary>
/// Returns the current loaded scene ids in deterministic order for diagnostics.
/// </summary>
public IReadOnlyList<string> GetLoadedSceneIds() {
    return LoadedScenes
        .Select(record => record.SceneId)
        .Where(sceneId => !string.IsNullOrWhiteSpace(sceneId))
        .ToArray();
}
```

- [ ] **Step 4: Run the shared-core tests to verify they pass**

Run:

```powershell
dotnet test C:\dev\helworks\helengine\engine\helengine.editor.tests\helengine.editor.tests.csproj --filter "FullyQualifiedName~SceneManagerTests|FullyQualifiedName~RuntimeSceneLoadServiceTests" -v minimal
```

Expected:
- PASS for the new diagnostics service and tracked-scene-id coverage.

- [ ] **Step 5: Commit**

```bash
git -C C:\dev\helworks\helengine add engine/helengine.core/diagnostics engine/helengine.core/CoreInitializationOptions.cs engine/helengine.core/scene/runtime/SceneManager.cs engine/helengine.editor.tests/serialization/scene/SceneManagerTests.cs engine/helengine.editor.tests/serialization/scene/RuntimeSceneLoadServiceTests.cs
git -C C:\dev\helworks\helengine commit -m "feat: add shared debug runtime diagnostics core"
```

### Task 2: Wire The Shared Diagnostics Service Through Core Initialization

**Files:**
- Modify: `C:\dev\helworks\helengine\engine\helengine.core\Core.cs`
- Modify: `C:\dev\helworks\helengine\engine\helengine.core\CoreInitializationOptions.cs`
- Test: `C:\dev\helworks\helengine\engine\helengine.editor.tests\serialization\scene\SceneManagerTests.cs`

- [ ] **Step 1: Write the failing core-initialization tests**

Add tests that prove:
- debug builds create the service when a provider is supplied
- the service remains safe when no provider is supplied
- release-oriented initialization paths are not forced to fabricate a provider

```csharp
[Fact]
public void Initialize_CreatesRuntimeDiagnosticsService_WhenProviderIsSupplied() {
    CoreInitializationOptions options = new CoreInitializationOptions {
        RuntimeDiagnosticsProvider = new FakeRuntimeDiagnosticsProvider(new RuntimeMemoryDiagnosticsSnapshot())
    };

    Core core = new Core();
    core.Initialize(renderManager3D, renderManager2D, inputManager, platformInfo, options);

    Assert.NotNull(core.RuntimeDiagnosticsService);
}

[Fact]
public void Initialize_CreatesRuntimeDiagnosticsServiceWithoutProvider_WhenDiagnosticsAreUnused() {
    Core core = new Core();
    core.Initialize(renderManager3D, renderManager2D, inputManager, platformInfo, new CoreInitializationOptions());

    Assert.NotNull(core.RuntimeDiagnosticsService);
    Assert.Empty(core.RuntimeDiagnosticsService.CaptureSnapshot().TrackedSceneIds);
}
```

- [ ] **Step 2: Run the targeted initialization tests to verify they fail**

Run:

```powershell
dotnet test C:\dev\helworks\helengine\engine\helengine.editor.tests\helengine.editor.tests.csproj --filter "FullyQualifiedName~SceneManagerTests.Initialize" -v minimal
```

Expected:
- FAIL because `Core` does not yet expose or initialize `RuntimeDiagnosticsService`.

- [ ] **Step 3: Implement the core initialization plumbing**

In `Core.cs`, add a property and initialize it after `SceneManager` is available:

```csharp
/// <summary>
/// Gets the debug-build runtime diagnostics service used to capture portable runtime snapshots.
/// </summary>
public RuntimeDiagnosticsService RuntimeDiagnosticsService { get; private set; }
```

Inside `Initialize(...)`:

```csharp
SceneManager = new SceneManager(this, SceneLoadService, InitializationOptions.SceneCatalog);
RuntimeDiagnosticsService = new RuntimeDiagnosticsService(
    InitializationOptions.RuntimeDiagnosticsProvider,
    SceneManager);
```

Inside `Dispose()` keep the behavior simple:

```csharp
RuntimeDiagnosticsService = null;
```

Do not add runtime fallback object creation outside this explicit seam. The service exists because core owns the snapshot vocabulary even when the provider is absent.

- [ ] **Step 4: Run the initialization tests to verify they pass**

Run:

```powershell
dotnet test C:\dev\helworks\helengine\engine\helengine.editor.tests\helengine.editor.tests.csproj --filter "FullyQualifiedName~SceneManagerTests" -v minimal
```

Expected:
- PASS, including the new initialization assertions.

- [ ] **Step 5: Commit**

```bash
git -C C:\dev\helworks\helengine add engine/helengine.core/Core.cs engine/helengine.core/CoreInitializationOptions.cs engine/helengine.editor.tests/serialization/scene/SceneManagerTests.cs
git -C C:\dev\helworks\helengine commit -m "feat: initialize shared runtime diagnostics service"
```

### Task 3: Move Windows Runtime Memory Sampling Behind A Debug Diagnostics Provider

**Files:**
- Create: `C:\dev\helworks\helengine-windows\src\platform\windows\runtime\runtime_memory_diagnostics_provider.hpp`
- Create: `C:\dev\helworks\helengine-windows\src\platform\windows\runtime\runtime_memory_diagnostics_provider.cpp`
- Modify: `C:\dev\helworks\helengine-windows\src\platform\windows\runtime\runtime_memory_snapshot.hpp`
- Modify: `C:\dev\helworks\helengine-windows\src\platform\windows\runtime\runtime_memory_snapshot.cpp`
- Modify: `C:\dev\helworks\helengine-windows\src\platform\windows\runtime\runtime_render_diagnostics.hpp`
- Modify: `C:\dev\helworks\helengine-windows\src\platform\windows\runtime\runtime_render_diagnostics.cpp`
- Modify: `C:\dev\helworks\helengine-windows\src\platform\windows\win32\win32_application.hpp`
- Modify: `C:\dev\helworks\helengine-windows\src\platform\windows\win32\win32_application.cpp`
- Modify: `C:\dev\helworks\helengine-windows\CMakeLists.txt`

- [ ] **Step 1: Write the failing Windows diagnostics integration checks**

Because there is no existing native unit-test harness here, add a small compile-time-focused verification target in the plan:
- the provider header is included only for debug/development builds
- `Win32Application` consumes provider output instead of calling `RuntimeMemorySnapshot::Capture()` directly at scene checkpoints
- the diagnostics log path still compiles with the same scene/resource output format plus the shared snapshot fields

Expected failing seam before implementation:

```cpp
RuntimeMemorySnapshot memorySnapshot = RuntimeMemorySnapshot::Capture();
```

This direct call should disappear from `WriteSceneDiagnosticsCheckpoint(...)` and be replaced by provider-backed/shared snapshot capture.

- [ ] **Step 2: Run the Windows native build to verify the current code does not yet support the provider abstraction**

Run:

```powershell
dotnet C:\dev\helworks\helengine\helengine.ui\helengine.editor.app\bin\Debug\net9.0-windows\helengine.editor.app.dll --project C:\dev\helprojs\city\project.heproj --build windows --output C:\tmp\city-windows-shared-runtime-diagnostics
```

Expected:
- Current code still builds, but it proves the old host-only path is still active because `win32_application.cpp` calls `RuntimeMemorySnapshot::Capture()` directly.

- [ ] **Step 3: Implement the Windows provider and shared snapshot consumption**

Create a Windows provider wrapper that owns the platform sampling:

`runtime_memory_diagnostics_provider.hpp`

```cpp
namespace helengine::windows {
    /// Owns debug-build Windows runtime memory diagnostics capture for the shared engine diagnostics service.
    class RuntimeMemoryDiagnosticsProvider {
    public:
        /// Captures the current Windows runtime memory snapshot.
        RuntimeMemorySnapshot CaptureSnapshot() const;
    };
}
```

`runtime_memory_diagnostics_provider.cpp`

```cpp
namespace helengine::windows {
    RuntimeMemorySnapshot RuntimeMemoryDiagnosticsProvider::CaptureSnapshot() const {
        return RuntimeMemorySnapshot::Capture();
    }
}
```

In `win32_application.hpp`, replace the scene-id latch field usage with a provider field:

```cpp
/// Stores the debug-build Windows runtime diagnostics provider used to populate shared memory snapshots.
std::unique_ptr<RuntimeMemoryDiagnosticsProvider> MemoryDiagnosticsProvider;
```

In `InitializeEngineCore()` populate the shared core options in debug builds:

```cpp
MemoryDiagnosticsProvider = std::make_unique<RuntimeMemoryDiagnosticsProvider>();
options->RuntimeDiagnosticsProvider = BuildRuntimeDiagnosticsProviderBridge(*MemoryDiagnosticsProvider);
```

In `WriteSceneDiagnosticsCheckpoint(...)`, stop calling `RuntimeMemorySnapshot::Capture()` directly and instead consume the shared/service snapshot:

```cpp
RuntimeMemorySnapshot memorySnapshot = CaptureSharedRuntimeMemorySnapshot();
std::string trackedSceneIds = CaptureSharedTrackedSceneIds();
```

Keep the existing Windows-specific metric fields, but source them from the provider-fed snapshot.

- [ ] **Step 4: Run the City Windows build again to verify the provider-backed path compiles**

Run:

```powershell
dotnet C:\dev\helworks\helengine\helengine.ui\helengine.editor.app\bin\Debug\net9.0-windows\helengine.editor.app.dll --project C:\dev\helprojs\city\project.heproj --build windows --output C:\tmp\city-windows-shared-runtime-diagnostics
```

Expected:
- PASS, producing a diagnostics-enabled player with the shared-provider-backed snapshot path.

- [ ] **Step 5: Commit**

```bash
git -C C:\dev\helworks\helengine-windows add CMakeLists.txt src/platform/windows/runtime src/platform/windows/win32/win32_application.cpp src/platform/windows/win32/win32_application.hpp
git -C C:\dev\helworks\helengine-windows commit -m "feat: route windows diagnostics through shared runtime provider"
```

### Task 4: Fix Steady-State Scene Id Reporting From The Loaded Scene Set

**Files:**
- Modify: `C:\dev\helworks\helengine-windows\src\platform\windows\win32\win32_application.hpp`
- Modify: `C:\dev\helworks\helengine-windows\src\platform\windows\win32\win32_application.cpp`
- Modify: `C:\dev\helworks\helengine-windows\src\platform\windows\runtime\runtime_render_diagnostics.cpp`
- Test: manual verification with `C:\tmp\city-windows-shared-runtime-diagnostics\helengine_windows.exe`

- [ ] **Step 1: Write the failing behavior note against the current log**

Use the current log as the failing case:

```text
scene.checkpoint label="scene_steady_state" ... tracked_scene_ids="DemoDiscMainMenu" ... texture_resources=6 scene_owned_texture_resources=5 ...
```

That line is wrong because the resource footprint is clearly a non-menu scene. The fix must replace the host latch with the loaded-scene set from the shared diagnostics service.

- [ ] **Step 2: Reproduce the incorrect scene-id behavior before the fix**

Run:

```powershell
C:\tmp\city-windows-shared-runtime-diagnostics\helengine_windows.exe
```

Manual path:
1. Open all scenes.
2. Close the player.
3. Inspect `C:\tmp\city-windows-shared-runtime-diagnostics\helengine_windows.diagnostics.log`.

Expected before the fix:
- Later `scene_steady_state` lines still say `tracked_scene_ids="DemoDiscMainMenu"` after leaving the menu.

- [ ] **Step 3: Replace the host-side latch with shared loaded-scene-id capture**

In `win32_application.cpp`, remove the logic that persists `TrackedLoadedSceneIds` from `LoadSceneImmediateEnd` only:

```cpp
void Win32Application::UpdateTrackedLoadedSceneIds(
    const std::string& sceneManagerStage,
    const std::string& sceneManagerSceneId,
    int loadedSceneCount) {
    // Delete this latch-based behavior.
}
```

Replace it with shared snapshot-driven tracking inside `WriteSceneDiagnosticsCheckpoint(...)`:

```cpp
std::string trackedSceneIds = EngineCore != nullptr && EngineCore->get_RuntimeDiagnosticsService() != nullptr
    ? JoinSceneIds(EngineCore->get_RuntimeDiagnosticsService()->CaptureSnapshot()->get_TrackedSceneIds())
    : std::string();
```

Keep transient trace fields such as `scene_manager_stage` and `scene_manager_scene_id` for debugging, but do not use them as the steady-state authority.

- [ ] **Step 4: Rerun the all-scenes pass and verify the corrected scene ids**

Run:

```powershell
dotnet C:\dev\helworks\helengine\helengine.ui\helengine.editor.app\bin\Debug\net9.0-windows\helengine.editor.app.dll --project C:\dev\helprojs\city\project.heproj --build windows --output C:\tmp\city-windows-shared-runtime-diagnostics
C:\tmp\city-windows-shared-runtime-diagnostics\helengine_windows.exe
```

Manual path:
1. Open all scenes.
2. Close the player.

Expected:
- steady-state log lines now name the actual loaded scenes instead of repeating `DemoDiscMainMenu`
- scene/resource counters still return to baseline for menu scenes

- [ ] **Step 5: Commit**

```bash
git -C C:\dev\helworks\helengine-windows add src/platform/windows/win32/win32_application.cpp src/platform/windows/win32/win32_application.hpp src/platform/windows/runtime/runtime_render_diagnostics.cpp
git -C C:\dev\helworks\helengine-windows commit -m "fix: report steady-state scene ids from loaded scenes"
```

### Task 5: Verify Debug-Build-Only Behavior And End-To-End Diagnostics

**Files:**
- Modify: `C:\dev\helworks\helengine-windows\src\platform\windows\win32\win32_application.cpp`
- Modify: `C:\dev\helworks\helengine-windows\CMakeLists.txt`
- Test: `C:\tmp\city-windows-shared-runtime-diagnostics\helengine_windows.diagnostics.log`

- [ ] **Step 1: Add the failing verification checklist**

The final implementation must prove:
- diagnostics provider creation is gated to debug/development builds
- release/shipping builds do not compile this path in
- all-scenes debug logs now contain:
  - correct `tracked_scene_ids`
  - shared memory snapshot fields
  - existing renderer/resource counters

Add a concise checklist to the code review notes or temporary verification scratchpad before editing.

- [ ] **Step 2: Build the debug player and verify diagnostics output exists**

Run:

```powershell
dotnet C:\dev\helworks\helengine\helengine.ui\helengine.editor.app\bin\Debug\net9.0-windows\helengine.editor.app.dll --project C:\dev\helprojs\city\project.heproj --build windows --output C:\tmp\city-windows-shared-runtime-diagnostics
```

Expected:
- PASS
- output contains `helengine_windows.exe`
- output contains `helengine_windows.diagnostics.log` after a manual run

- [ ] **Step 3: Inspect the final all-scenes diagnostics log and record the expected shape**

Run after the manual all-scenes pass:

```powershell
Select-String -Path C:\tmp\city-windows-shared-runtime-diagnostics\helengine_windows.diagnostics.log -Pattern 'label="scene_steady_state"|tracked_scene_ids=|texture_resources=|private_usage_bytes='
```

Expected:
- menu steady state shows `tracked_scene_ids` for the menu scene
- racer or other heavy scenes show different `tracked_scene_ids`
- renderer resource counters still return to menu baseline after transitions
- any remaining RAM growth is now attributable without the scene-id ambiguity

- [ ] **Step 4: Build or inspect the non-debug configuration to verify exclusion**

Run:

```powershell
cmake -S C:\dev\helworks\helengine-windows -B C:\tmp\helengine-windows-release-check -DCMAKE_BUILD_TYPE=Release
cmake --build C:\tmp\helengine-windows-release-check --config Release
```

Expected:
- PASS
- no debug-only runtime diagnostics provider symbols are required in Release
- no compile errors from stripped diagnostics codepaths

- [ ] **Step 5: Commit**

```bash
git -C C:\dev\helworks\helengine-windows add CMakeLists.txt src/platform/windows/win32/win32_application.cpp
git -C C:\dev\helworks\helengine-windows commit -m "test: verify shared debug runtime diagnostics end to end"
```

## Self-Review

### Spec Coverage

- Shared engine abstraction: covered by Task 1 and Task 2.
- Windows first provider: covered by Task 3.
- Debug-build-only behavior: covered by Task 5.
- Correct steady-state scene ids: covered by Task 4.
- Keep renderer/resource diagnostics while adding deeper process visibility: covered by Task 3 and Task 5.

### Placeholder Scan

- Removed vague “add tests” language by naming exact test files and commands.
- Removed vague “handle logging” language by naming the exact Windows logging files and seams.
- Each task includes explicit commands and concrete code fragments.

### Type Consistency

- Shared snapshot type stays `RuntimeMemoryDiagnosticsSnapshot` throughout.
- Provider seam stays `IRuntimeDiagnosticsProvider` throughout.
- Core service stays `RuntimeDiagnosticsService` throughout.
- Windows continues to log `tracked_scene_ids` as a string field derived from the shared snapshot scene-id list.
