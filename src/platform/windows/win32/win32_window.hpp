#pragma once

#include <Windows.h>

#include <string>

namespace helengine::windows {
    class Win32ActivityTracker;

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

        /// Returns and clears the accumulated mouse-wheel delta since the last input poll.
        int ConsumeMouseWheelDelta();

        /// Attaches the activity tracker that observes every message this window receives; the window does not own
        /// it. Only the opt-in idle throttle attaches one, and it must be attached before Create() so the creation
        /// messages are observed too.
        /// <param name="tracker">Tracker to notify about each received message.</param>
        void SetActivityTracker(Win32ActivityTracker* tracker);

    private:
        /// Handles window messages for this instance, first reporting each one to the attached activity tracker when
        /// there is one; the message handling and return values do not depend on the tracker.
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

        /// Accumulates mouse-wheel delta until the input backend consumes it.
        int MouseWheelDelta;

        /// Stores the optional, non-owned activity tracker notified about each message; null unless the idle
        /// throttle is enabled.
        Win32ActivityTracker* ActivityTracker;
    };
}
