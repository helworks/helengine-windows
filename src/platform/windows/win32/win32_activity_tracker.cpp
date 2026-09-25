#include "platform/windows/win32/win32_activity_tracker.hpp"

namespace helengine::windows {
    /// Creates a tracker whose last activity is the moment of creation, so the player starts in active mode.
    Win32ActivityTracker::Win32ActivityTracker()
        : LastActivity(std::chrono::steady_clock::now()) {
    }

    /// Records activity now when the message is a keyboard, mouse or window-state message; ignores all others.
    /// <param name="message">Win32 message identifier received by the window procedure.</param>
    void Win32ActivityTracker::ObserveMessage(UINT message) {
        switch (message) {
            case WM_KEYDOWN:
            case WM_KEYUP:
            case WM_SYSKEYDOWN:
            case WM_SYSKEYUP:
            case WM_CHAR:
            case WM_MOUSEMOVE:
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_LBUTTONDBLCLK:
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
            case WM_RBUTTONDBLCLK:
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
            case WM_MBUTTONDBLCLK:
            case WM_XBUTTONDOWN:
            case WM_XBUTTONUP:
            case WM_XBUTTONDBLCLK:
            case WM_MOUSEWHEEL:
            case WM_MOUSEHWHEEL:
            case WM_SIZE:
            case WM_ACTIVATE:
            case WM_SETFOCUS:
            case WM_KILLFOCUS:
            case WM_PAINT:
            case WM_DISPLAYCHANGE:
            case WM_DPICHANGED:
                MarkActivity();
                return;
        }
    }

    /// Records activity now regardless of any message.
    void Win32ActivityTracker::MarkActivity() {
        LastActivity = std::chrono::steady_clock::now();
    }

    /// Gets the time of the most recent recorded activity.
    std::chrono::steady_clock::time_point Win32ActivityTracker::GetLastActivity() const {
        return LastActivity;
    }
}
