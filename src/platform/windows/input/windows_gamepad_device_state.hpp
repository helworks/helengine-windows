#pragma once

#include "InputGamepadState.hpp"

#include "platform/windows/input/windows_gamepad_device_identity.hpp"

namespace helengine::windows {
    /// Combines backend identity metadata with one normalized gamepad snapshot for the current frame.
    struct WindowsGamepadDeviceState {
        /// Backend-specific identity metadata used during duplicate suppression.
        WindowsGamepadDeviceIdentity Identity;

        /// Normalized gamepad snapshot exposed to the shared input system.
        InputGamepadState Gamepad;
    };
}
