#pragma once

#include <Windows.h>

#include <array>
#include <chrono>
#include <cstdio>
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

#if defined(HELENGINE_WINDOWS_DEBUG_RUNTIME_DIAGNOSTICS)
        /// Installs the debug-build unhandled-exception hook used to record native crash stacks.
        void InstallDebugCrashHandler();

        /// Restores the previous unhandled-exception hook after the host exits normally.
        void UninstallDebugCrashHandler();

        /// Installs debug-build CRT and abort handlers used to record assertion and abort stacks.
        void InstallDebugAbortHandlers();

        /// Restores previous CRT and abort handlers after the host exits normally.
        void UninstallDebugAbortHandlers();

        /// Ensures DbgHelp symbol resolution is available for stack-frame logging.
        bool EnsureDebugSymbolsInitialized() const;

        /// Resolves and writes one stack frame into the lifecycle log.
        /// <param name="frameIndex">Zero-based frame index being written.</param>
        /// <param name="address">Instruction address for the frame.</param>
        void WriteResolvedStackFrame(std::uint32_t frameIndex, std::uint64_t address) const;

        /// Captures the current thread call stack and writes it into the lifecycle log.
        /// <param name="context">High-level context describing why the stack was captured.</param>
        /// <param name="framesToSkip">Number of leading frames to skip before logging.</param>
        void WriteCurrentThreadStackTrace(const char* context, std::uint32_t framesToSkip = 0) const;

        /// Writes one structured-exception summary and stack trace into the lifecycle log.
        /// <param name="context">High-level context describing why the stack was captured.</param>
        /// <param name="exceptionPointers">Structured-exception data captured by Windows.</param>
        void WriteStructuredExceptionStackTrace(const char* context, struct _EXCEPTION_POINTERS* exceptionPointers) const;

        /// Receives top-level Windows structured exceptions and forwards them into the lifecycle log.
        /// <param name="exceptionPointers">Structured-exception data captured by Windows.</param>
        /// <returns>Top-level filter action returned to Windows.</returns>
        static LONG WINAPI HandleUnhandledStructuredException(struct _EXCEPTION_POINTERS* exceptionPointers);

        /// Receives `std::terminate` callbacks and writes a stack trace before chaining to the previous handler.
        static void HandleTerminate();

        /// Receives CRT invalid-parameter failures and writes the associated diagnostics before chaining.
        static void HandleInvalidParameter(
            const wchar_t* expression,
            const wchar_t* functionName,
            const wchar_t* fileName,
            unsigned int lineNumber,
            uintptr_t reserved);

        /// Receives pure-virtual-call failures and writes a stack trace before chaining.
        static void HandlePureVirtualCall();

        /// Receives `SIGABRT` notifications and writes a stack trace before re-raising the signal.
        static void HandleAbortSignal(int signalValue);

        /// Receives CRT debug-report text and mirrors it into the lifecycle log.
        static int __cdecl HandleDebugReport(int reportType, char* message, int* returnValue);
#endif

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

#if defined(HELENGINE_WINDOWS_DEBUG_RUNTIME_DIAGNOSTICS)
        /// Stores one sampled summary of the Win32 process heaps.
        struct DebugWin32HeapSummary {
            /// Stores how many process heaps were enumerated successfully during the sample.
            std::uint64_t HeapCount = 0;

            /// Stores how many heaps could not be locked or walked during the sample.
            std::uint64_t FailedHeapCount = 0;

            /// Stores the number of busy heap blocks across all sampled heaps.
            std::uint64_t BusyBlockCount = 0;

            /// Stores the total busy block payload bytes across all sampled heaps.
            std::uint64_t BusyBytes = 0;

            /// Stores the total committed heap-region bytes across all sampled heaps.
            std::uint64_t RegionCommittedBytes = 0;

            /// Stores the total reserved heap-region bytes across all sampled heaps.
            std::uint64_t RegionReservedBytes = 0;

            /// Stores the total uncommitted heap-region bytes across all sampled heaps.
            std::uint64_t RegionUncommittedBytes = 0;
        };

        /// Stores one busy block sampled from a Win32 process heap.
        struct DebugWin32HeapBusyBlock {
            /// Stores the raw block address returned by the heap walker.
            std::uintptr_t Address = 0;

            /// Stores the busy block payload size in bytes.
            std::uint64_t Size = 0;
        };

        /// Stores one full sampled snapshot for a single Win32 process heap.
        struct DebugWin32HeapSnapshot {
            /// Stores the raw heap handle value used to identify the heap between samples.
            std::uintptr_t HeapHandleValue = 0;

            /// Stores the number of busy blocks discovered in this heap sample.
            std::uint64_t BusyBlockCount = 0;

            /// Stores the total busy payload bytes discovered in this heap sample.
            std::uint64_t BusyBytes = 0;

            /// Stores the committed heap-region bytes discovered in this heap sample.
            std::uint64_t RegionCommittedBytes = 0;

            /// Stores the reserved heap-region bytes discovered in this heap sample.
            std::uint64_t RegionReservedBytes = 0;

            /// Stores the uncommitted heap-region bytes discovered in this heap sample.
            std::uint64_t RegionUncommittedBytes = 0;

            /// Stores how many busy blocks were captured into the fixed sample buffer.
            std::uint32_t BusyBlockSampleCount = 0;

            /// Stores how many busy blocks were omitted because the fixed sample buffer filled up.
            std::uint32_t BusyBlockOverflowCount = 0;

            /// Stores every busy block discovered while walking the heap until the fixed sample buffer fills.
            std::array<DebugWin32HeapBusyBlock, 256> BusyBlocks;
        };

        /// Captures the CRT debug heap state used as the steady-state allocation baseline.
        void CaptureDebugAllocationBaseline();

        /// Logs the live CRT debug heap delta relative to the captured steady-state baseline.
        void LogDebugAllocationDelta();

        /// Captures the current Win32 process-heap summary used by debug allocation diagnostics.
        DebugWin32HeapSummary CaptureDebugWin32HeapSummary() const;

        /// Captures the current Win32 process-heap snapshots used for block-level attribution.
        std::uint32_t CaptureDebugWin32HeapSnapshots(std::array<DebugWin32HeapSnapshot, 32>& snapshots) const;

        /// Writes one allocation-diagnostics line without mirroring it into structured runtime diagnostics.
        void WriteDebugAllocationLog(const char* message) const;

        /// Appends capped block-level Win32 heap delta attribution to the supplied diagnostics builder.
        void AppendDebugWin32HeapBlockDelta(
            char* buffer,
            std::size_t bufferSize,
            std::size_t& writtenLength,
            const std::array<DebugWin32HeapSnapshot, 32>& currentHeapSnapshots,
            std::uint32_t currentHeapSnapshotCount) const;
#endif

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

#if defined(HELENGINE_WINDOWS_DEBUG_RUNTIME_DIAGNOSTICS)
        /// Stores whether one steady-state CRT allocation baseline has already been captured for the current run.
        bool DebugAllocationBaselineCaptured;

        /// Stores the captured steady-state CRT allocation baseline.
        _CrtMemState DebugAllocationBaselineState;

        /// Stores the captured steady-state Win32 heap baseline.
        DebugWin32HeapSummary DebugWin32HeapBaseline;

        /// Stores the captured steady-state Win32 heap snapshots used for block-level attribution.
        std::array<DebugWin32HeapSnapshot, 32> DebugWin32HeapBaselineSnapshots;

        /// Stores how many baseline heap snapshots were captured into the fixed baseline array.
        std::uint32_t DebugWin32HeapBaselineSnapshotCount;

        /// Stores the raw allocation diagnostics log stream used to avoid iostream allocations while sampling.
        mutable FILE* DebugAllocationLogFile;
#endif
    };
}

