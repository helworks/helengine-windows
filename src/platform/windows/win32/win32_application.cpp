#include "platform/windows/win32/win32_application.hpp"

#include <Windows.h>
#include <DbgHelp.h>
#include <Psapi.h>

#ifdef DrawText
#undef DrawText
#endif

#include <array>
#include <algorithm>
#include <chrono>
#include <csignal>
#include <crtdbg.h>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <malloc.h>
#include <mutex>
#include <new>
#include <sstream>
#include <stdexcept>

#include "platform/windows/directx11/directx11_bootstrap.hpp"
#include "platform/windows/directx11/directx11_presenter.hpp"
#include "platform/windows/runtime/runtime_memory_snapshot.hpp"
#include "platform/windows/runtime/runtime_memory_diagnostics_provider.hpp"
#include "platform/windows/runtime/runtime_player_profile_loader.hpp"
#include "platform/windows/runtime/runtime_render_diagnostics.hpp"
#include "platform/windows/win32/win32_input_bridge.hpp"
#include "platform/windows/win32/win32_render_bridge.hpp"
#include "platform/windows/win32/win32_window.hpp"

#if __has_include("runtime/runtime_player_settings_manifest.hpp")
#include "runtime/runtime_player_settings_manifest.hpp"
#endif

#if __has_include("Core.hpp")
#include "Asset.hpp"
#include "AssetSerializer.hpp"
#include "Core.hpp"
#include "Logger.hpp"
#include "PlatformInfo.hpp"
#include "RenderManager2D.hpp"
#include "RenderManager3D.hpp"
#include "SceneAsset.hpp"
#include "RuntimeSceneCatalog.hpp"
#include "RuntimeSceneCatalogEntry.hpp"
#include "runtime/runtime_startup_manifest.hpp"
#include "runtime/runtime_scene_catalog_manifest.hpp"
#include "runtime/array.hpp"
#include "runtime/native_exceptions.hpp"
#include "system/io/file.hpp"
#if __has_include("RuntimeMemoryDiagnosticsSnapshot.hpp")
#include "RuntimeMemoryDiagnosticsSnapshot.hpp"
#endif
#if __has_include("RuntimeDiagnosticsMetric.hpp")
#include "RuntimeDiagnosticsMetric.hpp"
#endif
#if __has_include("RuntimeDiagnosticsService.hpp")
#include "RuntimeDiagnosticsService.hpp"
#endif
#if __has_include("FontAssetBinarySerializer.hpp")
#include "FontAssetBinarySerializer.hpp"
#endif
#if __has_include("RuntimeSceneAssetReferenceResolver.hpp")
#include "RuntimeSceneAssetReferenceResolver.hpp"
#endif
#endif

namespace helengine::windows {
    namespace {
#if defined(HELENGINE_WINDOWS_DEBUG_RUNTIME_DIAGNOSTICS)
        /// Maximum number of frames emitted for one debug stack trace.
        constexpr USHORT StackTraceFrameCapacity = 62;

        /// Controls whether debug allocation logs capture per-block Win32 heap attribution or only summary deltas.
        constexpr bool EnableDebugHeapBlockAttribution = false;

        /// Controls whether periodic frame-statistic logs are mirrored through the general lifecycle logging path.
        constexpr bool EnableFrameStatisticLifecycleLogging = false;

        /// Controls whether periodic frame-statistic logs capture runtime memory and render counters.
        constexpr bool EnableFrameStatisticRuntimeSampling = false;

        /// Controls whether the debug host captures and logs in-process CRT and Win32 heap deltas.
        constexpr bool EnableDebugAllocationTracking = false;

        /// Controls whether the debug host emits scene-transition diagnostics checkpoints.
        constexpr bool EnableSceneDiagnosticsCheckpointLogging = false;

        /// Controls whether debug allocation tracking samples Win32 process heaps in addition to CRT checkpoints.
        constexpr bool EnableDebugWin32HeapSampling = false;

        /// Controls whether CRT normal-block allocations are aggregated by live call stack deltas.
        constexpr bool EnableDebugAllocationCallsiteTracking = false;

        /// Maximum number of frames retained for one tracked allocation call stack.
        constexpr USHORT DebugAllocationTrackedFrameCapacity = 20;

        /// Fixed-capacity request table size used for live CRT allocation tracking.
        constexpr std::size_t DebugAllocationTrackedRequestCapacity = 524288;

        /// Fixed-capacity stack table size used for live CRT allocation tracking.
        constexpr std::size_t DebugAllocationTrackedStackCapacity = 32768;

        /// One open-addressing request entry keyed by CRT allocation request number.
        struct DebugTrackedAllocationRequest {
            long RequestNumber;
            std::uint32_t StackIndex;
            std::uint64_t Size;
            std::uint8_t State;
        };

        /// One aggregated live-allocation stack bucket.
        struct DebugTrackedAllocationStack {
            std::uint64_t Hash;
            std::uint64_t LiveBytes;
            std::uint64_t BaselineLiveBytes;
            std::uint32_t LiveAllocations;
            std::uint32_t BaselineLiveAllocations;
            USHORT FrameCount;
            bool Occupied;
            void* Frames[DebugAllocationTrackedFrameCapacity];
        };

        /// Stores the currently running application instance used by the debug crash handler.
        Win32Application* ActiveCrashLoggingApplication = nullptr;

        /// Stores the previously registered top-level structured-exception filter.
        LPTOP_LEVEL_EXCEPTION_FILTER PreviousUnhandledExceptionFilter = nullptr;

        /// Tracks one-time DbgHelp initialization for the current process.
        std::once_flag DebugSymbolInitializationFlag;

        /// Tracks whether the DbgHelp symbol engine initialized successfully.
        bool DebugSymbolsInitialized = false;

        /// Tracks whether the live CRT request tracker has been initialized successfully.
        bool DebugAllocationCallsiteTrackerInitialized = false;

        /// Tracks whether the CRT allocation hook has already been installed.
        bool DebugAllocationCallsiteHookInstalled = false;

        /// Tracks whether the request tracker overflowed and dropped entries.
        bool DebugAllocationCallsiteTrackerOverflowed = false;

        /// Temporarily suppresses allocation-hook bookkeeping while diagnostics formatting runs.
        bool DebugAllocationCallsiteTrackingSuspended = false;

        /// Heap used to allocate fixed-capacity tracking tables without recursing through the CRT debug heap.
        HANDLE DebugAllocationCallsiteTrackerHeap = nullptr;

        /// Fixed-capacity table of live CRT requests keyed by request number.
        DebugTrackedAllocationRequest* DebugTrackedAllocationRequests = nullptr;

        /// Fixed-capacity table of aggregated call stacks keyed by stack hash.
        DebugTrackedAllocationStack* DebugTrackedAllocationStacks = nullptr;

        /// Prevents recursive allocation-hook tracking on the current thread.
        thread_local bool DebugAllocationCallsiteTrackingReentry = false;

        /// Stores the previously registered terminate handler.
        std::terminate_handler PreviousTerminateHandler = nullptr;

        /// Stores the previously registered invalid-parameter handler.
        _invalid_parameter_handler PreviousInvalidParameterHandler = nullptr;

        /// Stores the previously registered pure-virtual-call handler.
        _purecall_handler PreviousPureCallHandler = nullptr;

        /// Stores the previously registered `SIGABRT` handler.
        void (*PreviousAbortSignalHandler)(int) = nullptr;

#endif

#if defined(HELENGINE_WINDOWS_DEBUG_RUNTIME_DIAGNOSTICS) && __has_include("RuntimeMemoryDiagnosticsSnapshot.hpp") && __has_include("RuntimeDiagnosticsMetric.hpp")
        /// Releases one shared runtime diagnostics snapshot together with the nested metric and scene-id lists it owns.
        void DeleteRuntimeMemoryDiagnosticsSnapshot(RuntimeMemoryDiagnosticsSnapshot* snapshot) {
            if (snapshot == nullptr) {
                return;
            }

            List<RuntimeDiagnosticsMetric*>* detailMetrics = snapshot->get_DetailMetrics();
            if (detailMetrics != nullptr) {
                for (int32_t index = 0; index < detailMetrics->get_Count(); index++) {
                    delete (*detailMetrics)[index];
                }

                delete detailMetrics;
                snapshot->set_DetailMetrics(nullptr);
            }

            List<std::string>* trackedSceneIds = snapshot->get_TrackedSceneIds();
            if (trackedSceneIds != nullptr) {
                delete trackedSceneIds;
                snapshot->set_TrackedSceneIds(nullptr);
            }

            delete snapshot;
        }
#endif

#if defined(HELENGINE_WINDOWS_DEBUG_RUNTIME_DIAGNOSTICS)
        /// Resolves one fixed-capacity request slot for the supplied CRT allocation request number.
        DebugTrackedAllocationRequest* FindTrackedAllocationRequestSlot(long requestNumber, bool createIfMissing) {
            if (requestNumber <= 0 || DebugTrackedAllocationRequests == nullptr) {
                return nullptr;
            }

            std::size_t initialIndex = static_cast<std::size_t>(requestNumber) & (DebugAllocationTrackedRequestCapacity - 1);
            DebugTrackedAllocationRequest* firstTombstone = nullptr;
            for (std::size_t probeOffset = 0; probeOffset < DebugAllocationTrackedRequestCapacity; probeOffset++) {
                DebugTrackedAllocationRequest* entry = &DebugTrackedAllocationRequests[(initialIndex + probeOffset) & (DebugAllocationTrackedRequestCapacity - 1)];
                if (entry->State == 1 && entry->RequestNumber == requestNumber) {
                    return entry;
                }

                if (entry->State == 2 && firstTombstone == nullptr) {
                    firstTombstone = entry;
                }

                if (entry->State == 0) {
                    return createIfMissing ? (firstTombstone != nullptr ? firstTombstone : entry) : nullptr;
                }
            }

            return createIfMissing ? firstTombstone : nullptr;
        }

        /// Computes one stable hash for a captured stack-trace prefix.
        std::uint64_t HashTrackedAllocationFrames(const void* const* frames, USHORT frameCount) {
            std::uint64_t hash = 1469598103934665603ull;
            for (USHORT frameIndex = 0; frameIndex < frameCount; frameIndex++) {
                hash ^= static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(frames[frameIndex]));
                hash *= 1099511628211ull;
            }

            return hash;
        }

        /// Resolves one aggregated live-stack slot for the supplied captured frames.
        std::uint32_t FindOrCreateTrackedAllocationStack(const void* const* frames, USHORT frameCount) {
            if (DebugTrackedAllocationStacks == nullptr || frameCount == 0) {
                return UINT32_MAX;
            }

            std::uint64_t hash = HashTrackedAllocationFrames(frames, frameCount);
            std::size_t initialIndex = static_cast<std::size_t>(hash) & (DebugAllocationTrackedStackCapacity - 1);
            for (std::size_t probeOffset = 0; probeOffset < DebugAllocationTrackedStackCapacity; probeOffset++) {
                std::uint32_t index = static_cast<std::uint32_t>((initialIndex + probeOffset) & (DebugAllocationTrackedStackCapacity - 1));
                DebugTrackedAllocationStack* entry = &DebugTrackedAllocationStacks[index];
                if (entry->Occupied) {
                    if (entry->Hash != hash || entry->FrameCount != frameCount) {
                        continue;
                    }

                    bool allFramesMatch = true;
                    for (USHORT frameIndex = 0; frameIndex < frameCount; frameIndex++) {
                        if (entry->Frames[frameIndex] != frames[frameIndex]) {
                            allFramesMatch = false;
                            break;
                        }
                    }

                    if (allFramesMatch) {
                        return index;
                    }

                    continue;
                }

                entry->Occupied = true;
                entry->Hash = hash;
                entry->LiveBytes = 0;
                entry->BaselineLiveBytes = 0;
                entry->LiveAllocations = 0;
                entry->BaselineLiveAllocations = 0;
                entry->FrameCount = frameCount;
                for (USHORT frameIndex = 0; frameIndex < frameCount; frameIndex++) {
                    entry->Frames[frameIndex] = const_cast<void*>(frames[frameIndex]);
                }
                return index;
            }

            return UINT32_MAX;
        }

        /// Records one CRT normal-block allocation into the fixed-capacity live request tracker.
        void TrackDebugAllocationCallsite(long requestNumber, size_t size) {
            if (size == 0 || requestNumber <= 0) {
                return;
            }

            std::array<void*, DebugAllocationTrackedFrameCapacity> frames {};
            USHORT frameCount = CaptureStackBackTrace(2, DebugAllocationTrackedFrameCapacity, frames.data(), nullptr);
            if (frameCount == 0) {
                return;
            }

            std::uint32_t stackIndex = FindOrCreateTrackedAllocationStack(frames.data(), frameCount);
            DebugTrackedAllocationRequest* requestEntry = FindTrackedAllocationRequestSlot(requestNumber, true);
            if (stackIndex == UINT32_MAX || requestEntry == nullptr) {
                DebugAllocationCallsiteTrackerOverflowed = true;
                return;
            }

            if (requestEntry->State == 1 && requestEntry->StackIndex < DebugAllocationTrackedStackCapacity) {
                DebugTrackedAllocationStack* previousStack = &DebugTrackedAllocationStacks[requestEntry->StackIndex];
                if (previousStack->LiveBytes >= requestEntry->Size) {
                    previousStack->LiveBytes -= requestEntry->Size;
                } else {
                    previousStack->LiveBytes = 0;
                }
                if (previousStack->LiveAllocations > 0) {
                    previousStack->LiveAllocations--;
                }
            }

            requestEntry->RequestNumber = requestNumber;
            requestEntry->StackIndex = stackIndex;
            requestEntry->Size = static_cast<std::uint64_t>(size);
            requestEntry->State = 1;

            DebugTrackedAllocationStack* stackEntry = &DebugTrackedAllocationStacks[stackIndex];
            stackEntry->LiveBytes += static_cast<std::uint64_t>(size);
            stackEntry->LiveAllocations++;
        }

        /// Removes one CRT normal-block allocation from the live request tracker.
        void UntrackDebugAllocationCallsite(long requestNumber) {
            DebugTrackedAllocationRequest* requestEntry = FindTrackedAllocationRequestSlot(requestNumber, false);
            if (requestEntry == nullptr || requestEntry->State != 1 || requestEntry->StackIndex >= DebugAllocationTrackedStackCapacity) {
                return;
            }

            DebugTrackedAllocationStack* stackEntry = &DebugTrackedAllocationStacks[requestEntry->StackIndex];
            if (stackEntry->LiveBytes >= requestEntry->Size) {
                stackEntry->LiveBytes -= requestEntry->Size;
            } else {
                stackEntry->LiveBytes = 0;
            }
            if (stackEntry->LiveAllocations > 0) {
                stackEntry->LiveAllocations--;
            }

            requestEntry->RequestNumber = 0;
            requestEntry->StackIndex = 0;
            requestEntry->Size = 0;
            requestEntry->State = 2;
        }

        /// CRT debug allocation hook that attributes live normal-block deltas to call stacks.
        int __cdecl DebugTrackedAllocationHook(
            int allocType,
            void* userData,
            size_t size,
            int blockType,
            long requestNumber,
            const unsigned char* filename,
            int lineNumber) {
            (void)userData;
            (void)filename;
            (void)lineNumber;

            if (!EnableDebugAllocationTracking ||
                !EnableDebugAllocationCallsiteTracking ||
                DebugAllocationCallsiteTrackingSuspended ||
                DebugAllocationCallsiteTrackingReentry ||
                blockType != _NORMAL_BLOCK ||
                !DebugAllocationCallsiteTrackerInitialized) {
                return TRUE;
            }

            DebugAllocationCallsiteTrackingReentry = true;
            if (allocType == _HOOK_ALLOC || allocType == _HOOK_REALLOC) {
                TrackDebugAllocationCallsite(requestNumber, size);
            } else if (allocType == _HOOK_FREE) {
                UntrackDebugAllocationCallsite(requestNumber);
            }
            DebugAllocationCallsiteTrackingReentry = false;
            return TRUE;
        }

        /// Allocates fixed-capacity tracking tables and installs the CRT debug allocation hook on first use.
        void EnsureDebugAllocationCallsiteTrackerInitialized() {
            if (!EnableDebugAllocationTracking ||
                !EnableDebugAllocationCallsiteTracking ||
                DebugAllocationCallsiteTrackerInitialized) {
                return;
            }

            DebugAllocationCallsiteTrackerHeap = GetProcessHeap();
            if (DebugAllocationCallsiteTrackerHeap == nullptr) {
                DebugAllocationCallsiteTrackerOverflowed = true;
                return;
            }

            DebugTrackedAllocationRequests = static_cast<DebugTrackedAllocationRequest*>(HeapAlloc(
                DebugAllocationCallsiteTrackerHeap,
                HEAP_ZERO_MEMORY,
                sizeof(DebugTrackedAllocationRequest) * DebugAllocationTrackedRequestCapacity));
            DebugTrackedAllocationStacks = static_cast<DebugTrackedAllocationStack*>(HeapAlloc(
                DebugAllocationCallsiteTrackerHeap,
                HEAP_ZERO_MEMORY,
                sizeof(DebugTrackedAllocationStack) * DebugAllocationTrackedStackCapacity));
            if (DebugTrackedAllocationRequests == nullptr || DebugTrackedAllocationStacks == nullptr) {
                DebugAllocationCallsiteTrackerOverflowed = true;
                return;
            }

            _CrtSetAllocHook(DebugTrackedAllocationHook);
            DebugAllocationCallsiteHookInstalled = true;
            DebugAllocationCallsiteTrackerInitialized = true;
        }
#endif
    }

    /// Creates the application runner with no initialized native resources.
    Win32Application::Win32Application()
        : ExitCode(0),
          EngineCore(nullptr),
          EngineRenderManager3D(nullptr),
          EngineRenderManager2D(nullptr),
          EngineInputBackend(nullptr),
          EngineInitialized(false),
          FrameStatisticStartTime(std::chrono::steady_clock::now()),
          FramesSinceLastStatisticLog(0),
          LastObservedLoadedSceneCount(-1),
          LastObservedPendingOperationCount(-1),
          LastObservedSceneLoadRootEntityIndex(-1),
          LastObservedSceneLoadEntityDepth(-1),
          FramesSinceSceneTraceChange(0),
          PendingSteadyStateCheckpoint(false)
#if defined(HELENGINE_WINDOWS_DEBUG_RUNTIME_DIAGNOSTICS)
          , DebugAllocationBaselineCaptured(false),
          DebugAllocationBaselineState(),
          DebugWin32HeapBaseline(),
          DebugWin32HeapBaselineSnapshots(),
          DebugWin32HeapBaselineSnapshotCount(0)
#endif
    {
    }

    /// Releases native bootstrap objects after the application loop finishes.
    Win32Application::~Win32Application() {
#if __has_include("Core.hpp")
        if (EngineCore != nullptr) {
            EngineCore->Dispose();
        }
        delete EngineCore;
        delete EngineInputBackend;
        delete EngineRenderManager2D;
        delete EngineRenderManager3D;
#endif
    }

    /// Boots the Win32 window and DirectX11 loop and returns the process exit code.
    int Win32Application::Run() {
        try {
            InitializeConsole();
            RuntimeRenderDiagnostics::Initialize(ResolveApplicationDirectoryPath());
            RuntimeRenderDiagnostics::Reset();
            InitializeFileLog();
#if defined(HELENGINE_WINDOWS_DEBUG_RUNTIME_DIAGNOSTICS)
            InstallDebugCrashHandler();
            InstallDebugAbortHandlers();
#endif
            WriteLifecycleLog("Host startup began.");
            CreateMainWindow();
            CreateGraphicsBootstrap();
            InitializeEngineCore();
            WriteLifecycleLog("Entering render loop.");

            while (PumpMessages()) {
                RenderFrame();
            }
        } catch (Exception* exception) {
            std::ostringstream messageBuilder;
            messageBuilder << "Fatal host/engine exception: " << (exception != nullptr ? exception->what() : "null");
            std::string message = messageBuilder.str();
            WriteLifecycleLog(message.c_str());
#if defined(HELENGINE_WINDOWS_DEBUG_RUNTIME_DIAGNOSTICS)
            WriteCurrentThreadStackTrace("Fatal host/engine stack trace.", 1);
            UninstallDebugAbortHandlers();
            UninstallDebugCrashHandler();
#endif
            delete exception;
            return EXIT_FAILURE;
        } catch (const std::exception& exception) {
            std::ostringstream messageBuilder;
            messageBuilder << "Fatal host/engine exception: " << exception.what();
            std::string message = messageBuilder.str();
            WriteLifecycleLog(message.c_str());
#if defined(HELENGINE_WINDOWS_DEBUG_RUNTIME_DIAGNOSTICS)
            WriteCurrentThreadStackTrace("Fatal host/engine stack trace.", 1);
            UninstallDebugAbortHandlers();
            UninstallDebugCrashHandler();
#endif
            return EXIT_FAILURE;
        }

        std::ostringstream messageBuilder;
        messageBuilder << "Host shutdown requested with exit code " << ExitCode << '.';
        std::string message = messageBuilder.str();
        WriteLifecycleLog(message.c_str());
#if defined(HELENGINE_WINDOWS_DEBUG_RUNTIME_DIAGNOSTICS)
        UninstallDebugAbortHandlers();
        UninstallDebugCrashHandler();
#endif
        return ExitCode;
    }

    /// Attaches to the parent console or creates one so host and engine logs have a stable output target.
    void Win32Application::InitializeConsole() {
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
            AllocConsole();
        }

        FILE* stream = nullptr;
        freopen_s(&stream, "CONOUT$", "w", stdout);
        freopen_s(&stream, "CONOUT$", "w", stderr);
        std::ios::sync_with_stdio(true);
    }

    /// Opens the lifecycle log file beside the executable for crash diagnostics.
    void Win32Application::InitializeFileLog() {
        std::filesystem::path logPath = ResolveLogFilePath();
        std::filesystem::create_directories(logPath.parent_path());
        LifecycleLogFile.open(logPath, std::ios::out | std::ios::trunc);
        if (LifecycleLogFile.is_open()) {
            LifecycleLogFile << "[Host] Lifecycle log opened at " << logPath.string() << '\n';
            LifecycleLogFile.flush();
        }
    }

    /// Creates the main native window for the player host.
    void Win32Application::CreateMainWindow() {
        RuntimePlayerProfile profile = ResolveRuntimePlayerProfile();
        MainWindow = std::make_unique<Win32Window>(L"HelEngine Windows Host", profile.ResolutionWidth, profile.ResolutionHeight);
        MainWindow->Create();
        MainWindow->Show();
        {
            std::ostringstream messageBuilder;
            messageBuilder << "Main window configured to default client size "
                << profile.ResolutionWidth
                << "x"
                << profile.ResolutionHeight
                << '.';
            std::string message = messageBuilder.str();
            WriteLifecycleLog(message.c_str());
        }
        WriteLifecycleLog("Main window loaded and shown.");
    }

    /// Creates the DirectX11 device and presentation resources for the main window.
    void Win32Application::CreateGraphicsBootstrap() {
        Bootstrap = std::make_unique<DirectX11Bootstrap>(
            MainWindow->GetHandle(),
            MainWindow->GetClientWidth(),
            MainWindow->GetClientHeight());
        Presenter = std::make_unique<DirectX11Presenter>(*Bootstrap);
        WriteLifecycleLog("DirectX 11 bootstrap initialized.");
    }

    /// Initializes the generated engine core when it is available in the current build.
    void Win32Application::InitializeEngineCore() {
#if __has_include("Core.hpp")
        EngineCore = new Core();
        CoreInitializationOptions* options = EngineCore->get_InitializationOptions();
        options->ContentRootPath = ResolveApplicationDirectoryPath().string();
        options->UpdateOrderLayers = 4;
        options->RenderOrderLayers3D = 4;
        options->UpdateListInitialCapacity = 64;
        options->RenderList2DInitialCapacity = 64;
        options->RenderList3DInitialCapacity = 64;
        options->SceneCatalog = BuildRuntimeSceneCatalog();
#if defined(HELENGINE_WINDOWS_DEBUG_RUNTIME_DIAGNOSTICS) && __has_include("IRuntimeDiagnosticsProvider.hpp") && __has_include("RuntimeMemoryDiagnosticsSnapshot.hpp")
        RuntimeDiagnosticsProvider = std::make_unique<RuntimeMemoryDiagnosticsProvider>();
        options->set_RuntimeDiagnosticsProvider(RuntimeDiagnosticsProvider.get());
#endif

        EngineRenderManager3D = new Win32RenderManager3D(*Bootstrap);
        EngineRenderManager2D = new Win32RenderManager2D(*Bootstrap);
        EngineInputBackend = new Win32InputBackend(MainWindow.get());

        EngineRenderManager3D->AddWindow(
            reinterpret_cast<intptr_t>(MainWindow->GetHandle()),
            MainWindow->GetClientWidth(),
            MainWindow->GetClientHeight());

        PlatformInfo* platformInfo = BuildRuntimePlatformInfo();
        EngineCore->Initialize(EngineRenderManager3D, EngineRenderManager2D, EngineInputBackend, platformInfo, options);
#if defined(HELENGINE_WINDOWS_DEBUG_RUNTIME_DIAGNOSTICS)
        EnsureDebugAllocationCallsiteTrackerInitialized();
#endif
        WriteSceneDiagnosticsCheckpoint("after_core_initialize");
        std::chrono::steady_clock::time_point sceneLoadStart = std::chrono::steady_clock::now();
        try {
            WriteLifecycleLog("Loading packaged startup scene.");
            LoadPackagedStartupScene();
            WriteLifecycleLog("Packaged startup scene load completed.");
        } catch (const std::exception& exception) {
            std::ostringstream messageBuilder;
            messageBuilder << "Packaged startup scene load failed: " << exception.what();
            std::string message = messageBuilder.str();
            WriteLifecycleLog(message.c_str());
#if defined(HELENGINE_WINDOWS_DEBUG_RUNTIME_DIAGNOSTICS)
            WriteCurrentThreadStackTrace("Packaged startup scene failure stack trace.", 1);
#endif
            throw;
        }
        std::chrono::steady_clock::time_point sceneLoadEnd = std::chrono::steady_clock::now();
        std::chrono::duration<double, std::milli> sceneLoadElapsed = sceneLoadEnd - sceneLoadStart;
        EngineInitialized = true;
        Logger::WriteLine("Core initialized.");
        {
            std::ostringstream messageBuilder;
            messageBuilder << std::fixed << std::setprecision(2)
                << "Packaged startup scene loaded in "
                << sceneLoadElapsed.count()
                << " ms.";
            std::string message = messageBuilder.str();
            WriteLifecycleLog(message.c_str());
        }
        WriteLifecycleLog("Engine core initialized.");
        WriteSceneDiagnosticsCheckpoint("after_startup_scene_load");
        PendingSteadyStateCheckpoint = true;
        FramesSinceSceneTraceChange = 0;
#else
        WriteLifecycleLog("Generated engine core is not included in this build.");
#endif
    }

#if defined(HELENGINE_WINDOWS_DEBUG_RUNTIME_DIAGNOSTICS)
    /// Installs the debug-build unhandled-exception hook used to record native crash stacks.
    void Win32Application::InstallDebugCrashHandler() {
        ActiveCrashLoggingApplication = this;
        PreviousUnhandledExceptionFilter = SetUnhandledExceptionFilter(&Win32Application::HandleUnhandledStructuredException);
    }

    /// Restores the previous unhandled-exception hook after the host exits normally.
    void Win32Application::UninstallDebugCrashHandler() {
        if (ActiveCrashLoggingApplication != this) {
            return;
        }

        SetUnhandledExceptionFilter(PreviousUnhandledExceptionFilter);
        PreviousUnhandledExceptionFilter = nullptr;
        ActiveCrashLoggingApplication = nullptr;
    }

    /// Installs debug-build CRT and abort handlers used to record assertion and abort stacks.
    void Win32Application::InstallDebugAbortHandlers() {
        PreviousTerminateHandler = std::set_terminate(&Win32Application::HandleTerminate);
        PreviousInvalidParameterHandler = _set_invalid_parameter_handler(&Win32Application::HandleInvalidParameter);
        PreviousPureCallHandler = _set_purecall_handler(&Win32Application::HandlePureVirtualCall);
        PreviousAbortSignalHandler = std::signal(SIGABRT, &Win32Application::HandleAbortSignal);
#if defined(_DEBUG)
        _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, &Win32Application::HandleDebugReport);
#endif
    }

    /// Restores previous CRT and abort handlers after the host exits normally.
    void Win32Application::UninstallDebugAbortHandlers() {
#if defined(_DEBUG)
        _CrtSetReportHook2(_CRT_RPTHOOK_REMOVE, &Win32Application::HandleDebugReport);
#endif
        _set_purecall_handler(PreviousPureCallHandler);
        _set_invalid_parameter_handler(PreviousInvalidParameterHandler);
        std::set_terminate(PreviousTerminateHandler);
        std::signal(SIGABRT, PreviousAbortSignalHandler);
        PreviousPureCallHandler = nullptr;
        PreviousInvalidParameterHandler = nullptr;
        PreviousTerminateHandler = nullptr;
        PreviousAbortSignalHandler = nullptr;
    }

    /// Ensures DbgHelp symbol resolution is available for stack-frame logging.
    bool Win32Application::EnsureDebugSymbolsInitialized() const {
        HANDLE processHandle = GetCurrentProcess();
        std::call_once(DebugSymbolInitializationFlag, [processHandle]() {
            SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
            DebugSymbolsInitialized = SymInitialize(processHandle, nullptr, TRUE) == TRUE;
        });

        if (!DebugSymbolsInitialized) {
            std::ostringstream messageBuilder;
            messageBuilder << "DbgHelp symbol initialization failed with error " << GetLastError() << '.';
            WriteLifecycleLog(messageBuilder.str().c_str());
        }

        return DebugSymbolsInitialized;
    }

    /// Resolves and writes one stack frame into the lifecycle log.
    void Win32Application::WriteResolvedStackFrame(std::uint32_t frameIndex, std::uint64_t address) const {
        HANDLE processHandle = GetCurrentProcess();

        std::array<std::uint8_t, sizeof(SYMBOL_INFO) + MAX_SYM_NAME> symbolStorage {};
        SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolStorage.data());
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = MAX_SYM_NAME;

        DWORD64 displacement = 0;
        IMAGEHLP_LINE64 lineInfo {};
        lineInfo.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
        DWORD lineDisplacement = 0;

        bool resolvedSymbol = SymFromAddr(processHandle, static_cast<DWORD64>(address), &displacement, symbol) == TRUE;
        bool resolvedLine = SymGetLineFromAddr64(processHandle, static_cast<DWORD64>(address), &lineDisplacement, &lineInfo) == TRUE;

        std::ostringstream messageBuilder;
        messageBuilder << "Stack[" << frameIndex << "] 0x" << std::hex << std::uppercase << address << std::dec;
        if (resolvedSymbol) {
            messageBuilder << " " << symbol->Name;
            if (displacement != 0) {
                messageBuilder << " +0x" << std::hex << std::uppercase << displacement << std::dec;
            }
        }

        if (resolvedLine) {
            messageBuilder << " (" << lineInfo.FileName << ":" << lineInfo.LineNumber << ")";
        }

        WriteLifecycleLog(messageBuilder.str().c_str());
    }

    /// Captures the current thread call stack and writes it into the lifecycle log.
    void Win32Application::WriteCurrentThreadStackTrace(const char* context, std::uint32_t framesToSkip) const {
        WriteLifecycleLog(context != nullptr ? context : "Current thread stack trace.");
        if (!EnsureDebugSymbolsInitialized()) {
            return;
        }

        std::array<void*, StackTraceFrameCapacity> frames {};
        USHORT frameCount = CaptureStackBackTrace(
            static_cast<DWORD>(framesToSkip + 1),
            StackTraceFrameCapacity,
            frames.data(),
            nullptr);
        if (frameCount == 0) {
            WriteLifecycleLog("Stack trace capture returned no frames.");
            return;
        }

        for (USHORT frameIndex = 0; frameIndex < frameCount; frameIndex++) {
            WriteResolvedStackFrame(frameIndex, reinterpret_cast<std::uint64_t>(frames[frameIndex]));
        }
    }

    /// Writes one structured-exception summary and stack trace into the lifecycle log.
    void Win32Application::WriteStructuredExceptionStackTrace(const char* context, EXCEPTION_POINTERS* exceptionPointers) const {
        WriteLifecycleLog(context != nullptr ? context : "Unhandled structured exception.");

        if (exceptionPointers == nullptr || exceptionPointers->ExceptionRecord == nullptr) {
            WriteLifecycleLog("Structured exception details were unavailable.");
            WriteCurrentThreadStackTrace("Fallback current-thread stack trace.", 1);
            return;
        }

        std::ostringstream summaryBuilder;
        summaryBuilder
            << "Structured exception code=0x"
            << std::hex
            << std::uppercase
            << exceptionPointers->ExceptionRecord->ExceptionCode
            << " address=0x"
            << reinterpret_cast<std::uint64_t>(exceptionPointers->ExceptionRecord->ExceptionAddress)
            << std::dec;
        WriteLifecycleLog(summaryBuilder.str().c_str());

        if (!EnsureDebugSymbolsInitialized()) {
            return;
        }

        if (exceptionPointers->ContextRecord == nullptr) {
            WriteLifecycleLog("Structured exception context was unavailable.");
            WriteCurrentThreadStackTrace("Fallback current-thread stack trace.", 1);
            return;
        }

        HANDLE processHandle = GetCurrentProcess();
        HANDLE threadHandle = GetCurrentThread();
        CONTEXT contextRecord = *exceptionPointers->ContextRecord;
        STACKFRAME64 stackFrame {};
        DWORD machineType = 0;

#if defined(_M_X64)
        machineType = IMAGE_FILE_MACHINE_AMD64;
        stackFrame.AddrPC.Offset = contextRecord.Rip;
        stackFrame.AddrFrame.Offset = contextRecord.Rbp;
        stackFrame.AddrStack.Offset = contextRecord.Rsp;
#elif defined(_M_IX86)
        machineType = IMAGE_FILE_MACHINE_I386;
        stackFrame.AddrPC.Offset = contextRecord.Eip;
        stackFrame.AddrFrame.Offset = contextRecord.Ebp;
        stackFrame.AddrStack.Offset = contextRecord.Esp;
#elif defined(_M_ARM64)
        machineType = IMAGE_FILE_MACHINE_ARM64;
        stackFrame.AddrPC.Offset = contextRecord.Pc;
        stackFrame.AddrFrame.Offset = contextRecord.Fp;
        stackFrame.AddrStack.Offset = contextRecord.Sp;
#else
        WriteLifecycleLog("Structured exception stack walking is unsupported for this architecture.");
        WriteCurrentThreadStackTrace("Fallback current-thread stack trace.", 1);
        return;
#endif

        stackFrame.AddrPC.Mode = AddrModeFlat;
        stackFrame.AddrFrame.Mode = AddrModeFlat;
        stackFrame.AddrStack.Mode = AddrModeFlat;

        bool wroteAnyFrame = false;
        for (std::uint32_t frameIndex = 0; frameIndex < StackTraceFrameCapacity; frameIndex++) {
            BOOL advanced = StackWalk64(
                machineType,
                processHandle,
                threadHandle,
                &stackFrame,
                &contextRecord,
                nullptr,
                SymFunctionTableAccess64,
                SymGetModuleBase64,
                nullptr);
            if (advanced == FALSE || stackFrame.AddrPC.Offset == 0) {
                break;
            }

            wroteAnyFrame = true;
            WriteResolvedStackFrame(frameIndex, static_cast<std::uint64_t>(stackFrame.AddrPC.Offset));
        }

        if (!wroteAnyFrame) {
            WriteLifecycleLog("Structured exception stack walk returned no frames.");
        }
    }

    /// Receives top-level Windows structured exceptions and forwards them into the lifecycle log.
    LONG WINAPI Win32Application::HandleUnhandledStructuredException(EXCEPTION_POINTERS* exceptionPointers) {
        if (ActiveCrashLoggingApplication != nullptr) {
            ActiveCrashLoggingApplication->WriteStructuredExceptionStackTrace(
                "Unhandled structured exception captured by Windows host.",
                exceptionPointers);
        }

        if (PreviousUnhandledExceptionFilter != nullptr) {
            return PreviousUnhandledExceptionFilter(exceptionPointers);
        }

        return EXCEPTION_EXECUTE_HANDLER;
    }

    /// Receives `std::terminate` callbacks and writes a stack trace before chaining to the previous handler.
    void Win32Application::HandleTerminate() {
        if (ActiveCrashLoggingApplication != nullptr) {
            ActiveCrashLoggingApplication->WriteLifecycleLog("std::terminate was invoked.");
            ActiveCrashLoggingApplication->WriteCurrentThreadStackTrace("Terminate stack trace.", 1);
        }

        std::terminate_handler previousHandler = PreviousTerminateHandler;
        PreviousTerminateHandler = nullptr;
        if (previousHandler != nullptr) {
            previousHandler();
            return;
        }

        std::abort();
    }

    /// Receives CRT invalid-parameter failures and writes the associated diagnostics before chaining.
    void Win32Application::HandleInvalidParameter(
        const wchar_t* expression,
        const wchar_t* functionName,
        const wchar_t* fileName,
        unsigned int lineNumber,
        uintptr_t reserved) {
        (void)reserved;

        if (ActiveCrashLoggingApplication != nullptr) {
            std::ostringstream messageBuilder;
            messageBuilder << "CRT invalid parameter";
            if (expression != nullptr) {
                std::wstring expressionText(expression);
                messageBuilder << " expression=\"" << std::string(expressionText.begin(), expressionText.end()) << "\"";
            }
            if (functionName != nullptr) {
                std::wstring functionNameText(functionName);
                messageBuilder << " function=\"" << std::string(functionNameText.begin(), functionNameText.end()) << "\"";
            }
            if (fileName != nullptr) {
                std::wstring fileNameText(fileName);
                messageBuilder << " file=\"" << std::string(fileNameText.begin(), fileNameText.end()) << "\"";
            }
            if (lineNumber != 0) {
                messageBuilder << " line=" << lineNumber;
            }
            ActiveCrashLoggingApplication->WriteLifecycleLog(messageBuilder.str().c_str());
            ActiveCrashLoggingApplication->WriteCurrentThreadStackTrace("Invalid-parameter stack trace.", 1);
        }

        _invalid_parameter_handler previousHandler = PreviousInvalidParameterHandler;
        PreviousInvalidParameterHandler = nullptr;
        if (previousHandler != nullptr) {
            previousHandler(expression, functionName, fileName, lineNumber, reserved);
            return;
        }

        _invoke_watson(expression, functionName, fileName, lineNumber, reserved);
    }

    /// Receives pure-virtual-call failures and writes a stack trace before chaining.
    void Win32Application::HandlePureVirtualCall() {
        if (ActiveCrashLoggingApplication != nullptr) {
            ActiveCrashLoggingApplication->WriteLifecycleLog("Pure virtual function call detected.");
            ActiveCrashLoggingApplication->WriteCurrentThreadStackTrace("Pure-virtual-call stack trace.", 1);
        }

        _purecall_handler previousHandler = PreviousPureCallHandler;
        PreviousPureCallHandler = nullptr;
        if (previousHandler != nullptr) {
            previousHandler();
            return;
        }

        std::abort();
    }

    /// Receives `SIGABRT` notifications and writes a stack trace before re-raising the signal.
    void Win32Application::HandleAbortSignal(int signalValue) {
        if (ActiveCrashLoggingApplication != nullptr) {
            std::ostringstream messageBuilder;
            messageBuilder << "SIGABRT received with signal value " << signalValue << '.';
            ActiveCrashLoggingApplication->WriteLifecycleLog(messageBuilder.str().c_str());
            ActiveCrashLoggingApplication->WriteCurrentThreadStackTrace("SIGABRT stack trace.", 1);
        }

        void (*previousHandler)(int) = PreviousAbortSignalHandler;
        PreviousAbortSignalHandler = SIG_DFL;
        std::signal(SIGABRT, SIG_DFL);
        if (previousHandler != nullptr && previousHandler != SIG_DFL && previousHandler != SIG_IGN) {
            previousHandler(signalValue);
            return;
        }

        std::raise(SIGABRT);
    }

    /// Receives CRT debug-report text and mirrors it into the lifecycle log.
    int __cdecl Win32Application::HandleDebugReport(int reportType, char* message, int* returnValue) {
        (void)returnValue;

        if (ActiveCrashLoggingApplication != nullptr && message != nullptr) {
            std::ostringstream messageBuilder;
            messageBuilder << "CRT report type " << reportType << ": " << message;
            ActiveCrashLoggingApplication->WriteLifecycleLog(messageBuilder.str().c_str());
            if (reportType == _CRT_ASSERT || reportType == _CRT_ERROR) {
                ActiveCrashLoggingApplication->WriteCurrentThreadStackTrace("CRT report stack trace.", 1);
            }
        }

        return FALSE;
    }
#endif

    /// Builds the runtime platform metadata stamped into the packaged player.
    PlatformInfo* Win32Application::BuildRuntimePlatformInfo() {
#if __has_include("Core.hpp")
        const char* platformName = he_get_runtime_platform_name();
        if (platformName == nullptr || platformName[0] == '\0') {
            throw std::runtime_error("Packaged runtime platform name was not embedded into this build.");
        }

        const char* platformVersion = he_get_runtime_platform_version();
        if (platformVersion == nullptr || platformVersion[0] == '\0') {
            throw std::runtime_error("Packaged runtime platform version was not embedded into this build.");
        }

        {
            std::ostringstream messageBuilder;
            messageBuilder << "Runtime platform info resolved to '" << platformName << "' version '" << platformVersion << "'.";
            std::string message = messageBuilder.str();
            WriteLifecycleLog(message.c_str());
        }

        return new PlatformInfo(std::string(platformName), std::string(platformVersion));
#else
        throw std::runtime_error("Generated engine core is not included in this Windows build.");
#endif
    }

    /// Loads the packaged startup scene from the built content root when one is present.
    void Win32Application::LoadPackagedStartupScene() {
#if __has_include("Core.hpp")
        if (EngineCore == nullptr) {
            throw std::runtime_error("Windows startup scene loading requires an initialized engine core.");
        }

        CoreInitializationOptions* initializationOptions = EngineCore->get_InitializationOptions();
        if (initializationOptions == nullptr || initializationOptions->get_SceneCatalog() == nullptr) {
            throw std::runtime_error("Windows startup requires a runtime scene catalog.");
        }

        RuntimeSceneCatalog* runtimeSceneCatalog = initializationOptions->get_SceneCatalog();
        Array<RuntimeSceneCatalogEntry*>* catalogEntries = runtimeSceneCatalog->get_Entries();
        if (catalogEntries == nullptr || catalogEntries->get_Length() == 0) {
            throw std::runtime_error("Windows startup requires at least one runtime scene catalog entry.");
        }

        RuntimeSceneCatalogEntry* startupEntry = (*catalogEntries)[0];
        if (startupEntry == nullptr || startupEntry->get_SceneId().empty()) {
            throw std::runtime_error("Windows startup requires the first runtime scene catalog entry to define a scene id.");
        }

        {
            std::ostringstream messageBuilder;
            messageBuilder << "Loading startup scene from runtime scene catalog entry '" << startupEntry->get_SceneId() << "'.";
            std::string message = messageBuilder.str();
            WriteLifecycleLog(message.c_str());
        }

        if (EngineCore->get_SceneManager() == nullptr) {
            throw std::runtime_error("Windows startup scene loading requires an initialized scene manager.");
        }

        EngineCore->get_SceneManager()->LoadScene(startupEntry->get_SceneId(), SceneLoadMode::Single);
        WriteLifecycleLog("Packaged startup scene applied through scene manager.");
#endif
    }

    /// Resolves the runtime player profile that controls initial window sizing.
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

    /// Builds the runtime scene catalog consumed by packaged menu scene transitions.
    RuntimeSceneCatalog* Win32Application::BuildRuntimeSceneCatalog() {
#if __has_include("Core.hpp")
        std::size_t entryCount = 0;
        const HERuntimeSceneCatalogEntry* manifestEntries = he_runtime_scene_catalog_entries(&entryCount);
        if (manifestEntries == nullptr || entryCount == 0) {
            WriteLifecycleLog("No runtime scene catalog entries were embedded into this build.");
            return nullptr;
        }

        Array<RuntimeSceneCatalogEntry*>* catalogEntries = new Array<RuntimeSceneCatalogEntry*>(static_cast<int32_t>(entryCount));
        for (std::size_t index = 0; index < entryCount; index++) {
            const HERuntimeSceneCatalogEntry& manifestEntry = manifestEntries[index];
            if (manifestEntry.SceneId == nullptr || manifestEntry.SceneId[0] == '\0') {
                throw std::runtime_error("Runtime scene catalog entries must define a scene id.");
            }
            if (manifestEntry.CookedRelativePath == nullptr || manifestEntry.CookedRelativePath[0] == '\0') {
                throw std::runtime_error("Runtime scene catalog entries must define a cooked relative path.");
            }

            (*catalogEntries)[static_cast<int32_t>(index)] = new RuntimeSceneCatalogEntry(
                std::string(manifestEntry.SceneId),
                std::string(manifestEntry.CookedRelativePath));
        }

        {
            std::ostringstream messageBuilder;
            messageBuilder << "Runtime scene catalog initialized with " << entryCount << " scene entries.";
            std::string message = messageBuilder.str();
            WriteLifecycleLog(message.c_str());
        }

        return new RuntimeSceneCatalog(catalogEntries);
#else
        return nullptr;
#endif
    }

    /// Samples the current renderer cache counters exposed by the Windows bridge.
    RuntimeRenderCounters Win32Application::BuildRenderCounters() const {
        RuntimeRenderCounters counters;
        if (EngineRenderManager2D != nullptr) {
            counters.TextureResourceCount = EngineRenderManager2D->GetTextureResourceCount();
            counters.EngineOwnedTextureResourceCount = EngineRenderManager2D->GetEngineOwnedTextureResourceCount();
            counters.SceneOwnedTextureResourceCount = counters.TextureResourceCount >= counters.EngineOwnedTextureResourceCount
                ? counters.TextureResourceCount - counters.EngineOwnedTextureResourceCount
                : 0;
        } else if (EngineRenderManager3D != nullptr) {
            counters.TextureResourceCount = EngineRenderManager3D->GetTextureResourceCount();
            counters.SceneOwnedTextureResourceCount = counters.TextureResourceCount;
        }

        if (EngineRenderManager3D != nullptr) {
            counters.MaterialShaderResourceCount = EngineRenderManager3D->GetMaterialShaderResourceCount();
            counters.MaterialConstantBufferCount = EngineRenderManager3D->GetMaterialConstantBufferCount();
            counters.ModelBufferCount = EngineRenderManager3D->GetModelBufferCount();
            counters.ModelVertexBufferBytes = EngineRenderManager3D->GetModelVertexBufferBytes();
            counters.ModelIndexBufferBytes = EngineRenderManager3D->GetModelIndexBufferBytes();
            counters.MaterialConstantBufferBytes = EngineRenderManager3D->GetMaterialConstantBufferBytes();
        }

        return counters;
    }

    /// Captures the current runtime memory snapshot using shared diagnostics when available.
    RuntimeMemorySnapshot Win32Application::CaptureRuntimeMemorySnapshot(std::string* detailMetrics) const {
        if (detailMetrics == nullptr) {
            return RuntimeMemorySnapshot::Capture();
        }

#if defined(HELENGINE_WINDOWS_DEBUG_RUNTIME_DIAGNOSTICS) && __has_include("Core.hpp") && __has_include("RuntimeMemoryDiagnosticsSnapshot.hpp")
        if (EngineCore != nullptr && EngineCore->get_RuntimeDiagnosticsService() != nullptr) {
            RuntimeMemoryDiagnosticsSnapshot* sharedSnapshot = EngineCore->get_RuntimeDiagnosticsService()->CaptureSnapshot();
            if (sharedSnapshot != nullptr) {
                RuntimeMemorySnapshot nativeSnapshot;
                nativeSnapshot.WorkingSetBytes = sharedSnapshot->get_ResidentBytes();
                nativeSnapshot.PeakWorkingSetBytes = sharedSnapshot->get_PeakResidentBytes();
                nativeSnapshot.PrivateUsageBytes = sharedSnapshot->get_CommittedBytes();
                nativeSnapshot.PagefileUsageBytes = sharedSnapshot->get_CommittedBytes();
                nativeSnapshot.PeakPagefileUsageBytes = sharedSnapshot->get_PeakCommittedBytes();
                nativeSnapshot.AvailablePhysicalBytes = sharedSnapshot->get_AvailablePhysicalBytes();
                nativeSnapshot.PageFaultCount = sharedSnapshot->get_PageFaultCount();

                List<RuntimeDiagnosticsMetric*>* snapshotDetailMetrics = sharedSnapshot->get_DetailMetrics();
                if (snapshotDetailMetrics != nullptr) {
                    std::ostringstream detailMetricsBuilder;
                    for (int32_t index = 0; index < snapshotDetailMetrics->get_Count(); index++) {
                        RuntimeDiagnosticsMetric* detailMetric = (*snapshotDetailMetrics)[index];
                        if (detailMetric == nullptr) {
                            continue;
                        }

                        const std::string& metricName = detailMetric->get_Name();
                        std::uint64_t metricValue = detailMetric->get_Value();
                        if (metricName == "pagefile_usage_bytes") {
                            nativeSnapshot.PagefileUsageBytes = metricValue;
                        } else if (metricName == "quota_paged_pool_bytes") {
                            nativeSnapshot.QuotaPagedPoolBytes = metricValue;
                        } else if (metricName == "quota_nonpaged_pool_bytes") {
                            nativeSnapshot.QuotaNonPagedPoolBytes = metricValue;
                        } else if (metricName == "system_commit_total_bytes") {
                            nativeSnapshot.SystemCommitTotalBytes = metricValue;
                        } else if (metricName == "system_commit_limit_bytes") {
                            nativeSnapshot.SystemCommitLimitBytes = metricValue;
                        } else if (detailMetrics != nullptr && !metricName.empty()) {
                            detailMetricsBuilder
                                << " "
                                << metricName
                                << "="
                                << metricValue;
                        }
                    }

                    if (detailMetrics != nullptr) {
                        *detailMetrics = detailMetricsBuilder.str();
                    }
                }

                DeleteRuntimeMemoryDiagnosticsSnapshot(sharedSnapshot);
                return nativeSnapshot;
            }
        }
#endif

        return RuntimeMemorySnapshot::Capture();
    }

    /// Builds the currently tracked loaded scene id list from shared runtime state.
    std::string Win32Application::BuildTrackedLoadedSceneIds() const {
#if __has_include("Core.hpp")
        if (EngineCore == nullptr || EngineCore->get_SceneManager() == nullptr) {
            return std::string();
        }

        List<std::string>* sceneIds = EngineCore->get_SceneManager()->GetLoadedSceneIds();
        if (sceneIds == nullptr || sceneIds->get_Count() == 0) {
            return std::string();
        }

        std::ostringstream builder;
        for (int32_t index = 0; index < sceneIds->get_Count(); index++) {
            if (index > 0) {
                builder << ",";
            }

            builder << (*sceneIds)[index];
        }

        return builder.str();
#else
        return std::string();
#endif
    }

    /// Writes one named scene diagnostics checkpoint into the Windows diagnostics log.
    void Win32Application::WriteSceneDiagnosticsCheckpoint(const char* label) {
#if __has_include("Core.hpp")
        if (!EnableSceneDiagnosticsCheckpointLogging) {
            return;
        }

        std::string detailMetrics;
        RuntimeMemorySnapshot memorySnapshot = CaptureRuntimeMemorySnapshot(&detailMetrics);
        RuntimeRenderCounters renderCounters = BuildRenderCounters();
#if defined(HELENGINE_WINDOWS_DEBUG_RUNTIME_DIAGNOSTICS)
        if (EnableDebugAllocationTracking) {
            if (!DebugAllocationBaselineCaptured) {
                CaptureDebugAllocationBaseline();
            } else {
                _CrtMemState currentState {};
                _CrtMemState deltaState {};
                _CrtMemCheckpoint(&currentState);
                if (_CrtMemDifference(&deltaState, &DebugAllocationBaselineState, &currentState) != FALSE) {
                    std::ostringstream allocationMetricsBuilder;
                    allocationMetricsBuilder
                        << " debug_normal_blocks_delta=" << deltaState.lCounts[_NORMAL_BLOCK]
                        << " debug_normal_bytes_delta=" << deltaState.lSizes[_NORMAL_BLOCK]
                        << " debug_crt_blocks_delta=" << deltaState.lCounts[_CRT_BLOCK]
                        << " debug_crt_bytes_delta=" << deltaState.lSizes[_CRT_BLOCK];
                    detailMetrics += allocationMetricsBuilder.str();
                }
            }
        }
#endif

        std::string coreStage;
        std::string sceneManagerStage;
        std::string sceneManagerSceneId;
        int loadedSceneCount = -1;
        int pendingOperationCount = -1;
        std::string sceneLoadStage;
        int rootEntityIndex = -1;
        int entityDepth = -1;
        std::string componentTypeId;
        std::string textFontRelativePath;
        std::string textFontLoadStage;
        std::string fontDeserializeStage;

        if (EngineCore != nullptr) {
            coreStage = EngineCore->get_LastSceneTransitionStage();
            SceneManager* sceneManager = EngineCore->get_SceneManager();
            if (sceneManager != nullptr) {
                sceneManagerStage = sceneManager->get_LastTraceStage();
                sceneManagerSceneId = sceneManager->get_LastTraceSceneId();
                loadedSceneCount = sceneManager->get_LastTraceLoadedSceneCount();
                pendingOperationCount = sceneManager->get_LastTracePendingOperationCount();
            }

            RuntimeSceneLoadService* sceneLoadService = EngineCore->get_SceneLoadService();
            if (sceneLoadService != nullptr) {
                sceneLoadStage = sceneLoadService->get_LastTraceStage();
                rootEntityIndex = sceneLoadService->get_LastTraceRootEntityIndex();
                entityDepth = sceneLoadService->get_LastTraceEntityDepth();
                componentTypeId = sceneLoadService->get_LastTraceComponentTypeId();
            }

#if __has_include("RuntimeSceneAssetReferenceResolver.hpp")
            RuntimeSceneAssetReferenceResolver* referenceResolver = EngineCore->get_SceneAssetReferenceResolver();
            if (referenceResolver != nullptr) {
                textFontRelativePath = referenceResolver->get_LastTextFontRelativePath();
                textFontLoadStage = referenceResolver->get_LastTextLoadStage();
            }
#endif
#if __has_include("FontAssetBinarySerializer.hpp")
            fontDeserializeStage = FontAssetBinarySerializer::get_LastDeserializeStage();
#endif
        }

        RuntimeRenderDiagnostics::WriteSceneCheckpoint(
            label != nullptr ? std::string(label) : std::string(),
            memorySnapshot,
            renderCounters,
            detailMetrics,
            coreStage,
            BuildTrackedLoadedSceneIds(),
            sceneManagerStage,
            sceneManagerSceneId,
            loadedSceneCount,
            pendingOperationCount,
            sceneLoadStage,
            rootEntityIndex,
            entityDepth,
            componentTypeId,
            textFontRelativePath,
            textFontLoadStage,
            fontDeserializeStage);
#else
        (void)label;
#endif
    }

    /// Polls generated core scene-transition trace fields and emits checkpoints when they change.
    void Win32Application::PollSceneTransitionDiagnostics() {
#if __has_include("Core.hpp")
        if (!EnableSceneDiagnosticsCheckpointLogging) {
            return;
        }

        if (!EngineInitialized || EngineCore == nullptr) {
            return;
        }

        std::string coreStage = EngineCore->get_LastSceneTransitionStage();
        std::string sceneManagerStage;
        std::string sceneManagerSceneId;
        int loadedSceneCount = -1;
        int pendingOperationCount = -1;
        std::string sceneLoadStage;
        std::string componentTypeId;
        int rootEntityIndex = -1;
        int entityDepth = -1;

        SceneManager* sceneManager = EngineCore->get_SceneManager();
        if (sceneManager != nullptr) {
            sceneManagerStage = sceneManager->get_LastTraceStage();
            sceneManagerSceneId = sceneManager->get_LastTraceSceneId();
            loadedSceneCount = sceneManager->get_LastTraceLoadedSceneCount();
            pendingOperationCount = sceneManager->get_LastTracePendingOperationCount();
        }

        RuntimeSceneLoadService* sceneLoadService = EngineCore->get_SceneLoadService();
        if (sceneLoadService != nullptr) {
            sceneLoadStage = sceneLoadService->get_LastTraceStage();
            componentTypeId = sceneLoadService->get_LastTraceComponentTypeId();
            rootEntityIndex = sceneLoadService->get_LastTraceRootEntityIndex();
            entityDepth = sceneLoadService->get_LastTraceEntityDepth();
        }

        bool hasChanged = coreStage != LastObservedCoreSceneTransitionStage
            || sceneManagerStage != LastObservedSceneManagerTraceStage
            || sceneManagerSceneId != LastObservedSceneManagerSceneId
            || loadedSceneCount != LastObservedLoadedSceneCount
            || pendingOperationCount != LastObservedPendingOperationCount
            || sceneLoadStage != LastObservedSceneLoadStage
            || componentTypeId != LastObservedSceneLoadComponentTypeId
            || rootEntityIndex != LastObservedSceneLoadRootEntityIndex
            || entityDepth != LastObservedSceneLoadEntityDepth;

        if (hasChanged) {
            LastObservedCoreSceneTransitionStage = coreStage;
            LastObservedSceneManagerTraceStage = sceneManagerStage;
            LastObservedSceneManagerSceneId = sceneManagerSceneId;
            LastObservedLoadedSceneCount = loadedSceneCount;
            LastObservedPendingOperationCount = pendingOperationCount;
            LastObservedSceneLoadStage = sceneLoadStage;
            LastObservedSceneLoadComponentTypeId = componentTypeId;
            LastObservedSceneLoadRootEntityIndex = rootEntityIndex;
            LastObservedSceneLoadEntityDepth = entityDepth;
            FramesSinceSceneTraceChange = 0;
            PendingSteadyStateCheckpoint = true;
            WriteSceneDiagnosticsCheckpoint("scene_trace_change");
            return;
        }

        if (!PendingSteadyStateCheckpoint) {
            return;
        }

        FramesSinceSceneTraceChange++;
        if (FramesSinceSceneTraceChange >= 120) {
            PendingSteadyStateCheckpoint = false;
            WriteSceneDiagnosticsCheckpoint("scene_steady_state");
        }
#endif
    }

    /// Loads one packaged serialized asset from a build-relative path.
    Asset* Win32Application::LoadPackagedAsset(const std::string& relativePath) {
#if __has_include("Core.hpp")
        std::filesystem::path fullPath = ResolveApplicationDirectoryPath() / relativePath;
        {
            std::ostringstream messageBuilder;
            messageBuilder << "Loading packaged asset '" << fullPath.string() << "'.";
            std::string message = messageBuilder.str();
            WriteLifecycleLog(message.c_str());
        }
        if (!std::filesystem::exists(fullPath)) {
            throw std::runtime_error(std::string("Required packaged asset was not found: ") + fullPath.string());
        }

        RuntimeRenderDiagnostics::RecordPackagedAssetLoad(relativePath, fullPath.string());
        FileStream* stream = File::OpenRead(fullPath.string());
        WriteLifecycleLog("Packaged asset file opened.");
        WriteLifecycleLog("Deserializing packaged asset.");
        Asset* asset = AssetSerializer::Deserialize(stream);
        WriteLifecycleLog("Packaged asset deserialized.");
        WriteLifecycleLog("Packaged asset load completed.");
        return asset;
#else
        (void)relativePath;
        throw std::runtime_error("Generated engine core is not included in this Windows build.");
#endif
    }

    /// Resolves the current executable directory used as the packaged content root.
    std::filesystem::path Win32Application::ResolveApplicationDirectoryPath() const {
        wchar_t buffer[MAX_PATH];
        DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
        if (length == 0) {
            throw std::runtime_error("Failed to resolve the current executable path.");
        }

        return std::filesystem::path(buffer).parent_path();
    }

    /// Resolves the lifecycle log file path beside the executable.
    std::filesystem::path Win32Application::ResolveLogFilePath() const {
        return ResolveApplicationDirectoryPath() / "helengine_windows.startup.log";
    }

    /// Runs one non-blocking message pump pass.
    bool Win32Application::PumpMessages() {
        MSG message {};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                ExitCode = static_cast<int>(message.wParam);
                return false;
            }

            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        return true;
    }

    /// Renders and presents the current frame.
    void Win32Application::RenderFrame() {
        int clientWidth = MainWindow->GetClientWidth();
        int clientHeight = MainWindow->GetClientHeight();
        if (clientWidth <= 0 || clientHeight <= 0) {
            return;
        }

        if (Bootstrap->GetWidth() != clientWidth || Bootstrap->GetHeight() != clientHeight) {
            Bootstrap->Resize(clientWidth, clientHeight);
#if __has_include("Core.hpp")
            if (EngineInitialized && EngineRenderManager3D != nullptr) {
                EngineRenderManager3D->OnWindowResize(
                    reinterpret_cast<intptr_t>(MainWindow->GetHandle()),
                    clientWidth,
                    clientHeight);
            }
#endif
        }

        if (EngineInitialized && EngineCore != nullptr) {
#if __has_include("Core.hpp")
            EngineCore->Update();
            PollSceneTransitionDiagnostics();
            EngineCore->Draw();
#endif
        }

        Presenter->RenderFrame();
        UpdateFrameStatistics();
    }

    /// Writes one lifecycle message to the host console.
    void Win32Application::WriteLifecycleLog(const char* message) const {
        std::cout << "[Host] " << message << std::endl;
        RuntimeRenderDiagnostics::WriteHostEvent("lifecycle", message != nullptr ? std::string(message) : std::string());
        if (LifecycleLogFile.is_open()) {
            LifecycleLogFile << "[Host] " << message << '\n';
            LifecycleLogFile.flush();
        }
    }

    /// Updates and emits periodic frame statistics for the host loop.
    void Win32Application::UpdateFrameStatistics() {
        FramesSinceLastStatisticLog++;

        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed = now - FrameStatisticStartTime;
        if (elapsed.count() < 5.0) {
            return;
        }

        double seconds = elapsed.count();
        double fps = static_cast<double>(FramesSinceLastStatisticLog) / seconds;
        double averageFrameTimeMs = (seconds * 1000.0) / static_cast<double>(FramesSinceLastStatisticLog);

        std::ostringstream messageBuilder;
        messageBuilder << std::fixed << std::setprecision(1)
            << "FPS: " << fps
            << " | Avg Frame: ";
        messageBuilder << std::setprecision(2) << averageFrameTimeMs
            << " ms"
            << " | Frames: " << FramesSinceLastStatisticLog;
        if (EnableFrameStatisticRuntimeSampling) {
            try {
                RuntimeMemorySnapshot memorySnapshot = CaptureRuntimeMemorySnapshot();
                RuntimeRenderCounters renderCounters = BuildRenderCounters();
                messageBuilder
                    << " | " << memorySnapshot.ToSummaryString()
                    << " | texture_resources=" << renderCounters.TextureResourceCount
                    << " | scene_owned_texture_resources=" << renderCounters.SceneOwnedTextureResourceCount
                    << " | engine_owned_texture_resources=" << renderCounters.EngineOwnedTextureResourceCount
                    << " | material_shader_resources=" << renderCounters.MaterialShaderResourceCount
                    << " | material_constant_buffers=" << renderCounters.MaterialConstantBufferCount;
            } catch (const std::exception&) {
            }
        }

        std::string message = messageBuilder.str();
        if (EnableFrameStatisticLifecycleLogging) {
            WriteLifecycleLog(message.c_str());
        }

#if defined(HELENGINE_WINDOWS_DEBUG_RUNTIME_DIAGNOSTICS)
        if (EnableDebugAllocationTracking) {
            if (!DebugAllocationBaselineCaptured) {
                CaptureDebugAllocationBaseline();
            } else {
                LogDebugAllocationDelta();
            }
        }
#endif

        FrameStatisticStartTime = now;
        FramesSinceLastStatisticLog = 0;
    }

#if defined(HELENGINE_WINDOWS_DEBUG_RUNTIME_DIAGNOSTICS)
    /// Captures the CRT debug heap state used as the steady-state allocation baseline.
    void Win32Application::CaptureDebugAllocationBaseline() {
        EnsureDebugAllocationCallsiteTrackerInitialized();
        _CrtMemCheckpoint(&DebugAllocationBaselineState);
        if (EnableDebugWin32HeapSampling) {
            DebugWin32HeapBaseline = CaptureDebugWin32HeapSummary();
        } else {
            DebugWin32HeapBaseline = DebugWin32HeapSummary();
        }
        if (EnableDebugWin32HeapSampling && EnableDebugHeapBlockAttribution) {
            DebugWin32HeapBaselineSnapshotCount = CaptureDebugWin32HeapSnapshots(DebugWin32HeapBaselineSnapshots);
        } else {
            DebugWin32HeapBaselineSnapshotCount = 0;
        }
        if (EnableDebugAllocationCallsiteTracking && DebugTrackedAllocationStacks != nullptr) {
            for (std::size_t stackIndex = 0; stackIndex < DebugAllocationTrackedStackCapacity; stackIndex++) {
                DebugTrackedAllocationStack* stackEntry = &DebugTrackedAllocationStacks[stackIndex];
                if (!stackEntry->Occupied) {
                    continue;
                }

                stackEntry->BaselineLiveBytes = stackEntry->LiveBytes;
                stackEntry->BaselineLiveAllocations = stackEntry->LiveAllocations;
            }
        }
        DebugAllocationBaselineCaptured = true;

        char message[1024];
        std::snprintf(
            message,
            sizeof(message),
            "Debug allocation baseline captured: normal_blocks=%ld normal_bytes=%ld crt_blocks=%ld crt_bytes=%ld client_blocks=%ld client_bytes=%ld | heap_count=%llu failed_heap_count=%llu busy_blocks=%llu busy_bytes=%llu region_committed_bytes=%llu region_reserved_bytes=%llu region_uncommitted_bytes=%llu",
            DebugAllocationBaselineState.lCounts[_NORMAL_BLOCK],
            DebugAllocationBaselineState.lSizes[_NORMAL_BLOCK],
            DebugAllocationBaselineState.lCounts[_CRT_BLOCK],
            DebugAllocationBaselineState.lSizes[_CRT_BLOCK],
            DebugAllocationBaselineState.lCounts[_CLIENT_BLOCK],
            DebugAllocationBaselineState.lSizes[_CLIENT_BLOCK],
            static_cast<unsigned long long>(DebugWin32HeapBaseline.HeapCount),
            static_cast<unsigned long long>(DebugWin32HeapBaseline.FailedHeapCount),
            static_cast<unsigned long long>(DebugWin32HeapBaseline.BusyBlockCount),
            static_cast<unsigned long long>(DebugWin32HeapBaseline.BusyBytes),
            static_cast<unsigned long long>(DebugWin32HeapBaseline.RegionCommittedBytes),
            static_cast<unsigned long long>(DebugWin32HeapBaseline.RegionReservedBytes),
            static_cast<unsigned long long>(DebugWin32HeapBaseline.RegionUncommittedBytes));
        WriteDebugAllocationLog(message);
    }

    /// Logs the live CRT debug heap delta relative to the captured steady-state baseline.
    void Win32Application::LogDebugAllocationDelta() {
        _CrtMemState currentState {};
        _CrtMemState deltaState {};
        _CrtMemCheckpoint(&currentState);
        std::array<DebugWin32HeapSnapshot, 32> currentHeapSnapshots {};
        std::uint32_t currentHeapSnapshotCount = 0;
        if (EnableDebugWin32HeapSampling && EnableDebugHeapBlockAttribution) {
            currentHeapSnapshotCount = CaptureDebugWin32HeapSnapshots(currentHeapSnapshots);
        }
        DebugWin32HeapSummary currentHeapSummary = EnableDebugWin32HeapSampling
            ? CaptureDebugWin32HeapSummary()
            : DebugWin32HeapSummary();

        if (_CrtMemDifference(&deltaState, &DebugAllocationBaselineState, &currentState) == FALSE) {
            char message[4096];
            std::size_t writtenLength = static_cast<std::size_t>(std::snprintf(
                message,
                sizeof(message),
                "Debug allocation delta: no CRT heap delta from baseline. | heap_count_delta=%lld failed_heap_count_delta=%lld busy_blocks_delta=%lld busy_bytes_delta=%lld region_committed_bytes_delta=%lld region_reserved_bytes_delta=%lld region_uncommitted_bytes_delta=%lld",
                static_cast<long long>(currentHeapSummary.HeapCount) - static_cast<long long>(DebugWin32HeapBaseline.HeapCount),
                static_cast<long long>(currentHeapSummary.FailedHeapCount) - static_cast<long long>(DebugWin32HeapBaseline.FailedHeapCount),
                static_cast<long long>(currentHeapSummary.BusyBlockCount) - static_cast<long long>(DebugWin32HeapBaseline.BusyBlockCount),
                static_cast<long long>(currentHeapSummary.BusyBytes) - static_cast<long long>(DebugWin32HeapBaseline.BusyBytes),
                static_cast<long long>(currentHeapSummary.RegionCommittedBytes) - static_cast<long long>(DebugWin32HeapBaseline.RegionCommittedBytes),
                static_cast<long long>(currentHeapSummary.RegionReservedBytes) - static_cast<long long>(DebugWin32HeapBaseline.RegionReservedBytes),
                static_cast<long long>(currentHeapSummary.RegionUncommittedBytes) - static_cast<long long>(DebugWin32HeapBaseline.RegionUncommittedBytes)));
            if (EnableDebugHeapBlockAttribution) {
                AppendDebugWin32HeapBlockDelta(message, sizeof(message), writtenLength, currentHeapSnapshots, currentHeapSnapshotCount);
            }
            WriteDebugAllocationLog(message);
            return;
        }

        char message[4096];
        std::size_t writtenLength = static_cast<std::size_t>(std::snprintf(
            message,
            sizeof(message),
            "Debug allocation delta: normal_blocks=%ld normal_bytes=%ld crt_blocks=%ld crt_bytes=%ld client_blocks=%ld client_bytes=%ld free_blocks=%ld free_bytes=%ld | heap_count_delta=%lld failed_heap_count_delta=%lld busy_blocks_delta=%lld busy_bytes_delta=%lld region_committed_bytes_delta=%lld region_reserved_bytes_delta=%lld region_uncommitted_bytes_delta=%lld",
            deltaState.lCounts[_NORMAL_BLOCK],
            deltaState.lSizes[_NORMAL_BLOCK],
            deltaState.lCounts[_CRT_BLOCK],
            deltaState.lSizes[_CRT_BLOCK],
            deltaState.lCounts[_CLIENT_BLOCK],
            deltaState.lSizes[_CLIENT_BLOCK],
            deltaState.lCounts[_FREE_BLOCK],
            deltaState.lSizes[_FREE_BLOCK],
            static_cast<long long>(currentHeapSummary.HeapCount) - static_cast<long long>(DebugWin32HeapBaseline.HeapCount),
            static_cast<long long>(currentHeapSummary.FailedHeapCount) - static_cast<long long>(DebugWin32HeapBaseline.FailedHeapCount),
            static_cast<long long>(currentHeapSummary.BusyBlockCount) - static_cast<long long>(DebugWin32HeapBaseline.BusyBlockCount),
            static_cast<long long>(currentHeapSummary.BusyBytes) - static_cast<long long>(DebugWin32HeapBaseline.BusyBytes),
            static_cast<long long>(currentHeapSummary.RegionCommittedBytes) - static_cast<long long>(DebugWin32HeapBaseline.RegionCommittedBytes),
            static_cast<long long>(currentHeapSummary.RegionReservedBytes) - static_cast<long long>(DebugWin32HeapBaseline.RegionReservedBytes),
            static_cast<long long>(currentHeapSummary.RegionUncommittedBytes) - static_cast<long long>(DebugWin32HeapBaseline.RegionUncommittedBytes)));
        if (EnableDebugHeapBlockAttribution) {
            AppendDebugWin32HeapBlockDelta(message, sizeof(message), writtenLength, currentHeapSnapshots, currentHeapSnapshotCount);
        }
        WriteDebugAllocationLog(message);
        if (EnableDebugAllocationCallsiteTracking &&
            DebugTrackedAllocationStacks != nullptr &&
            EnsureDebugSymbolsInitialized()) {
            struct TopTrackedAllocationStack {
                std::uint32_t StackIndex;
                std::uint64_t DeltaBytes;
                std::uint32_t DeltaAllocations;
            };

            std::array<TopTrackedAllocationStack, 5> topStacks {};
            for (std::size_t stackIndex = 0; stackIndex < DebugAllocationTrackedStackCapacity; stackIndex++) {
                DebugTrackedAllocationStack* stackEntry = &DebugTrackedAllocationStacks[stackIndex];
                if (!stackEntry->Occupied || stackEntry->LiveBytes <= stackEntry->BaselineLiveBytes) {
                    continue;
                }

                std::uint64_t deltaBytes = stackEntry->LiveBytes - stackEntry->BaselineLiveBytes;
                std::uint32_t deltaAllocations = stackEntry->LiveAllocations >= stackEntry->BaselineLiveAllocations
                    ? stackEntry->LiveAllocations - stackEntry->BaselineLiveAllocations
                    : 0;
                for (std::size_t topIndex = 0; topIndex < topStacks.size(); topIndex++) {
                    if (deltaBytes <= topStacks[topIndex].DeltaBytes) {
                        continue;
                    }

                    for (std::size_t shiftIndex = topStacks.size() - 1; shiftIndex > topIndex; shiftIndex--) {
                        topStacks[shiftIndex] = topStacks[shiftIndex - 1];
                    }

                    topStacks[topIndex].StackIndex = static_cast<std::uint32_t>(stackIndex);
                    topStacks[topIndex].DeltaBytes = deltaBytes;
                    topStacks[topIndex].DeltaAllocations = deltaAllocations;
                    break;
                }
            }

            DebugAllocationCallsiteTrackingSuspended = true;
            for (std::size_t topIndex = 0; topIndex < topStacks.size(); topIndex++) {
                if (topStacks[topIndex].DeltaBytes == 0 || topStacks[topIndex].StackIndex >= DebugAllocationTrackedStackCapacity) {
                    continue;
                }

                DebugTrackedAllocationStack* stackEntry = &DebugTrackedAllocationStacks[topStacks[topIndex].StackIndex];
                std::ostringstream stackMessageBuilder;
                stackMessageBuilder
                    << "Debug allocation top stack #" << (topIndex + 1)
                    << ": delta_bytes=" << topStacks[topIndex].DeltaBytes
                    << " delta_allocations=" << topStacks[topIndex].DeltaAllocations
                    << " live_bytes=" << stackEntry->LiveBytes
                    << " live_allocations=" << stackEntry->LiveAllocations;
                if (DebugAllocationCallsiteTrackerOverflowed) {
                    stackMessageBuilder << " tracker_overflowed=true";
                }
                std::string stackMessage = stackMessageBuilder.str();
                WriteDebugAllocationLog(stackMessage.c_str());

                for (USHORT frameIndex = 0; frameIndex < stackEntry->FrameCount; frameIndex++) {
                    WriteResolvedStackFrame(frameIndex, reinterpret_cast<std::uint64_t>(stackEntry->Frames[frameIndex]));
                }
            }
            DebugAllocationCallsiteTrackingSuspended = false;
        }
    }

    /// Captures the current Win32 process-heap summary used by debug allocation diagnostics.
    Win32Application::DebugWin32HeapSummary Win32Application::CaptureDebugWin32HeapSummary() const {
        DebugWin32HeapSummary summary;

        DWORD reportedHeapCount = GetProcessHeaps(0, nullptr);
        if (reportedHeapCount == 0) {
            summary.FailedHeapCount = 1;
            return summary;
        }

        std::array<HANDLE, 32> heaps {};
        if (reportedHeapCount > heaps.size()) {
            summary.FailedHeapCount = 1;
            return summary;
        }

        DWORD actualHeapCount = GetProcessHeaps(reportedHeapCount, heaps.data());
        if (actualHeapCount == 0) {
            summary.FailedHeapCount = 1;
            return summary;
        }

        if (actualHeapCount > reportedHeapCount) {
            summary.FailedHeapCount = 1;
            return summary;
        }

        for (DWORD heapIndex = 0; heapIndex < actualHeapCount; heapIndex++) {
            HANDLE heapHandle = heaps[heapIndex];
            if (heapHandle == nullptr) {
                summary.FailedHeapCount++;
                continue;
            }

            summary.HeapCount++;
            if (HeapLock(heapHandle) == FALSE) {
                summary.FailedHeapCount++;
                continue;
            }

            PROCESS_HEAP_ENTRY entry {};
            SetLastError(ERROR_SUCCESS);
            while (HeapWalk(heapHandle, &entry) != FALSE) {
                if ((entry.wFlags & PROCESS_HEAP_ENTRY_BUSY) != 0) {
                    summary.BusyBlockCount++;
                    summary.BusyBytes += static_cast<std::uint64_t>(entry.cbData);
                } else if ((entry.wFlags & PROCESS_HEAP_REGION) != 0) {
                    summary.RegionCommittedBytes += static_cast<std::uint64_t>(entry.Region.dwCommittedSize);
                    summary.RegionUncommittedBytes += static_cast<std::uint64_t>(entry.Region.dwUnCommittedSize);
                    summary.RegionReservedBytes += static_cast<std::uint64_t>(entry.Region.dwCommittedSize)
                        + static_cast<std::uint64_t>(entry.Region.dwUnCommittedSize);
                }
            }

            DWORD heapWalkError = GetLastError();
            if (heapWalkError != ERROR_NO_MORE_ITEMS && heapWalkError != ERROR_SUCCESS) {
                summary.FailedHeapCount++;
            }

            HeapUnlock(heapHandle);
        }

        return summary;
    }

    /// Captures the current Win32 process-heap snapshots used for block-level attribution.
    std::uint32_t Win32Application::CaptureDebugWin32HeapSnapshots(std::array<DebugWin32HeapSnapshot, 32>& snapshots) const {
        for (std::size_t snapshotIndex = 0; snapshotIndex < snapshots.size(); snapshotIndex++) {
            snapshots[snapshotIndex] = DebugWin32HeapSnapshot();
        }
        DWORD reportedHeapCount = GetProcessHeaps(0, nullptr);
        if (reportedHeapCount == 0) {
            return 0;
        }

        std::array<HANDLE, 32> heaps {};
        if (reportedHeapCount > heaps.size()) {
            return 0;
        }

        DWORD actualHeapCount = GetProcessHeaps(reportedHeapCount, heaps.data());
        if (actualHeapCount == 0) {
            return 0;
        }

        if (actualHeapCount > reportedHeapCount) {
            return 0;
        }

        std::uint32_t capturedSnapshotCount = 0;
        for (DWORD heapIndex = 0; heapIndex < actualHeapCount; heapIndex++) {
            HANDLE heapHandle = heaps[heapIndex];
            if (heapHandle == nullptr) {
                continue;
            }
            if (HeapLock(heapHandle) == FALSE) {
                continue;
            }

            DebugWin32HeapSnapshot& snapshot = snapshots[capturedSnapshotCount];
            snapshot.HeapHandleValue = reinterpret_cast<std::uintptr_t>(heapHandle);

            PROCESS_HEAP_ENTRY entry {};
            while (HeapWalk(heapHandle, &entry) != FALSE) {
                if ((entry.wFlags & PROCESS_HEAP_ENTRY_BUSY) != 0) {
                    snapshot.BusyBlockCount++;
                    snapshot.BusyBytes += static_cast<std::uint64_t>(entry.cbData);
                    if (snapshot.BusyBlockSampleCount < snapshot.BusyBlocks.size()) {
                        DebugWin32HeapBusyBlock& busyBlock = snapshot.BusyBlocks[snapshot.BusyBlockSampleCount];
                        busyBlock.Address = reinterpret_cast<std::uintptr_t>(entry.lpData);
                        busyBlock.Size = static_cast<std::uint64_t>(entry.cbData);
                        snapshot.BusyBlockSampleCount++;
                    } else {
                        snapshot.BusyBlockOverflowCount++;
                    }
                } else if ((entry.wFlags & PROCESS_HEAP_REGION) != 0) {
                    snapshot.RegionCommittedBytes += static_cast<std::uint64_t>(entry.Region.dwCommittedSize);
                    snapshot.RegionUncommittedBytes += static_cast<std::uint64_t>(entry.Region.dwUnCommittedSize);
                    snapshot.RegionReservedBytes += static_cast<std::uint64_t>(entry.Region.dwCommittedSize)
                        + static_cast<std::uint64_t>(entry.Region.dwUnCommittedSize);
                }
            }

            HeapUnlock(heapHandle);
            std::sort(
                snapshot.BusyBlocks.begin(),
                snapshot.BusyBlocks.begin() + snapshot.BusyBlockSampleCount,
                [](const DebugWin32HeapBusyBlock& left, const DebugWin32HeapBusyBlock& right) {
                    return left.Address < right.Address;
                });
            capturedSnapshotCount++;
        }

        std::sort(
            snapshots.begin(),
            snapshots.begin() + capturedSnapshotCount,
            [](const DebugWin32HeapSnapshot& left, const DebugWin32HeapSnapshot& right) {
                return left.HeapHandleValue < right.HeapHandleValue;
            });
        return capturedSnapshotCount;
    }

    /// Appends capped block-level Win32 heap delta attribution to the supplied diagnostics builder.
    void Win32Application::AppendDebugWin32HeapBlockDelta(
        char* buffer,
        std::size_t bufferSize,
        std::size_t& writtenLength,
        const std::array<DebugWin32HeapSnapshot, 32>& currentHeapSnapshots,
        std::uint32_t currentHeapSnapshotCount) const {
        constexpr std::size_t MaximumLoggedNewBlocks = 8;
        std::size_t loggedNewBlocks = 0;

        for (std::size_t currentHeapIndex = 0; currentHeapIndex < currentHeapSnapshotCount; currentHeapIndex++) {
            const DebugWin32HeapSnapshot& currentHeapSnapshot = currentHeapSnapshots[currentHeapIndex];
            const DebugWin32HeapSnapshot* baselineHeapSnapshot = nullptr;
            for (std::size_t baselineHeapIndex = 0; baselineHeapIndex < DebugWin32HeapBaselineSnapshotCount; baselineHeapIndex++) {
                if (DebugWin32HeapBaselineSnapshots[baselineHeapIndex].HeapHandleValue == currentHeapSnapshot.HeapHandleValue) {
                    baselineHeapSnapshot = &DebugWin32HeapBaselineSnapshots[baselineHeapIndex];
                    break;
                }
            }

            const std::int64_t busyBytesDelta = static_cast<std::int64_t>(currentHeapSnapshot.BusyBytes)
                - static_cast<std::int64_t>(baselineHeapSnapshot != nullptr ? baselineHeapSnapshot->BusyBytes : 0);
            const std::int64_t busyBlocksDelta = static_cast<std::int64_t>(currentHeapSnapshot.BusyBlockCount)
                - static_cast<std::int64_t>(baselineHeapSnapshot != nullptr ? baselineHeapSnapshot->BusyBlockCount : 0);
            if (busyBytesDelta <= 0 && busyBlocksDelta <= 0) {
                continue;
            }

            for (std::size_t blockIndex = 0; blockIndex < currentHeapSnapshot.BusyBlockSampleCount; blockIndex++) {
                const DebugWin32HeapBusyBlock& currentBusyBlock = currentHeapSnapshot.BusyBlocks[blockIndex];
                bool existedInBaseline = false;
                if (baselineHeapSnapshot != nullptr) {
                    auto baselineBlockIterator = std::lower_bound(
                        baselineHeapSnapshot->BusyBlocks.begin(),
                        baselineHeapSnapshot->BusyBlocks.begin() + baselineHeapSnapshot->BusyBlockSampleCount,
                        currentBusyBlock.Address,
                        [](const DebugWin32HeapBusyBlock& block, std::uintptr_t address) {
                            return block.Address < address;
                        });
                    existedInBaseline = baselineBlockIterator != (baselineHeapSnapshot->BusyBlocks.begin() + baselineHeapSnapshot->BusyBlockSampleCount)
                        && baselineBlockIterator->Address == currentBusyBlock.Address
                        && baselineBlockIterator->Size == currentBusyBlock.Size;
                }

                if (existedInBaseline) {
                    continue;
                }

                if (writtenLength >= bufferSize) {
                    return;
                }
                int appendedLength = std::snprintf(
                    buffer + writtenLength,
                    bufferSize - writtenLength,
                    " | new_heap_block[%zu]=heap=0x%llx,addr=0x%llx,size=%llu",
                    loggedNewBlocks,
                    static_cast<unsigned long long>(currentHeapSnapshot.HeapHandleValue),
                    static_cast<unsigned long long>(currentBusyBlock.Address),
                    static_cast<unsigned long long>(currentBusyBlock.Size));
                if (appendedLength <= 0) {
                    return;
                }
                writtenLength += static_cast<std::size_t>(appendedLength);
                loggedNewBlocks++;
                if (loggedNewBlocks >= MaximumLoggedNewBlocks) {
                    return;
                }
            }
        }
    }

    /// Writes one allocation-diagnostics line without mirroring it into structured runtime diagnostics.
    void Win32Application::WriteDebugAllocationLog(const char* message) const {
        std::cout << "[Host] " << message << std::endl;
        if (LifecycleLogFile.is_open()) {
            LifecycleLogFile << "[Host] " << message << '\n';
            LifecycleLogFile.flush();
        }
    }
#endif
}

