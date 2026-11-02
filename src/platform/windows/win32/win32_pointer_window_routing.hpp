#pragma once
#include <Windows.h>
#include <stdexcept>

namespace helengine::windows {
    /// Restricts passive pointer input to the visible native view actually hit by Windows.
    class Win32PointerWindowRouting {
    public:
        /// Maps an inclusive remote-desktop endpoint to the last client pixel only at an exposed monitor edge.
        static POINT NormalizeMonitorEndpoint(POINT point, const RECT& client, const RECT& monitor, bool onDisplay) {
            if (onDisplay) { return point; }
            const bool bottom = client.bottom == monitor.bottom && point.y == monitor.bottom
                && point.x >= client.left && point.x <= client.right;
            const bool right = client.right == monitor.right && point.x == monitor.right
                && point.y >= client.top && point.y <= client.bottom;
            if (bottom) { --point.y; }
            if (right) { --point.x; }
            return point;
        }

        /// Normalizes only off-display inclusive endpoints; valid positions on another monitor retain their ownership.
        static POINT ResolveScreenPoint(HWND window, POINT point) {
            if (MonitorFromPoint(point, MONITOR_DEFAULTTONULL) != nullptr) { return point; }
            HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONULL);
            if (monitor == nullptr) { return point; }
            MONITORINFO info {}; info.cbSize = sizeof(info);
            RECT client {};
            if (!GetMonitorInfoW(monitor, &info) || !GetClientRect(window, &client)) {
                throw std::runtime_error("Unable to read monitor/client bounds for pointer endpoint mapping.");
            }
            POINT origin {client.left, client.top}, end {client.right, client.bottom};
            if (!ClientToScreen(window, &origin) || !ClientToScreen(window, &end)) {
                throw std::runtime_error("Unable to map pointer client bounds to screen coordinates.");
            }
            client = RECT {origin.x, origin.y, end.x, end.y};
            return NormalizeMonitorEndpoint(point, client, info.rcMonitor, false);
        }

        /// Tests native hit ownership without granting keyboard focus or accepting transparent views.
        static bool IsPointerTarget(HWND candidate, HWND hitWindow) {
            return candidate != nullptr && hitWindow != nullptr && IsWindow(candidate)
                && IsWindowVisible(candidate) && !IsIconic(candidate) && IsWindowEnabled(candidate)
                && (GetWindowLongPtrW(candidate, GWL_EXSTYLE) & WS_EX_TRANSPARENT) == 0
                && GetAncestor(hitWindow, GA_ROOT) == candidate;
        }
    };
}
