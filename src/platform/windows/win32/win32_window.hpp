#pragma once

#include <Windows.h>

#include <string>

namespace helengine::windows {
    /// Owns one native Win32 window and the static-to-instance message bridge.
    class Win32Window {
    public:
        /// Creates a window wrapper with a title and requested client size.
        Win32Window(const wchar_t* title, int width, int height);

        /// Releases the native window if it is still alive.
        ~Win32Window();

        /// Registers the window class and creates the native window.
        void Create();

        /// Shows the native window using the default show mode.
        void Show() const;

        /// Gets the native window handle.
        HWND GetHandle() const;

        /// Gets the current client width in pixels.
        int GetClientWidth() const;

        /// Gets the current client height in pixels.
        int GetClientHeight() const;

    private:
        /// Handles window messages for this instance.
        LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

        /// Registers the native window class used by the player host.
        void RegisterWindowClass();

        /// Updates the cached client size from the current native window state.
        void RefreshClientSize();

        /// Bridges the Win32 callback signature to the stored window instance.
        static LRESULT CALLBACK WindowProcedure(HWND handle, UINT message, WPARAM wParam, LPARAM lParam);

        /// Stores the native window title.
        std::wstring Title;

        /// Stores the requested initial client width.
        int Width;

        /// Stores the requested initial client height.
        int Height;

        /// Stores the native window handle.
        HWND Handle;
    };
}
