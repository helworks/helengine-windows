#include "platform/windows/win32/win32_application.hpp"

#include <Windows.h>

#ifdef DrawText
#undef DrawText
#endif

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <iostream>
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
#if __has_include("FontAssetBinarySerializer.hpp")
#include "FontAssetBinarySerializer.hpp"
#endif
#if __has_include("RuntimeSceneAssetReferenceResolver.hpp")
#include "RuntimeSceneAssetReferenceResolver.hpp"
#endif
#endif

namespace helengine::windows {
    namespace {
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
          PendingSteadyStateCheckpoint(false) {
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
            delete exception;
            return EXIT_FAILURE;
        } catch (const std::exception& exception) {
            std::ostringstream messageBuilder;
            messageBuilder << "Fatal host/engine exception: " << exception.what();
            std::string message = messageBuilder.str();
            WriteLifecycleLog(message.c_str());
            return EXIT_FAILURE;
        }

        std::ostringstream messageBuilder;
        messageBuilder << "Host shutdown requested with exit code " << ExitCode << '.';
        std::string message = messageBuilder.str();
        WriteLifecycleLog(message.c_str());
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
        std::string detailMetrics;
        RuntimeMemorySnapshot memorySnapshot = CaptureRuntimeMemorySnapshot(&detailMetrics);
        RuntimeRenderCounters renderCounters = BuildRenderCounters();

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

        std::string message = messageBuilder.str();
        WriteLifecycleLog(message.c_str());

        FrameStatisticStartTime = now;
        FramesSinceLastStatisticLog = 0;
    }
}

