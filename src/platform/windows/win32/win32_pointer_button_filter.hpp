#pragma once
#include <Windows.h>
#include <cstdint>

namespace helengine::windows {
    /// Prevents a held button from becoming a new press when pointer ownership changes between views.
    class Win32PointerButtonFilter {
    public:
        /// Returns only presses that began while their pointer view was eligible to receive input.
        std::uint32_t Capture(std::uint32_t physicalButtons, bool eligible, HWND pointerWindow) {
            if (!HasSample || !eligible) {
                SuppressedButtons |= physicalButtons;
            } else if (PreviousWindow != pointerWindow) {
                SuppressedButtons |= PreviousPhysicalButtons & physicalButtons;
            }
            SuppressedButtons &= physicalButtons;
            PreviousPhysicalButtons = physicalButtons;
            PreviousWindow = pointerWindow;
            HasSample = true;
            return eligible ? physicalButtons & ~SuppressedButtons : 0;
        }
    private:
        /// Remembers buttons held before a view change so their original gesture cannot transfer.
        std::uint32_t PreviousPhysicalButtons = 0;
        /// Retains rejected buttons until their physical release rather than fabricating another press.
        std::uint32_t SuppressedButtons = 0;
        /// Identifies the pointer view that received the previous physical snapshot.
        HWND PreviousWindow = nullptr;
        /// Distinguishes startup from an observed physical release before the first valid gesture.
        bool HasSample = false;
    };
}
