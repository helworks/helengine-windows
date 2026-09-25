#pragma once

#include <Windows.h>

#include "platform/windows/win32/win32_window_mode.hpp"

namespace helengine::windows {
    /// Describes how the player's main window is created and shown: the window mode it belongs to, the Win32 style
    /// and extended style passed to CreateWindowExW, and the ShowWindow command. Only the two factory functions create
    /// instances, so a window can only be built with today's normal style set or the overlay style set confirmed by the
    /// overlay spike.
    class Win32WindowStyle {
    public:
        /// Returns today's ordinary window: WS_OVERLAPPEDWINDOW, no extended style, shown with SW_SHOWDEFAULT. These
        /// are the same values Win32Window's normal-mode path writes out literally, so normal mode stays byte-identical.
        static Win32WindowStyle Normal();

        /// Returns the borderless overlay window: WS_POPUP with WS_EX_NOREDIRECTIONBITMAP (DirectComposition supplies
        /// the content), WS_EX_TOPMOST (always on top), WS_EX_TOOLWINDOW (no taskbar button) and WS_EX_LAYERED
        /// (required for the WS_EX_TRANSPARENT click-through toggle), shown with SW_SHOWNOACTIVATE so it never takes
        /// the foreground.
        static Win32WindowStyle Overlay();

        /// Gets the window mode this style set belongs to, which selects Win32Window's creation and show path.
        Win32WindowMode GetWindowMode() const;

        /// Gets the Win32 window style passed to CreateWindowExW.
        DWORD GetStyle() const;

        /// Gets the Win32 extended window style passed to CreateWindowExW.
        DWORD GetExStyle() const;

        /// Gets the ShowWindow command used when the window is first shown.
        int GetShowCommand() const;

    private:
        /// Creates one style set; private so only Normal() and Overlay() can build instances.
        /// <param name="windowMode">Window mode this style set belongs to.</param>
        /// <param name="style">Win32 window style.</param>
        /// <param name="exStyle">Win32 extended window style.</param>
        /// <param name="showCommand">ShowWindow command used when the window is first shown.</param>
        Win32WindowStyle(Win32WindowMode windowMode, DWORD style, DWORD exStyle, int showCommand);

        /// Stores the window mode this style set belongs to.
        Win32WindowMode WindowMode;

        /// Stores the Win32 window style.
        DWORD Style;

        /// Stores the Win32 extended window style.
        DWORD ExStyle;

        /// Stores the ShowWindow command used when the window is first shown.
        int ShowCommand;
    };
}
