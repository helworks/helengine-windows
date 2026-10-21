#pragma once
#include <Windows.h>

namespace helengine::windows {
    /// Restricts passive pointer input to the visible native view actually hit by Windows.
    class Win32PointerWindowRouting {
    public:
        /// Tests native hit ownership without granting keyboard focus or accepting transparent views.
        static bool IsPointerTarget(HWND candidate, HWND hitWindow) {
            return candidate != nullptr && hitWindow != nullptr && IsWindow(candidate)
                && IsWindowVisible(candidate) && !IsIconic(candidate) && IsWindowEnabled(candidate)
                && (GetWindowLongPtrW(candidate, GWL_EXSTYLE) & WS_EX_TRANSPARENT) == 0
                && GetAncestor(hitWindow, GA_ROOT) == candidate;
        }
    };
}
