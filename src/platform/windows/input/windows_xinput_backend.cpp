#include "platform/windows/input/windows_xinput_backend.hpp"

#include <cstdint>

#include "InputGamepadButton.hpp"

namespace helengine::windows {
    namespace {
        /// Converts one XInput thumb-stick or trigger value into the shared gamepad snapshot.
        InputGamepadState BuildGamepadState(const XINPUT_STATE& state) {
            InputGamepadState gamepad;
            gamepad.set_Connected(true);
            gamepad.set_LeftStickX(state.Gamepad.sThumbLX);
            gamepad.set_LeftStickY(state.Gamepad.sThumbLY);
            gamepad.set_RightStickX(state.Gamepad.sThumbRX);
            gamepad.set_RightStickY(state.Gamepad.sThumbRY);
            gamepad.set_LeftTrigger(static_cast<std::int16_t>(state.Gamepad.bLeftTrigger * 128));
            gamepad.set_RightTrigger(static_cast<std::int16_t>(state.Gamepad.bRightTrigger * 128));
            gamepad.SetButtonDown(InputGamepadButton::South, (state.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0);
            gamepad.SetButtonDown(InputGamepadButton::East, (state.Gamepad.wButtons & XINPUT_GAMEPAD_B) != 0);
            gamepad.SetButtonDown(InputGamepadButton::West, (state.Gamepad.wButtons & XINPUT_GAMEPAD_X) != 0);
            gamepad.SetButtonDown(InputGamepadButton::North, (state.Gamepad.wButtons & XINPUT_GAMEPAD_Y) != 0);
            gamepad.SetButtonDown(InputGamepadButton::LeftShoulder, (state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0);
            gamepad.SetButtonDown(InputGamepadButton::RightShoulder, (state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0);
            gamepad.SetButtonDown(InputGamepadButton::LeftStick, (state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) != 0);
            gamepad.SetButtonDown(InputGamepadButton::RightStick, (state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0);
            gamepad.SetButtonDown(InputGamepadButton::LeftTrigger, state.Gamepad.bLeftTrigger >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
            gamepad.SetButtonDown(InputGamepadButton::RightTrigger, state.Gamepad.bRightTrigger >= XINPUT_GAMEPAD_TRIGGER_THRESHOLD);
            gamepad.SetButtonDown(InputGamepadButton::DPadUp, (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0);
            gamepad.SetButtonDown(InputGamepadButton::DPadDown, (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0);
            gamepad.SetButtonDown(InputGamepadButton::DPadLeft, (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0);
            gamepad.SetButtonDown(InputGamepadButton::DPadRight, (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0);
            gamepad.SetButtonDown(InputGamepadButton::Start, (state.Gamepad.wButtons & XINPUT_GAMEPAD_START) != 0);
            gamepad.SetButtonDown(InputGamepadButton::Select, (state.Gamepad.wButtons & XINPUT_GAMEPAD_BACK) != 0);
            return gamepad;
        }
    }

    /// Returns which backend this adapter represents.
    WindowsGamepadBackendKind WindowsXInputBackend::GetBackendKind() const {
        return WindowsGamepadBackendKind::XInput;
    }

    /// Initializes the XInput backend.
    bool WindowsXInputBackend::TryInitialize(std::string& failureMessage) {
        failureMessage.clear();
        return true;
    }

    /// Captures the current XInput controllers.
    void WindowsXInputBackend::CaptureFrame(List<WindowsGamepadDeviceState>& devices) {
        for (DWORD userIndex = 0; userIndex < XUSER_MAX_COUNT; userIndex++) {
            XINPUT_STATE state {};
            if (::XInputGetState(userIndex, &state) != ERROR_SUCCESS) {
                continue;
            }

            WindowsGamepadDeviceState deviceState;
            deviceState.Identity.BackendKind = WindowsGamepadBackendKind::XInput;
            deviceState.Identity.BackendDeviceId = userIndex;
            deviceState.Identity.SlotIndex = static_cast<std::uint32_t>(userIndex);
            deviceState.Gamepad = BuildGamepadState(state);
            devices.Add(deviceState);
        }
    }
}
