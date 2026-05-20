#pragma once

#include <Windows.h>
#include <Xinput.h>

#include "platform/windows/input/i_windows_gamepad_backend.hpp"

namespace helengine::windows {
    /// Captures Windows gamepad state through the XInput API.
    class WindowsXInputBackend : public IWindowsGamepadBackend {
    public:
        /// Returns which backend this adapter represents.
        WindowsGamepadBackendKind GetBackendKind() const override;

        /// Initializes the XInput backend.
        bool TryInitialize(std::string& failureMessage) override;

        /// Captures the current XInput controllers.
        void CaptureFrame(List<WindowsGamepadDeviceState>& devices) override;
    };
}
