#pragma once

#include <string>

#include "runtime/native_list.hpp"

#include "platform/windows/input/windows_gamepad_backend_kind.hpp"
#include "platform/windows/input/windows_gamepad_device_state.hpp"

namespace helengine::windows {
    /// Defines the contract implemented by one Windows-native gamepad backend.
    class IWindowsGamepadBackend {
    public:
        /// Releases backend-owned native resources.
        virtual ~IWindowsGamepadBackend() = default;

        /// Returns which Windows gamepad backend this adapter represents.
        virtual WindowsGamepadBackendKind GetBackendKind() const = 0;

        /// Initializes any native state required before the backend can capture frames.
        virtual bool TryInitialize(std::string& failureMessage) = 0;

        /// Captures the current frame's connected gamepads and appends them to the shared candidate list.
        virtual void CaptureFrame(List<WindowsGamepadDeviceState>& devices) = 0;
    };
}
