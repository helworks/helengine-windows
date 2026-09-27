#pragma once

#include <Windows.h>

namespace helengine::windows {
    /// Computes the window geometry a Per-Monitor v2 DPI-aware player window needs so that its client area keeps an
    /// exact size in physical pixels at a given DPI. Static methods only: it holds no state and only turns a client
    /// size, a style set and a DPI into an outer window size or placement.
    class Win32DpiWindowSizing {
    public:
        /// Returns the outer window size whose client area is exactly clientWidth x clientHeight physical pixels at the
        /// given DPI, sizing the non-client frame with AdjustWindowRectExForDpi for that DPI. Throws
        /// std::runtime_error naming the call and GetLastError() when AdjustWindowRectExForDpi fails.
        /// <param name="clientWidth">Wanted client width in physical pixels.</param>
        /// <param name="clientHeight">Wanted client height in physical pixels.</param>
        /// <param name="style">Win32 window style whose frame is added around the client area.</param>
        /// <param name="exStyle">Win32 extended window style whose frame is added around the client area.</param>
        /// <param name="dpi">DPI the frame metrics are computed for.</param>
        /// <returns>The outer window width and height in physical pixels.</returns>
        static SIZE OuterSizeForClient(int clientWidth, int clientHeight, DWORD style, DWORD exStyle, UINT dpi);

        /// Returns the window rectangle to apply for a WM_DPICHANGED message: the suggested rectangle's top-left, so
        /// the window lands where Windows proposes, but sized with OuterSizeForClient at the new DPI, so the client
        /// area keeps its physical pixel size instead of being scaled with the DPI. Throws std::runtime_error when
        /// OuterSizeForClient fails.
        /// <param name="suggestedRectangle">Rectangle Windows suggested in the WM_DPICHANGED lParam.</param>
        /// <param name="clientWidth">Client width in physical pixels to keep.</param>
        /// <param name="clientHeight">Client height in physical pixels to keep.</param>
        /// <param name="style">Win32 window style of the window being placed.</param>
        /// <param name="exStyle">Win32 extended window style of the window being placed.</param>
        /// <param name="newDpi">New DPI reported in the WM_DPICHANGED wParam.</param>
        /// <returns>The window rectangle in screen coordinates.</returns>
        static RECT PlacementForDpiChange(const RECT& suggestedRectangle, int clientWidth, int clientHeight, DWORD style, DWORD exStyle, UINT newDpi);
    };
}
