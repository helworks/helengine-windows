#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "platform/windows/runtime/runtime_render_diagnostics.hpp"
#include "platform/windows/runtime/runtime_player_profile.hpp"
#include "platform/windows/runtime/runtime_memory_diagnostics_provider.hpp"

class CameraClearSettings;
class CameraComponent;
class Asset;
class Core;
class FontAsset;
class MeshComponent;
class PlatformInfo;
class RenderManager2D;
class RenderManager3D;
class RuntimeSceneCatalog;
class SceneAsset;
class float4;

namespace helengine::windows {
    class DirectX11Bootstrap;
    class DirectX11Presenter;
    class Win32InputBackend;
    class Win32RenderManager2D;
    class Win32RenderManager3D;
    class Win32Window;

    /// Owns Windows host startup and the main message/render loop for the native player.
    class Win32Application {
    public:
        /// Creates the application runner with no initialized native resources.
        Win32Application();

        /// Releases native bootstrap objects after the application loop finishes.
        ~Win32Application();

        /// Boots the Win32 window and DirectX11 loop and returns the process exit code.
        int Run();

    private:
        /// Creates the main native window for the player host.
        void CreateMainWindow();

        /// Creates the DirectX11 device and presentation resources for the main window.
        void CreateGraphicsBootstrap();

        /// Attaches to the parent console or creates one so host and engine logs have a stable output target.
        void InitializeConsole();

        /// Opens the per-run lifecycle log file beside the executable.
        void InitializeFileLog();

        /// Initializes the generated engine core when it is available in the current build.
        void InitializeEngineCore();

        /// Builds the runtime platform metadata stamped into the packaged player.
        PlatformInfo* BuildRuntimePlatformInfo();

        /// Loads the packaged startup scene from the built content root when one is present.
        void LoadPackagedStartupScene();

        /// Resolves the runtime player profile that controls initial window sizing.
        RuntimePlayerProfile ResolveRuntimePlayerProfile() const;

        /// Builds the runtime scene catalog consumed by scene-loading menu actions in packaged players.
        RuntimeSceneCatalog* BuildRuntimeSceneCatalog();

        /// Samples the current renderer cache counters exposed by the Windows bridge.
        RuntimeRenderCounters BuildRenderCounters() const;

        /// Writes one named scene diagnostics checkpoint into the Windows diagnostics log.
        void WriteSceneDiagnosticsCheckpoint(const char* label);

        /// Captures the current runtime memory snapshot using shared diagnostics when available.
        RuntimeMemorySnapshot CaptureRuntimeMemorySnapshot(std::string* detailMetrics = nullptr) const;

        /// Builds the currently tracked loaded scene id list from shared runtime state.
        std::string BuildTrackedLoadedSceneIds() const;

        /// Polls generated core scene-transition trace fields and emits checkpoints when they change.
        void PollSceneTransitionDiagnostics();

        /// Loads one packaged serialized asset from a build-relative path.
        Asset* LoadPackagedAsset(const std::string& relativePath);

        /// Resolves the current executable directory used as the packaged content root.
        std::filesystem::path ResolveApplicationDirectoryPath() const;

        /// Resolves the lifecycle log file path beside the executable.
        std::filesystem::path ResolveLogFilePath() const;

        /// Runs one non-blocking message pump pass.
        bool PumpMessages();

        /// Renders and presents the current frame.
        void RenderFrame();

        /// Writes one lifecycle message to the host console.
        void WriteLifecycleLog(const char* message) const;

        /// Updates and emits periodic frame statistics for the host loop.
        void UpdateFrameStatistics();

        /// Stores the main native window instance.
        std::unique_ptr<Win32Window> MainWindow;

        /// Stores the DirectX11 device and swap-chain bootstrap.
        std::unique_ptr<DirectX11Bootstrap> Bootstrap;

        /// Stores the clear/present helper for the bootstrap resources.
        std::unique_ptr<DirectX11Presenter> Presenter;

        /// Stores the process exit code requested by the Windows message loop.
        int ExitCode;

        /// Stores the generated engine core instance when the host is built with generated core support.
        Core* EngineCore;

        /// Stores the generated 3D render manager used by the engine core.
        Win32RenderManager3D* EngineRenderManager3D;

        /// Stores the generated 2D render manager used by the engine core.
        Win32RenderManager2D* EngineRenderManager2D;

        /// Stores the generated input backend used by the engine core.
        Win32InputBackend* EngineInputBackend;

        /// Tracks whether the generated engine core finished initialization.
        bool EngineInitialized;

        /// Tracks when the current FPS sampling window started.
        std::chrono::steady_clock::time_point FrameStatisticStartTime;

        /// Counts frames presented since the last FPS log flush.
        std::uint32_t FramesSinceLastStatisticLog;

        /// Streams lifecycle logs into a file beside the executable for crash debugging.
        mutable std::ofstream LifecycleLogFile;

        /// Stores the last observed core-owned scene transition stage.
        std::string LastObservedCoreSceneTransitionStage;

        /// Stores the last observed scene-manager trace stage.
        std::string LastObservedSceneManagerTraceStage;

        /// Stores the last observed scene-manager scene id.
        std::string LastObservedSceneManagerSceneId;

        /// Stores the last observed scene-manager loaded-scene count.
        int LastObservedLoadedSceneCount;

        /// Stores the last observed scene-manager pending-operation count.
        int LastObservedPendingOperationCount;

        /// Stores the last observed scene-load-service trace stage.
        std::string LastObservedSceneLoadStage;

        /// Stores the last observed scene-load-service component type.
        std::string LastObservedSceneLoadComponentTypeId;

        /// Stores the last observed root-entity index reported by the scene-load service.
        int LastObservedSceneLoadRootEntityIndex;

        /// Stores the last observed entity depth reported by the scene-load service.
        int LastObservedSceneLoadEntityDepth;

        /// Counts frames since the most recent observed scene-transition trace change.
        std::uint32_t FramesSinceSceneTraceChange;

        /// Tracks whether one steady-state checkpoint should be emitted after the current scene transition settles.
        bool PendingSteadyStateCheckpoint;

#if defined(HELENGINE_WINDOWS_DEBUG_RUNTIME_DIAGNOSTICS) && __has_include("IRuntimeDiagnosticsProvider.hpp") && __has_include("RuntimeMemoryDiagnosticsSnapshot.hpp")
        /// Stores the debug-build Windows runtime diagnostics provider exposed to the shared core service.
        std::unique_ptr<RuntimeMemoryDiagnosticsProvider> RuntimeDiagnosticsProvider;
#endif
    };
}

