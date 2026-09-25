#pragma once

#include <Windows.h>

#include <string>

#include "platform/windows/win32/win32_window_style.hpp"

namespace helengine::windows {
    class Win32ActivityTracker;

    /// Owns one native Win32 window and the static-to-instance message bridge.
    class Win32Window {
    public:
        /// Creates a window wrapper with a title, a requested position and client size, and the style set that decides
        /// how the window is created and shown.
        /// <param name="title">Native window title.</param>
        /// <param name="left">Screen x of the window's top-left corner; only the overlay style uses it, a normal window
        /// keeps the CW_USEDEFAULT placement.</param>
        /// <param name="top">Screen y of the window's top-left corner; only the overlay style uses it.</param>
        /// <param name="width">Requested client width in pixels.</param>
        /// <param name="height">Requested client height in pixels.</param>
        /// <param name="windowStyle">Normal or overlay style set.</param>
        Win32Window(const wchar_t* title, int left, int top, int width, int height, const Win32WindowStyle& windowStyle);

        /// Releases the native window if it is still alive.
        ~Win32Window();

        /// Registers the window class and creates the native window through the normal or overlay path chosen by the
        /// window style.
        void Create();

        /// Shows the native window through the normal or overlay path chosen by the window style.
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

        /// Creates today's ordinary WS_OVERLAPPEDWINDOW window at the default placement, with exactly the calls the
        /// player has always made.
        void CreateNormalWindow();

        /// Creates the borderless overlay window at the requested position, sized with AdjustWindowRectEx for the
        /// overlay style set.
        void CreateOverlayWindow();

        /// Shows today's ordinary window and brings it to the foreground with keyboard focus, exactly as the player
        /// has always done.
        void ShowNormalWindow() const;

        /// Shows the overlay window without activating it and pins it topmost with SWP_NOMOVE | SWP_NOSIZE |
        /// SWP_NOACTIVATE: the bounds were already set when the window was created, so this call only changes the
        /// z-order and can never shrink the client area. It never calls a foreground or focus function, so the user's
        /// foreground window keeps focus. Throws std::runtime_error when SetWindowPos fails.
        void ShowOverlayWindow() const;

        /// Updates the cached client size from the current native window state.
        void RefreshClientSize();

        /// Bridges the Win32 callback signature to the stored window instance.
        static LRESULT CALLBACK WindowProcedure(HWND handle, UINT message, WPARAM wParam, LPARAM lParam);

        /// Stores the native window title.
        std::wstring Title;

        /// Stores the requested screen x of the window's top-left corner; only used by the overlay style.
        int Left;

        /// Stores the requested screen y of the window's top-left corner; only used by the overlay style.
        int Top;

        /// Stores the requested initial client width.
        int Width;

        /// Stores the requested initial client height.
        int Height;

        /// Stores the style set that selects the normal or overlay creation and show path.
        Win32WindowStyle WindowStyle;

        /// Stores the native window handle.
        HWND Handle;

        /// Accumulates mouse-wheel delta until the input backend consumes it.
        int MouseWheelDelta;

        /// Stores the optional, non-owned activity tracker notified about each message; null unless the idle
        /// throttle is enabled.
        Win32ActivityTracker* ActivityTracker;
    };
}
