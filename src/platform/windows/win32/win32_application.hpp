#pragma once

#include <memory>

namespace helengine::windows {
    class DirectX11Bootstrap;
    class DirectX11Presenter;
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

        /// Runs one non-blocking message pump pass.
        bool PumpMessages();

        /// Renders and presents the current frame.
        void RenderFrame();

        /// Stores the main native window instance.
        std::unique_ptr<Win32Window> MainWindow;

        /// Stores the DirectX11 device and swap-chain bootstrap.
        std::unique_ptr<DirectX11Bootstrap> Bootstrap;

        /// Stores the clear/present helper for the bootstrap resources.
        std::unique_ptr<DirectX11Presenter> Presenter;

        /// Stores the process exit code requested by the Windows message loop.
        int ExitCode;
    };
}
