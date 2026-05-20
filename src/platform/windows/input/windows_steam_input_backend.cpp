#include "platform/windows/input/windows_steam_input_backend.hpp"

#include <Windows.h>
#include <Xinput.h>

#include <cstdint>

#include "InputGamepadButton.hpp"

#include "steam/steam_api.h"
#include "steam/isteaminput.h"

namespace helengine::windows {
    namespace {
        /// Converts one XInput snapshot into the shared gamepad format for Steam-managed emulated gamepad slots.
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

    /// Creates one SteamInput backend with no initialized Steam API state.
    WindowsSteamInputBackend::WindowsSteamInputBackend()
        : SteamApiInitialized(false)
        , SteamInputInitialized(false) {
    }

    /// Shuts SteamInput and SteamAPI down when the backend owns them.
    WindowsSteamInputBackend::~WindowsSteamInputBackend() {
        if (SteamInputInitialized && SteamInput() != nullptr) {
            SteamInput()->Shutdown();
            SteamInputInitialized = false;
        }

        if (SteamApiInitialized) {
            SteamAPI_Shutdown();
            SteamApiInitialized = false;
        }
    }

    /// Returns which backend this adapter represents.
    WindowsGamepadBackendKind WindowsSteamInputBackend::GetBackendKind() const {
        return WindowsGamepadBackendKind::SteamInput;
    }

    /// Initializes SteamAPI and SteamInput.
    bool WindowsSteamInputBackend::TryInitialize(std::string& failureMessage) {
        failureMessage.clear();
        if (!SteamAPI_Init()) {
            failureMessage = "SteamAPI_Init failed.";
            return false;
        }

        SteamApiInitialized = true;
        if (SteamInput() == nullptr || !SteamInput()->Init(true)) {
            SteamAPI_Shutdown();
            SteamApiInitialized = false;
            failureMessage = "SteamInput()->Init(true) failed.";
            return false;
        }

        SteamInputInitialized = true;
        return true;
    }

    /// Captures the current Steam-managed controllers.
    void WindowsSteamInputBackend::CaptureFrame(List<WindowsGamepadDeviceState>& devices) {
        if (!SteamInputInitialized || SteamInput() == nullptr) {
            return;
        }

        SteamInput()->RunFrame();
        InputHandle_t handles[STEAM_INPUT_MAX_COUNT] {};
        int handleCount = SteamInput()->GetConnectedControllers(handles);
        for (int index = 0; index < handleCount; index++) {
            InputHandle_t handle = handles[index];
            int gamepadIndex = SteamInput()->GetGamepadIndexForController(handle);
            if (gamepadIndex < 0 || gamepadIndex >= XUSER_MAX_COUNT) {
                continue;
            }

            XINPUT_STATE state {};
            if (::XInputGetState(static_cast<DWORD>(gamepadIndex), &state) != ERROR_SUCCESS) {
                continue;
            }

            WindowsGamepadDeviceState deviceState;
            deviceState.Identity.BackendKind = WindowsGamepadBackendKind::SteamInput;
            deviceState.Identity.BackendDeviceId = static_cast<std::uint64_t>(handle);
            deviceState.Identity.SlotIndex = static_cast<std::uint32_t>(gamepadIndex);
            deviceState.Gamepad = BuildGamepadState(state);
            devices.Add(deviceState);
        }
    }
}
