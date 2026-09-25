#include "platform/windows/win32/win32_window_style.hpp"

namespace helengine::windows {
    /// Creates one style set; private so only Normal() and Overlay() can build instances.
    /// <param name="windowMode">Window mode this style set belongs to.</param>
    /// <param name="style">Win32 window style.</param>
    /// <param name="exStyle">Win32 extended window style.</param>
    /// <param name="showCommand">ShowWindow command used when the window is first shown.</param>
    Win32WindowStyle::Win32WindowStyle(Win32WindowMode windowMode, DWORD style, DWORD exStyle, int showCommand)
        : WindowMode(windowMode)
        , Style(style)
        , ExStyle(exStyle)
        , ShowCommand(showCommand) {
    }

    /// Returns today's ordinary window: WS_OVERLAPPEDWINDOW, no extended style, shown with SW_SHOWDEFAULT. These
    /// are the same values Win32Window's normal-mode path writes out literally, so normal mode stays byte-identical.
    Win32WindowStyle Win32WindowStyle::Normal() {
        return Win32WindowStyle(Win32WindowMode::Normal, WS_OVERLAPPEDWINDOW, 0, SW_SHOWDEFAULT);
    }

    /// Returns the borderless overlay window: WS_POPUP with WS_EX_NOREDIRECTIONBITMAP (DirectComposition supplies
    /// the content), WS_EX_TOPMOST (always on top), WS_EX_TOOLWINDOW (no taskbar button) and WS_EX_LAYERED
    /// (required for the WS_EX_TRANSPARENT click-through toggle), shown with SW_SHOWNOACTIVATE so it never takes
    /// the foreground.
    Win32WindowStyle Win32WindowStyle::Overlay() {
        return Win32WindowStyle(Win32WindowMode::Overlay, WS_POPUP, WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, SW_SHOWNOACTIVATE);
    }

    /// Gets the window mode this style set belongs to, which selects Win32Window's creation and show path.
    Win32WindowMode Win32WindowStyle::GetWindowMode() const {
        return WindowMode;
    }

    /// Gets the Win32 window style passed to CreateWindowExW.
    DWORD Win32WindowStyle::GetStyle() const {
        return Style;
    }

    /// Gets the Win32 extended window style passed to CreateWindowExW.
    DWORD Win32WindowStyle::GetExStyle() const {
        return ExStyle;
    }

    /// Gets the ShowWindow command used when the window is first shown.
    int Win32WindowStyle::GetShowCommand() const {
        return ShowCommand;
    }
}
