#pragma once

#include "InputGamepadState.hpp"
#include "runtime/native_list.hpp"

#include "platform/windows/input/windows_gamepad_device_state.hpp"

namespace helengine::windows {
    /// Merges backend gamepad candidates into one deduplicated list using the agreed backend-priority order.
    class WindowsGamepadDedupeService {
    public:
        /// Merges the supplied backend candidates into one stable list of gamepads for the current frame.
        static void MergeAndDedupe(
            const List<WindowsGamepadDeviceState>& candidates,
            List<InputGamepadState>& mergedGamepads);
    };
}
