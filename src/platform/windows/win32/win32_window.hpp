#pragma once

#include <Windows.h>

#include <exception>
#include <string>

#include "platform/windows/win32/win32_dpi_awareness.hpp"
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
        /// window style. When the process is Per-Monitor v2 aware and the window is a normal window, the window is
        /// then resized so its client area is the requested size in physical pixels at the window's DPI. Any exception a
        /// message handler raised during creation or that correction is rethrown before this returns.
        void Create();

        /// Shows the native window through the normal or overlay path chosen by the window style.
        void Show() const;

        /// Configures secondary lifetime before creation: closing this window does not quit the process.
        void SetSecondaryWindow();

        /// Shows a secondary window at its requested position without stealing foreground focus.
        void ShowSecondary() const;

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

        /// Records the DPI awareness the process applied, which decides whether Create() corrects a normal window's
        /// size for its DPI. It must be called before Create(); the default is Win32DpiAwareness::Unaware, which keeps
        /// today's creation calls unchanged.
        /// <param name="dpiAwareness">DPI awareness the process applied before any window was created.</param>
        void SetDpiAwareness(Win32DpiAwareness dpiAwareness);

        /// Rethrows, and clears, the first exception a message handler raised since the last call. The window
        /// procedure never lets a C++ exception unwind through user32 or kernel callback frames (undefined on x64);
        /// it keeps the exception instead, and the message pump calls this right after each DispatchMessageW so the
        /// failure reaches the application's fatal handler through ordinary C++ frames. Does nothing when no handler
        /// failed.
        void RethrowPendingException();

    private:
        /// Handles window messages for this instance, first reporting each one to the attached activity tracker when
        /// there is one; the message handling and return values do not depend on the tracker. WM_DPICHANGED, which
        /// only a Per-Monitor v2 aware window receives, moves a normal window to the suggested top-left while keeping
        /// its client pixel size, applies the suggested rectangle as given to a maximized window, leaves a minimized
        /// window and an overlay's rectangle unchanged, and returns 0 in every case. WM_GETDPISCALEDSIZE reports that
        /// client-preserving outer size and returns TRUE for a non-maximized, non-minimized Per-Monitor v2 normal
        /// window, and returns FALSE (linear scaling) otherwise.
        LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

        /// Registers the native window class used by the player host.
        void RegisterWindowClass();

        /// Creates today's ordinary WS_OVERLAPPEDWINDOW window at the default placement, with exactly the calls the
        /// player has always made.
        void CreateNormalWindow();

        /// Creates the borderless overlay window at the requested position, sized with AdjustWindowRectEx for the
        /// overlay style set.
        void CreateOverlayWindow();

        /// Resizes a freshly created Per-Monitor v2 normal window so its client area is exactly the requested size in
        /// physical pixels at the window's own DPI: the creation path sizes the frame with AdjustWindowRect, which does
        /// not know the monitor the window landed on. The outer size comes from Win32DpiWindowSizing at the DPI read
        /// from the window; the window is resized only when that size differs from its current outer size, never
        /// moved, and the cached client size is then refreshed. Throws std::runtime_error when the DPI, the window
        /// rectangle or the resize cannot be obtained or applied.
        /// <param name="requestedClientWidth">Client width in physical pixels requested before creation.</param>
        /// <param name="requestedClientHeight">Client height in physical pixels requested before creation.</param>
        void CorrectNormalWindowSizeForDpi(int requestedClientWidth, int requestedClientHeight);

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

        /// Bridges the Win32 callback signature to the stored window instance. Any exception HandleMessage throws is
        /// caught here, kept in PendingException (the first one wins, so the root cause is not overwritten) and the
        /// message returns 0, because a C++ exception must not unwind through the user32 frames that called the
        /// window procedure; RethrowPendingException rethrows it once control is back in ordinary C++ frames.
        static LRESULT CALLBACK WindowProcedure(HWND handle, UINT message, WPARAM wParam, LPARAM lParam);

        /// Stores the native window title.
        std::wstring Title;

        /// Records whether destroying this window should end the process message loop.
        bool QuitOnDestroy;

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

        /// Stores the DPI awareness the process applied; Win32DpiAwareness::Unaware unless SetDpiAwareness opted the
        /// window into Per-Monitor v2 sizing.
        Win32DpiAwareness DpiAwareness;

        /// Stores the first exception a message handler raised inside the window procedure, until
        /// RethrowPendingException rethrows it; null when no handler has failed since the last rethrow.
        std::exception_ptr PendingException;
    };
}
