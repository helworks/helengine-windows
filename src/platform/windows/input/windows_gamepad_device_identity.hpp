#pragma once

#include <cstdint>
#include <limits>

#include "platform/windows/input/windows_gamepad_backend_kind.hpp"

namespace helengine::windows {
    /// Stores the backend-specific identity metadata used for duplicate suppression between Windows gamepad APIs.
    struct WindowsGamepadDeviceIdentity {
        /// Sentinel slot value used when a backend cannot provide a stable emulated gamepad index.
        static constexpr std::uint32_t InvalidSlotIndex = std::numeric_limits<std::uint32_t>::max();

        /// Backend that produced the device snapshot.
        WindowsGamepadBackendKind BackendKind = WindowsGamepadBackendKind::DirectInput;

        /// Stable backend-local identifier for the device when available.
        std::uint64_t BackendDeviceId = 0;

        /// Optional vendor identifier when the backend exposes one.
        std::uint32_t VendorId = 0;

        /// Optional product identifier when the backend exposes one.
        std::uint32_t ProductId = 0;

        /// Stable emulated gamepad slot used for conservative duplicate suppression when available.
        std::uint32_t SlotIndex = InvalidSlotIndex;
    };
}
