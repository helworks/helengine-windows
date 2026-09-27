#include "platform/windows/win32/win32_dpi_window_sizing.hpp"

#include <sstream>
#include <stdexcept>

namespace helengine::windows {
    /// Returns the outer window size whose client area is exactly clientWidth x clientHeight physical pixels at the
    /// given DPI, sizing the non-client frame with AdjustWindowRectExForDpi for that DPI. Throws std::runtime_error
    /// naming the call and GetLastError() when AdjustWindowRectExForDpi fails.
    /// <param name="clientWidth">Wanted client width in physical pixels.</param>
    /// <param name="clientHeight">Wanted client height in physical pixels.</param>
    /// <param name="style">Win32 window style whose frame is added around the client area.</param>
    /// <param name="exStyle">Win32 extended window style whose frame is added around the client area.</param>
    /// <param name="dpi">DPI the frame metrics are computed for.</param>
    /// <returns>The outer window width and height in physical pixels.</returns>
    SIZE Win32DpiWindowSizing::OuterSizeForClient(int clientWidth, int clientHeight, DWORD style, DWORD exStyle, UINT dpi) {
        RECT windowRectangle { 0, 0, clientWidth, clientHeight };
        if (!AdjustWindowRectExForDpi(&windowRectangle, style, FALSE, exStyle, dpi)) {
            DWORD errorCode = GetLastError();
            std::ostringstream messageBuilder;
            messageBuilder << "AdjustWindowRectExForDpi failed for the HelEngine Windows host at " << dpi
                << " dpi with Win32 error " << errorCode << ".";
            throw std::runtime_error(messageBuilder.str());
        }

        SIZE outerSize { windowRectangle.right - windowRectangle.left, windowRectangle.bottom - windowRectangle.top };
        return outerSize;
    }

    /// Returns the window rectangle to apply for a WM_DPICHANGED message: the suggested rectangle's top-left, so the
    /// window lands where Windows proposes, but sized with OuterSizeForClient at the new DPI, so the client area keeps
    /// its physical pixel size instead of being scaled with the DPI. Throws std::runtime_error when OuterSizeForClient
    /// fails.
    /// <param name="suggestedRectangle">Rectangle Windows suggested in the WM_DPICHANGED lParam.</param>
    /// <param name="clientWidth">Client width in physical pixels to keep.</param>
    /// <param name="clientHeight">Client height in physical pixels to keep.</param>
    /// <param name="style">Win32 window style of the window being placed.</param>
    /// <param name="exStyle">Win32 extended window style of the window being placed.</param>
    /// <param name="newDpi">New DPI reported in the WM_DPICHANGED wParam.</param>
    /// <returns>The window rectangle in screen coordinates.</returns>
    RECT Win32DpiWindowSizing::PlacementForDpiChange(const RECT& suggestedRectangle, int clientWidth, int clientHeight, DWORD style, DWORD exStyle, UINT newDpi) {
        SIZE outerSize = OuterSizeForClient(clientWidth, clientHeight, style, exStyle, newDpi);
        RECT placement {
            suggestedRectangle.left,
            suggestedRectangle.top,
            suggestedRectangle.left + outerSize.cx,
            suggestedRectangle.top + outerSize.cy
        };
        return placement;
    }
}
