#include "platform/windows/win32/win32_application.hpp"

#include <Windows.h>

#include "platform/windows/directx11/directx11_bootstrap.hpp"
#include "platform/windows/directx11/directx11_presenter.hpp"
#include "platform/windows/win32/win32_window.hpp"

namespace helengine::windows {
    /// Creates the application runner with no initialized native resources.
    Win32Application::Win32Application()
        : ExitCode(0) {
    }

    /// Releases native bootstrap objects after the application loop finishes.
    Win32Application::~Win32Application() = default;

    /// Boots the Win32 window and DirectX11 loop and returns the process exit code.
    int Win32Application::Run() {
        CreateMainWindow();
        CreateGraphicsBootstrap();

        while (PumpMessages()) {
            RenderFrame();
        }

        return ExitCode;
    }

    /// Creates the main native window for the player host.
    void Win32Application::CreateMainWindow() {
        MainWindow = std::make_unique<Win32Window>(L"HelEngine Windows Host", 1280, 720);
        MainWindow->Create();
        MainWindow->Show();
    }

    /// Creates the DirectX11 device and presentation resources for the main window.
    void Win32Application::CreateGraphicsBootstrap() {
        Bootstrap = std::make_unique<DirectX11Bootstrap>(
            MainWindow->GetHandle(),
            MainWindow->GetClientWidth(),
            MainWindow->GetClientHeight());
        Presenter = std::make_unique<DirectX11Presenter>(*Bootstrap);
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
        Presenter->RenderFrame();
    }
}
