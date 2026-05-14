# Windows Scene Memory Diagnostics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Windows-only diagnostics that log scene-transition RAM snapshots, per-asset-class cache counts, and debug-level asset load/build events for the City `menu -> cube_test -> menu` flow without changing runtime cleanup behavior yet.

**Architecture:** Add small native diagnostics helpers for process-memory snapshots and structured log formatting, then expose readonly renderer cache/build counters from the Windows render bridge and have `Win32Application` emit named transition checkpoints through the existing lifecycle log. Keep the first pass diagnostics-only: no asset eviction, no disposal fixes, no scene lifetime behavior changes.

**Tech Stack:** C++, C++20, Win32, PSAPI process-memory APIs, DirectX11 bridge, CMake

---

## File Structure

### Existing Files To Modify

- `CMakeLists.txt`
  Must compile any new diagnostics helper sources added for the Windows host.
- `src/platform/windows/win32/win32_application.hpp`
  Needs diagnostics helper declarations, scene-checkpoint tracking state, and readonly access to transition logging helpers.
- `src/platform/windows/win32/win32_application.cpp`
  Must gather process-memory snapshots, emit structured scene checkpoints, and trace packaged asset loads.
- `src/platform/windows/win32/win32_render_bridge.hpp`
  Needs readonly diagnostics snapshot types or accessors for texture, model, and material cache/build accounting.
- `src/platform/windows/win32/win32_render_bridge.cpp`
  Must track cache sizes, duplicate builds by asset id, and emit debug-level asset build records without changing release behavior.

### New Files To Create

- `src/platform/windows/runtime/runtime_memory_snapshot.hpp`
  Defines the process-memory snapshot contract used by the application diagnostics.
- `src/platform/windows/runtime/runtime_memory_snapshot.cpp`
  Implements Win32 process-memory sampling and structured snapshot formatting.
- `src/platform/windows/runtime/runtime_render_diagnostics.hpp`
  Declares readonly diagnostics snapshot types for texture, model, material, and aggregate renderer counters.
- `src/platform/windows/runtime/runtime_render_diagnostics.cpp`
  Implements shared formatting helpers for renderer diagnostics log lines.
- `docs/superpowers/plans/2026-05-14-windows-scene-memory-diagnostics.md`
  This implementation plan.

### Notes

- There is no established native C++ unit-test harness in this repo today, so verification will be a mix of build safety, focused helper-level tests where feasible, and manual runtime validation.
- Keep new diagnostics types narrow and readonly from the point of view of `Win32Application`.
- Do not implement `ReleaseTexture`, renderer cache clearing, or `Core->Dispose()` changes in this plan. Those belong to the follow-up fix pass after the diagnostics baseline is captured.

---

### Task 1: Add Process-Memory Snapshot Helpers

**Files:**
- Create: `src/platform/windows/runtime/runtime_memory_snapshot.hpp`
- Create: `src/platform/windows/runtime/runtime_memory_snapshot.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the failing build integration step**

Plan the first compile break by adding the new helper files to the build before they exist:

```cmake
set(HELENGINE_WINDOWS_SOURCES
    src/main.cpp
    src/platform/windows/runtime/runtime_memory_snapshot.cpp
    src/platform/windows/runtime/runtime_player_profile.cpp
    src/platform/windows/runtime/runtime_player_profile_loader.cpp
    src/platform/windows/win32/win32_application.cpp
    src/platform/windows/win32/win32_window.cpp
    src/platform/windows/directx11/directx11_bootstrap.cpp
    src/platform/windows/directx11/directx11_presenter.cpp
)
```

- [ ] **Step 2: Run configure/build to verify it fails**

Run:

```powershell
cmake -S . -B build
cmake --build build
```

Expected: FAIL because `runtime_memory_snapshot.cpp` does not exist yet.

- [ ] **Step 3: Add the runtime memory snapshot contract**

Create `src/platform/windows/runtime/runtime_memory_snapshot.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <string>

namespace helengine::windows {
    /// Stores one sampled process-memory snapshot for scene diagnostics.
    struct RuntimeMemorySnapshot {
        /// Stores the working-set size in bytes.
        std::uint64_t WorkingSetBytes;

        /// Stores the private-usage size in bytes.
        std::uint64_t PrivateUsageBytes;

        /// Stores the pagefile-usage size in bytes.
        std::uint64_t PagefileUsageBytes;
    };

    /// Samples process-memory usage for the current Windows player process.
    RuntimeMemorySnapshot CaptureRuntimeMemorySnapshot();

    /// Formats one process-memory snapshot into structured key/value text.
    std::string FormatRuntimeMemorySnapshot(const RuntimeMemorySnapshot& snapshot);
}
```

- [ ] **Step 4: Add the memory snapshot implementation**

Create `src/platform/windows/runtime/runtime_memory_snapshot.cpp`:

```cpp
#include "platform/windows/runtime/runtime_memory_snapshot.hpp"

#include <Windows.h>
#include <psapi.h>

#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace helengine::windows {
    /// Converts one byte count into a fixed MB string for diagnostics.
    static std::string FormatMegabytes(std::uint64_t bytes) {
        std::ostringstream messageBuilder;
        messageBuilder << std::fixed << std::setprecision(2)
            << (static_cast<double>(bytes) / 1024.0 / 1024.0);
        return messageBuilder.str();
    }

    RuntimeMemorySnapshot CaptureRuntimeMemorySnapshot() {
        PROCESS_MEMORY_COUNTERS_EX counters {};
        counters.cb = sizeof(PROCESS_MEMORY_COUNTERS_EX);
        if (!GetProcessMemoryInfo(
                GetCurrentProcess(),
                reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                sizeof(PROCESS_MEMORY_COUNTERS_EX))) {
            throw std::runtime_error("GetProcessMemoryInfo failed for the HelEngine Windows player.");
        }

        RuntimeMemorySnapshot snapshot {};
        snapshot.WorkingSetBytes = static_cast<std::uint64_t>(counters.WorkingSetSize);
        snapshot.PrivateUsageBytes = static_cast<std::uint64_t>(counters.PrivateUsage);
        snapshot.PagefileUsageBytes = static_cast<std::uint64_t>(counters.PagefileUsage);
        return snapshot;
    }

    std::string FormatRuntimeMemorySnapshot(const RuntimeMemorySnapshot& snapshot) {
        std::ostringstream messageBuilder;
        messageBuilder
            << "working_set_bytes=" << snapshot.WorkingSetBytes
            << " working_set_mb=" << FormatMegabytes(snapshot.WorkingSetBytes)
            << " private_bytes=" << snapshot.PrivateUsageBytes
            << " private_mb=" << FormatMegabytes(snapshot.PrivateUsageBytes)
            << " pagefile_bytes=" << snapshot.PagefileUsageBytes
            << " pagefile_mb=" << FormatMegabytes(snapshot.PagefileUsageBytes);
        return messageBuilder.str();
    }
}
```

- [ ] **Step 5: Run configure/build to verify it passes**

Run:

```powershell
cmake -S . -B build
cmake --build build
```

Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt src/platform/windows/runtime/runtime_memory_snapshot.hpp src/platform/windows/runtime/runtime_memory_snapshot.cpp
git commit -m "feat: add Windows runtime memory snapshot diagnostics"
```

---

### Task 2: Add Readonly Renderer Diagnostics Contracts And Counters

**Files:**
- Create: `src/platform/windows/runtime/runtime_render_diagnostics.hpp`
- Create: `src/platform/windows/runtime/runtime_render_diagnostics.cpp`
- Modify: `src/platform/windows/win32/win32_render_bridge.hpp`
- Modify: `src/platform/windows/win32/win32_render_bridge.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the failing build integration step**

Add the new renderer diagnostics helper source to `CMakeLists.txt` before creating it:

```cmake
set(HELENGINE_WINDOWS_SOURCES
    src/main.cpp
    src/platform/windows/runtime/runtime_memory_snapshot.cpp
    src/platform/windows/runtime/runtime_render_diagnostics.cpp
    src/platform/windows/runtime/runtime_player_profile.cpp
    src/platform/windows/runtime/runtime_player_profile_loader.cpp
    src/platform/windows/win32/win32_application.cpp
    src/platform/windows/win32/win32_window.cpp
    src/platform/windows/directx11/directx11_bootstrap.cpp
    src/platform/windows/directx11/directx11_presenter.cpp
)
```

- [ ] **Step 2: Run configure/build to verify it fails**

Run:

```powershell
cmake -S . -B build
cmake --build build
```

Expected: FAIL because `runtime_render_diagnostics.cpp` does not exist yet.

- [ ] **Step 3: Add readonly diagnostics snapshot types**

Create `src/platform/windows/runtime/runtime_render_diagnostics.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <string>

namespace helengine::windows {
    /// Stores one aggregate diagnostics snapshot for Windows renderer-owned runtime resources.
    struct RuntimeRenderDiagnosticsSnapshot {
        /// Stores the count of live uploaded texture resources.
        std::uint64_t TextureCacheCount;

        /// Stores the cumulative texture build count.
        std::uint64_t TextureBuildCount;

        /// Stores the cumulative duplicate texture build count.
        std::uint64_t TextureDuplicateBuildCount;

        /// Stores the count of live material shader resources.
        std::uint64_t MaterialShaderCacheCount;

        /// Stores the cumulative material shader build count.
        std::uint64_t MaterialShaderBuildCount;

        /// Stores the cumulative duplicate material shader build count.
        std::uint64_t MaterialShaderDuplicateBuildCount;

        /// Stores the cumulative runtime model build count.
        std::uint64_t RuntimeModelBuildCount;

        /// Stores the cumulative duplicate runtime model build count.
        std::uint64_t RuntimeModelDuplicateBuildCount;
    };

    /// Formats one renderer diagnostics snapshot into structured key/value text.
    std::string FormatRuntimeRenderDiagnosticsSnapshot(const RuntimeRenderDiagnosticsSnapshot& snapshot);
}
```

- [ ] **Step 4: Add renderer snapshot formatting**

Create `src/platform/windows/runtime/runtime_render_diagnostics.cpp`:

```cpp
#include "platform/windows/runtime/runtime_render_diagnostics.hpp"

#include <sstream>

namespace helengine::windows {
    std::string FormatRuntimeRenderDiagnosticsSnapshot(const RuntimeRenderDiagnosticsSnapshot& snapshot) {
        std::ostringstream messageBuilder;
        messageBuilder
            << "texture_cache_count=" << snapshot.TextureCacheCount
            << " texture_build_count=" << snapshot.TextureBuildCount
            << " texture_duplicate_build_count=" << snapshot.TextureDuplicateBuildCount
            << " material_shader_cache_count=" << snapshot.MaterialShaderCacheCount
            << " material_shader_build_count=" << snapshot.MaterialShaderBuildCount
            << " material_shader_duplicate_build_count=" << snapshot.MaterialShaderDuplicateBuildCount
            << " runtime_model_build_count=" << snapshot.RuntimeModelBuildCount
            << " runtime_model_duplicate_build_count=" << snapshot.RuntimeModelDuplicateBuildCount;
        return messageBuilder.str();
    }
}
```

- [ ] **Step 5: Expose readonly diagnostics accessors from the render bridges**

Modify `src/platform/windows/win32/win32_render_bridge.hpp` to add one accessor per renderer:

```cpp
#include "platform/windows/runtime/runtime_render_diagnostics.hpp"
```

Add public methods:

```cpp
        /// Gets one readonly diagnostics snapshot for 3D renderer-owned caches and build counters.
        RuntimeRenderDiagnosticsSnapshot GetDiagnosticsSnapshot() const;
```

and

```cpp
        /// Gets one readonly diagnostics snapshot for 2D renderer-owned caches and build counters.
        RuntimeRenderDiagnosticsSnapshot GetDiagnosticsSnapshot() const;
```

Add private counters:

```cpp
        /// Counts cumulative runtime model builds performed by the 3D bridge.
        std::uint64_t RuntimeModelBuildCount = 0;

        /// Counts cumulative duplicate runtime model builds detected by model id.
        std::uint64_t RuntimeModelDuplicateBuildCount = 0;

        /// Counts cumulative material shader resource builds performed by the 3D bridge.
        std::uint64_t MaterialShaderBuildCount = 0;

        /// Counts cumulative duplicate material shader builds detected by material id.
        std::uint64_t MaterialShaderDuplicateBuildCount = 0;

        /// Counts cumulative texture builds performed by the 2D bridge.
        std::uint64_t TextureBuildCount = 0;

        /// Counts cumulative duplicate texture builds detected by texture id.
        std::uint64_t TextureDuplicateBuildCount = 0;
```

- [ ] **Step 6: Track duplicate builds and return snapshots**

Modify `src/platform/windows/win32/win32_render_bridge.cpp`:

- increment texture counters in `Win32RenderManager2D::BuildTextureFromRaw`
- increment material counters in `Win32RenderManager3D::BuildMaterialFromRaw`
- increment model counters in `Win32RenderManager3D::BuildModelFromRaw`
- detect duplicates by checking whether the id is already present in the corresponding live cache map before inserting
- implement both `GetDiagnosticsSnapshot()` methods

Use this minimal shape:

```cpp
        const bool isDuplicateTextureBuild = TextureResources.find(textureId) != TextureResources.end();
        TextureBuildCount++;
        if (isDuplicateTextureBuild) {
            TextureDuplicateBuildCount++;
        }
```

For model ids, use the runtime model id exposed through generated runtime data if present; otherwise count the build but do not mark it duplicate.

- [ ] **Step 7: Run configure/build to verify it passes**

Run:

```powershell
cmake -S . -B build
cmake --build build
```

Expected: PASS

- [ ] **Step 8: Commit**

```bash
git add CMakeLists.txt src/platform/windows/runtime/runtime_render_diagnostics.hpp src/platform/windows/runtime/runtime_render_diagnostics.cpp src/platform/windows/win32/win32_render_bridge.hpp src/platform/windows/win32/win32_render_bridge.cpp
git commit -m "feat: add Windows renderer diagnostics counters"
```

---

### Task 3: Add Structured Packaged Asset And Runtime Build Trace Logging

**Files:**
- Modify: `src/platform/windows/runtime/runtime_render_diagnostics.hpp`
- Modify: `src/platform/windows/runtime/runtime_render_diagnostics.cpp`
- Modify: `src/platform/windows/win32/win32_application.cpp`
- Modify: `src/platform/windows/win32/win32_render_bridge.cpp`

- [ ] **Step 1: Write the failing build step**

Add declarations and call sites for structured diagnostics logging before the helper functions exist.

In `win32_application.cpp`, plan one helper call in `LoadPackagedAsset`:

```cpp
        WriteLifecycleLog(FormatPackagedAssetLoadDiagnostic(relativePath, fullPath.string(), "deserialize_begin").c_str());
```

In `win32_render_bridge.cpp`, plan one helper call in `BuildTextureFromRaw`:

```cpp
        AppendRenderDiagnosticsLine(BuildAssetBuildDiagnostic("texture", textureId, false, "BuildTextureFromRaw"));
```

- [ ] **Step 2: Run configure/build to verify it fails**

Run:

```powershell
cmake -S . -B build
cmake --build build
```

Expected: FAIL because the new helper functions are not defined yet.

- [ ] **Step 3: Add structured packaged asset load log formatting**

Add a packaged-asset diagnostics formatter declaration to `src/platform/windows/runtime/runtime_render_diagnostics.hpp`:

```cpp
    /// Formats one packaged asset load event into a structured diagnostics line.
    std::string FormatPackagedAssetLoadDiagnostic(
        const std::string& relativePath,
        const std::string& fullPath,
        const std::string& stage);
```

Implement it in `src/platform/windows/runtime/runtime_render_diagnostics.cpp`:

```cpp
    std::string FormatPackagedAssetLoadDiagnostic(
        const std::string& relativePath,
        const std::string& fullPath,
        const std::string& stage) {
        std::ostringstream messageBuilder;
        messageBuilder
            << "[Diag] asset.load"
            << " kind=packaged"
            << " stage=" << stage
            << " relative_path=" << relativePath
            << " full_path=" << fullPath;
        return messageBuilder.str();
    }
```

Use it in `LoadPackagedAsset` around:

- file open
- deserialize begin
- deserialize end

- [ ] **Step 4: Add structured renderer build log formatting**

Add a renderer asset-build diagnostics formatter declaration to `src/platform/windows/runtime/runtime_render_diagnostics.hpp`:

```cpp
    /// Formats one renderer-owned asset build event into a structured diagnostics line.
    std::string FormatRuntimeAssetBuildDiagnostic(
        const std::string& kind,
        const std::string& assetId,
        bool duplicate,
        const std::string& source);
```

Implement it in `src/platform/windows/runtime/runtime_render_diagnostics.cpp`:

```cpp
    std::string FormatRuntimeAssetBuildDiagnostic(
        const std::string& kind,
        const std::string& assetId,
        bool duplicate,
        const std::string& source) {
        std::ostringstream messageBuilder;
        messageBuilder
            << "[Diag] asset.build"
            << " kind=" << kind
            << " asset_id=" << (assetId.empty() ? "generated" : assetId)
            << " duplicate=" << (duplicate ? "true" : "false")
            << " source=" << source;
        return messageBuilder.str();
    }
```

Emit these logs from:

- `Win32RenderManager2D::BuildTextureFromRaw`
- `Win32RenderManager3D::BuildModelFromRaw`
- `Win32RenderManager3D::BuildMaterialFromRaw`
- `Win32RenderManager3D::BuildShaderResource`

- [ ] **Step 5: Run configure/build to verify it passes**

Run:

```powershell
cmake -S . -B build
cmake --build build
```

Expected: PASS

- [ ] **Step 6: Commit**

```bash
git add src/platform/windows/runtime/runtime_render_diagnostics.hpp src/platform/windows/runtime/runtime_render_diagnostics.cpp src/platform/windows/win32/win32_application.cpp src/platform/windows/win32/win32_render_bridge.cpp
git commit -m "feat: add Windows asset load diagnostics"
```

---

### Task 4: Add Scene Checkpoint Logging In The Windows Host

**Files:**
- Modify: `src/platform/windows/win32/win32_application.hpp`
- Modify: `src/platform/windows/win32/win32_application.cpp`
- Modify: `src/platform/windows/win32/win32_render_bridge.hpp`

- [ ] **Step 1: Write the failing build step**

Add declarations for scene-checkpoint helpers in `win32_application.hpp` before implementing them:

```cpp
        /// Writes one structured scene checkpoint with RAM and renderer diagnostics.
        void WriteSceneCheckpoint(const char* checkpointName);
```

```cpp
        /// Tracks whether the first steady-state checkpoint after startup scene load has already been written.
        bool HasWrittenInitialSceneSteadyStateCheckpoint;
```

- [ ] **Step 2: Run configure/build to verify it fails**

Run:

```powershell
cmake -S . -B build
cmake --build build
```

Expected: FAIL because the new member or helper is not defined yet.

- [ ] **Step 3: Add checkpoint helper implementation**

Implement `WriteSceneCheckpoint` in `win32_application.cpp` using:

- `CaptureRuntimeMemorySnapshot()`
- `FormatRuntimeMemorySnapshot(...)`
- `EngineRenderManager2D->GetDiagnosticsSnapshot()`
- `EngineRenderManager3D->GetDiagnosticsSnapshot()`
- `FormatRuntimeRenderDiagnosticsSnapshot(...)`

Use this shape:

```cpp
    void Win32Application::WriteSceneCheckpoint(const char* checkpointName) {
        std::ostringstream messageBuilder;
        messageBuilder << "[Diag] scene.checkpoint"
            << " name=" << checkpointName;

#if __has_include("Core.hpp")
        if (EngineCore != nullptr && EngineCore->get_SceneManager() != nullptr) {
            messageBuilder << " loaded_scene_count="
                << EngineCore->get_SceneManager()->get_LoadedScenes()->get_Count();
        }
#endif

        try {
            messageBuilder << " " << FormatRuntimeMemorySnapshot(CaptureRuntimeMemorySnapshot());
        } catch (const std::exception& exception) {
            messageBuilder << " memory_error=\"" << exception.what() << "\"";
        }

        if (EngineRenderManager2D != nullptr) {
            messageBuilder << " " << FormatRuntimeRenderDiagnosticsSnapshot(EngineRenderManager2D->GetDiagnosticsSnapshot());
        }

        if (EngineRenderManager3D != nullptr) {
            messageBuilder << " " << FormatRuntimeRenderDiagnosticsSnapshot(EngineRenderManager3D->GetDiagnosticsSnapshot());
        }

        WriteLifecycleLog(messageBuilder.str().c_str());
    }
```

- [ ] **Step 4: Emit checkpoints at deterministic lifecycle boundaries**

Call `WriteSceneCheckpoint(...)` from:

- `InitializeEngineCore()` before startup scene load
- `InitializeEngineCore()` after startup scene load
- `RenderFrame()` after the first successful loaded frame as `startup_steady_state`

Add one frame-based steady-state guard:

```cpp
        if (EngineInitialized && !HasWrittenInitialSceneSteadyStateCheckpoint) {
            WriteSceneCheckpoint("startup_steady_state");
            HasWrittenInitialSceneSteadyStateCheckpoint = true;
        }
```

Also emit a checkpoint around the startup-scene handoff:

```cpp
        WriteSceneCheckpoint("before_startup_scene_load");
        EngineCore->get_SceneLoadService()->Load(startupScene);
        WriteSceneCheckpoint("after_startup_scene_load");
```

- [ ] **Step 5: Add transition polling hooks for later runtime scene changes**

Add lightweight polling state in `Win32Application` so the main loop can notice when loaded-scene ids or counts change and emit:

- `scene_transition_detected`
- `scene_transition_steady_state`

Use a minimal comparison against the previous loaded-scene count in `RenderFrame()` for this diagnostics pass rather than invasive scene-manager event wiring.

- [ ] **Step 6: Run configure/build to verify it passes**

Run:

```powershell
cmake -S . -B build
cmake --build build
```

Expected: PASS

- [ ] **Step 7: Commit**

```bash
git add src/platform/windows/win32/win32_application.hpp src/platform/windows/win32/win32_application.cpp src/platform/windows/win32/win32_render_bridge.hpp
git commit -m "feat: add Windows scene checkpoint diagnostics"
```

---

### Task 5: Verify Diagnostics Output With The City Flow

**Files:**
- Modify: `docs/superpowers/plans/2026-05-14-windows-scene-memory-diagnostics.md`

- [ ] **Step 1: Build the Windows player with diagnostics enabled**

Run:

```powershell
cmake -S . -B build
cmake --build build --config Debug
```

Expected: PASS

- [ ] **Step 2: Run the City player scenario**

Run the built Windows player against the City packaged output and execute:

1. menu
2. `cube_test`
3. menu

Expected: the lifecycle log contains:

- `[Diag] asset.load`
- `[Diag] asset.build`
- `[Diag] scene.checkpoint`

- [ ] **Step 3: Inspect the lifecycle log for baseline questions**

Verify the log answers:

- what was the RAM footprint at first menu steady-state
- what changed after `cube_test`
- whether the second menu steady-state returned to the first menu cache counts
- which asset ids were built more than once

Expected: at least one clear baseline table can be extracted manually from the log for the follow-up fix pass.

- [ ] **Step 4: Record the observed baseline in the final implementation notes**

Add a short execution note to the plan file or the implementing branch notes with:

- first-menu working set
- `cube_test` working set
- second-menu working set
- duplicate texture/material/model counts observed

- [ ] **Step 5: Commit**

```bash
git add docs/superpowers/plans/2026-05-14-windows-scene-memory-diagnostics.md
git commit -m "docs: capture Windows scene diagnostics baseline"
```

---

## Self-Review

- Spec coverage check:
  - process RAM snapshots: Task 1 and Task 4
  - per-asset-class cache counts: Task 2 and Task 4
  - packaged/content load tracing: Task 3
  - startup and runtime scene checkpoints: Task 4
  - manual City flow comparison baseline: Task 5
  - diagnostics-only scope with no cleanup fixes: enforced in Notes and preserved by all tasks
- Placeholder scan:
  - removed `TODO`/`TBD`
  - each code-changing step includes concrete file paths and code shapes
  - each verification step includes explicit commands and expected outcomes
- Type consistency:
  - `RuntimeMemorySnapshot`, `RuntimeRenderDiagnosticsSnapshot`, `CaptureRuntimeMemorySnapshot`, `FormatRuntimeMemorySnapshot`, `FormatRuntimeRenderDiagnosticsSnapshot`, and `GetDiagnosticsSnapshot()` are used consistently across tasks
