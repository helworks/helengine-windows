#include "platform/windows/win32/win32_click_through_controller.hpp"

#include <sstream>
#include <stdexcept>

namespace helengine::windows {
    /// Creates a controller for the overlay window, starting in the click-through-off state the window was created
    /// with.
    Win32ClickThroughController::Win32ClickThroughController(HWND windowHandle)
        : WindowHandle(windowHandle),
          ClickThrough(false) {
    }

    /// Applies the requested state: sets or clears WS_EX_TRANSPARENT with SetWindowLongPtrW and makes the change take
    /// effect with SetWindowPos(SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE). Does
    /// nothing when the state is already applied, so a steady state costs no Win32 calls. Throws std::runtime_error
    /// carrying the Win32 error when either call fails.
    void Win32ClickThroughController::Apply(bool clickThrough) {
        if (clickThrough == ClickThrough) {
            return;
        }

        LONG_PTR exStyle = GetWindowLongPtrW(WindowHandle, GWL_EXSTYLE);
        if (clickThrough) {
            exStyle |= WS_EX_TRANSPARENT;
        } else {
            exStyle &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
        }

        // SetWindowLongPtrW returns the previous value, and a failure is reported as 0 with a non-zero last error, so
        // the last error is cleared first to tell a failure from a previous value of 0.
        SetLastError(0);
        if (SetWindowLongPtrW(WindowHandle, GWL_EXSTYLE, exStyle) == 0 && GetLastError() != 0) {
            std::ostringstream messageBuilder;
            messageBuilder << "SetWindowLongPtrW(GWL_EXSTYLE) failed for the overlay click-through toggle with Win32 error " << GetLastError() << ".";
            throw std::runtime_error(messageBuilder.str());
        }
        if (!SetWindowPos(WindowHandle, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)) {
            std::ostringstream messageBuilder;
            messageBuilder << "SetWindowPos failed for the overlay click-through toggle with Win32 error " << GetLastError() << ".";
            throw std::runtime_error(messageBuilder.str());
        }

        ClickThrough = clickThrough;
    }

    /// Gets whether the mouse currently passes through the window.
    bool Win32ClickThroughController::IsClickThrough() const {
        return ClickThrough;
    }
}
