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
#include "platform/windows/win32/win32_input_bridge.hpp"
#include "platform/windows/win32/win32_render_bridge.hpp"
#include "platform/windows/win32/win32_window.hpp"

#if __has_include("Core.hpp")
#include "Asset.hpp"
#include "AssetSerializer.hpp"
#include "Core.hpp"
#include "Logger.hpp"
#include "RenderManager2D.hpp"
#include "RenderManager3D.hpp"
#include "SceneAsset.hpp"
#include "runtime/runtime_startup_manifest.hpp"
#include "runtime/native_exceptions.hpp"
#include "system/io/file.hpp"
#endif

namespace helengine::windows {
    /// Creates the application runner with no initialized native resources.
    Win32Application::Win32Application()
        : ExitCode(0),
          EngineCore(nullptr),
          EngineRenderManager3D(nullptr),
          EngineRenderManager2D(nullptr),
          EngineInputBackend(nullptr),
          EngineInitialized(false),
          FrameStatisticStartTime(std::chrono::steady_clock::now()),
          FramesSinceLastStatisticLog(0) {
    }

    /// Releases native bootstrap objects after the application loop finishes.
    Win32Application::~Win32Application() {
#if __has_include("Core.hpp")
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
        MainWindow = std::make_unique<Win32Window>(L"HelEngine Windows Host", 1280, 720);
        MainWindow->Create();
        MainWindow->Show();
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

        EngineRenderManager3D = new Win32RenderManager3D(*Bootstrap);
        EngineRenderManager2D = new Win32RenderManager2D(*Bootstrap);
        EngineInputBackend = new Win32InputBackend(MainWindow.get());

        EngineRenderManager3D->AddWindow(
            reinterpret_cast<intptr_t>(MainWindow->GetHandle()),
            MainWindow->GetClientWidth(),
            MainWindow->GetClientHeight());

        EngineCore->Initialize(EngineRenderManager3D, EngineRenderManager2D, EngineInputBackend, options);
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
#else
        WriteLifecycleLog("Generated engine core is not included in this build.");
#endif
    }

    /// Loads the packaged startup scene from the built content root when one is present.
    void Win32Application::LoadPackagedStartupScene() {
#if __has_include("Core.hpp")
        const char* startupSceneRelativePath = he_get_runtime_startup_scene_relative_path();
        if (startupSceneRelativePath == nullptr || startupSceneRelativePath[0] == '\0') {
            WriteLifecycleLog("No packaged startup scene was configured.");
            return;
        }

        std::filesystem::path startupScenePath = ResolveApplicationDirectoryPath() / startupSceneRelativePath;
        {
            std::ostringstream messageBuilder;
            messageBuilder << "Startup scene path resolved to '" << startupScenePath.string() << "'.";
            std::string message = messageBuilder.str();
            WriteLifecycleLog(message.c_str());
        }
        if (!std::filesystem::exists(startupScenePath)) {
            WriteLifecycleLog("No packaged startup scene was found.");
            return;
        }

        SceneAsset* startupScene = static_cast<SceneAsset*>(LoadPackagedAsset(startupSceneRelativePath));
        EngineCore->get_SceneLoadService()->Load(startupScene);
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

        FileStream* stream = File::OpenRead(fullPath.string());
        WriteLifecycleLog("Packaged asset file opened.");
        WriteLifecycleLog("Deserializing packaged asset.");
        Asset* asset = AssetSerializer::Deserialize(stream);
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
            EngineCore->Draw();
#endif
        }

        Presenter->RenderFrame();
        UpdateFrameStatistics();
    }

    /// Writes one lifecycle message to the host console.
    void Win32Application::WriteLifecycleLog(const char* message) const {
        std::cout << "[Host] " << message << std::endl;
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

        std::string message = messageBuilder.str();
        WriteLifecycleLog(message.c_str());

        FrameStatisticStartTime = now;
        FramesSinceLastStatisticLog = 0;
    }
}
