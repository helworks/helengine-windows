#pragma once

#include <memory>
#include <vector>

#include "InputGamepadState.hpp"
#include "runtime/native_list.hpp"

#include "platform/windows/input/i_windows_gamepad_backend.hpp"
#include "platform/windows/win32/win32_window.hpp"

namespace helengine::windows {
    /// Coordinates the enabled Windows gamepad backends and merges them into one shared frame snapshot.
    class WindowsGamepadBackendMux {
    public:
        /// Creates one gamepad backend multiplexer bound to the host window.
        explicit WindowsGamepadBackendMux(Win32Window* window);

        /// Captures one merged and deduplicated gamepad frame.
        void CaptureFrame(List<InputGamepadState>& mergedGamepads);

    private:
        /// Initializes the enabled backend set the first time gamepad capture is requested.
        void EnsureInitialized();

        /// Adds one backend when it initialized successfully and logs failures otherwise.
        void TryAddBackend(std::unique_ptr<IWindowsGamepadBackend> backend);

        /// Stores the host window used by backends that require HWND access.
        Win32Window* Window;

        /// Stores the enabled backend list.
        std::vector<std::unique_ptr<IWindowsGamepadBackend>> Backends;

        /// Tracks whether the backend list has already been initialized.
        bool Initialized;
    };
}
