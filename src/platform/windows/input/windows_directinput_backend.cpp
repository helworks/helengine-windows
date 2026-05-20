#include "platform/windows/input/windows_directinput_backend.hpp"

#include <algorithm>
#include <cstdint>

#include "InputGamepadButton.hpp"

namespace helengine::windows {
    namespace {
        /// Converts one DirectInput axis value into the shared signed-short range.
        std::int16_t NormalizeAxis(LONG value) {
            LONG clamped = std::clamp(value, static_cast<LONG>(0), static_cast<LONG>(65535));
            return static_cast<std::int16_t>(clamped - 32767);
        }

        /// Converts one DirectInput trigger-style value into the shared non-negative trigger range.
        std::int16_t NormalizeTrigger(LONG value) {
            LONG clamped = std::clamp(value, static_cast<LONG>(0), static_cast<LONG>(65535));
            return static_cast<std::int16_t>(clamped / 2);
        }

        /// Returns whether the supplied DirectInput button is currently pressed.
        bool IsButtonPressed(const DIJOYSTATE2& state, int buttonIndex) {
            if (buttonIndex < 0 || buttonIndex >= 128) {
                return false;
            }

            return (state.rgbButtons[buttonIndex] & 0x80) != 0;
        }

        /// Returns whether one DirectInput POV hat reports the supplied direction.
        bool IsPovDirectionPressed(DWORD povValue, DWORD minimum, DWORD maximum) {
            if (LOWORD(povValue) == 0xFFFF) {
                return false;
            }

            if (minimum <= maximum) {
                return povValue >= minimum && povValue <= maximum;
            }

            return povValue >= minimum || povValue <= maximum;
        }

        /// Builds one normalized gamepad snapshot from a DirectInput state packet.
        InputGamepadState BuildGamepadState(const DIJOYSTATE2& state) {
            InputGamepadState gamepad;
            gamepad.set_Connected(true);
            gamepad.set_LeftStickX(NormalizeAxis(state.lX));
            gamepad.set_LeftStickY(static_cast<std::int16_t>(-NormalizeAxis(state.lY)));
            gamepad.set_RightStickX(NormalizeAxis(state.lRx));
            gamepad.set_RightStickY(static_cast<std::int16_t>(-NormalizeAxis(state.lRy)));
            gamepad.set_LeftTrigger(NormalizeTrigger(state.rglSlider[0] != 0 ? state.rglSlider[0] : state.lZ));
            gamepad.set_RightTrigger(NormalizeTrigger(state.rglSlider[1] != 0 ? state.rglSlider[1] : state.lRz));
            gamepad.SetButtonDown(InputGamepadButton::South, IsButtonPressed(state, 0));
            gamepad.SetButtonDown(InputGamepadButton::East, IsButtonPressed(state, 1));
            gamepad.SetButtonDown(InputGamepadButton::West, IsButtonPressed(state, 2));
            gamepad.SetButtonDown(InputGamepadButton::North, IsButtonPressed(state, 3));
            gamepad.SetButtonDown(InputGamepadButton::LeftShoulder, IsButtonPressed(state, 4));
            gamepad.SetButtonDown(InputGamepadButton::RightShoulder, IsButtonPressed(state, 5));
            gamepad.SetButtonDown(InputGamepadButton::Select, IsButtonPressed(state, 6));
            gamepad.SetButtonDown(InputGamepadButton::Start, IsButtonPressed(state, 7));
            gamepad.SetButtonDown(InputGamepadButton::LeftStick, IsButtonPressed(state, 8));
            gamepad.SetButtonDown(InputGamepadButton::RightStick, IsButtonPressed(state, 9));
            gamepad.SetButtonDown(InputGamepadButton::LeftTrigger, gamepad.get_LeftTrigger() >= 16384);
            gamepad.SetButtonDown(InputGamepadButton::RightTrigger, gamepad.get_RightTrigger() >= 16384);
            gamepad.SetButtonDown(InputGamepadButton::DPadUp, IsPovDirectionPressed(state.rgdwPOV[0], 31500, 4500));
            gamepad.SetButtonDown(InputGamepadButton::DPadRight, IsPovDirectionPressed(state.rgdwPOV[0], 4500, 13500));
            gamepad.SetButtonDown(InputGamepadButton::DPadDown, IsPovDirectionPressed(state.rgdwPOV[0], 13500, 22500));
            gamepad.SetButtonDown(InputGamepadButton::DPadLeft, IsPovDirectionPressed(state.rgdwPOV[0], 22500, 31500));
            return gamepad;
        }

        /// Extracts the HID vendor identifier from one DirectInput product GUID.
        std::uint32_t ExtractVendorId(const GUID& guid) {
            return static_cast<std::uint32_t>((guid.Data1 >> 16) & 0xFFFF);
        }

        /// Extracts the HID product identifier from one DirectInput product GUID.
        std::uint32_t ExtractProductId(const GUID& guid) {
            return static_cast<std::uint32_t>(guid.Data1 & 0xFFFF);
        }

        /// Builds a stable 64-bit identifier from one DirectInput instance GUID.
        std::uint64_t BuildBackendDeviceId(const GUID& guid) {
            std::uint64_t upper = static_cast<std::uint64_t>(guid.Data1) << 32;
            std::uint64_t lower = static_cast<std::uint64_t>(guid.Data2) << 16 | guid.Data3;
            return upper | lower;
        }
    }

    /// Creates one DirectInput backend bound to the supplied host window handle.
    WindowsDirectInputBackend::WindowsDirectInputBackend(HWND windowHandle)
        : WindowHandle(windowHandle)
        , DirectInput(nullptr)
        , Devices() {
    }

    /// Releases DirectInput devices and interfaces owned by the backend.
    WindowsDirectInputBackend::~WindowsDirectInputBackend() {
        ReleaseDevices();
        if (DirectInput != nullptr) {
            DirectInput->Release();
            DirectInput = nullptr;
        }
    }

    /// Returns which backend this adapter represents.
    WindowsGamepadBackendKind WindowsDirectInputBackend::GetBackendKind() const {
        return WindowsGamepadBackendKind::DirectInput;
    }

    /// Initializes the DirectInput interface and enumerates attached controllers.
    bool WindowsDirectInputBackend::TryInitialize(std::string& failureMessage) {
        failureMessage.clear();
        if (WindowHandle == nullptr) {
            failureMessage = "DirectInput requires a valid host window handle.";
            return false;
        }

        HRESULT result = ::DirectInput8Create(
            ::GetModuleHandleW(nullptr),
            DIRECTINPUT_VERSION,
            IID_IDirectInput8W,
            reinterpret_cast<void**>(&DirectInput),
            nullptr);
        if (FAILED(result) || DirectInput == nullptr) {
            failureMessage = "DirectInput8Create failed.";
            return false;
        }

        result = DirectInput->EnumDevices(DI8DEVCLASS_GAMECTRL, EnumerateAttachedControllers, this, DIEDFL_ATTACHEDONLY);
        if (FAILED(result)) {
            failureMessage = "DirectInput device enumeration failed.";
            return false;
        }

        return true;
    }

    /// Captures the current DirectInput controllers.
    void WindowsDirectInputBackend::CaptureFrame(List<WindowsGamepadDeviceState>& devices) {
        for (std::size_t index = 0; index < Devices.size(); index++) {
            DeviceEntry& entry = Devices[index];
            if (entry.Device == nullptr) {
                continue;
            }

            HRESULT pollResult = entry.Device->Poll();
            if (FAILED(pollResult)) {
                entry.Device->Acquire();
                pollResult = entry.Device->Poll();
                if (FAILED(pollResult)) {
                    continue;
                }
            }

            DIJOYSTATE2 state {};
            HRESULT stateResult = entry.Device->GetDeviceState(sizeof(DIJOYSTATE2), &state);
            if (FAILED(stateResult)) {
                continue;
            }

            WindowsGamepadDeviceState deviceState;
            deviceState.Identity.BackendKind = WindowsGamepadBackendKind::DirectInput;
            deviceState.Identity.BackendDeviceId = BuildBackendDeviceId(entry.InstanceGuid);
            deviceState.Identity.VendorId = entry.VendorId;
            deviceState.Identity.ProductId = entry.ProductId;
            deviceState.Gamepad = BuildGamepadState(state);
            devices.Add(deviceState);
        }
    }

    /// Enumerates one attached DirectInput controller.
    BOOL CALLBACK WindowsDirectInputBackend::EnumerateAttachedControllers(const DIDEVICEINSTANCEW* instance, VOID* context) {
        if (instance == nullptr || context == nullptr) {
            return DIENUM_STOP;
        }

        WindowsDirectInputBackend* backend = static_cast<WindowsDirectInputBackend*>(context);
        return backend->AddDevice(*instance);
    }

    /// Adds one DirectInput controller to the backend.
    BOOL WindowsDirectInputBackend::AddDevice(const DIDEVICEINSTANCEW& instance) {
        if (DirectInput == nullptr) {
            return DIENUM_STOP;
        }

        IDirectInputDevice8W* device = nullptr;
        HRESULT createResult = DirectInput->CreateDevice(instance.guidInstance, &device, nullptr);
        if (FAILED(createResult) || device == nullptr) {
            return DIENUM_CONTINUE;
        }

        HRESULT dataFormatResult = device->SetDataFormat(&c_dfDIJoystick2);
        HRESULT cooperativeResult = device->SetCooperativeLevel(WindowHandle, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
        HRESULT acquireResult = device->Acquire();
        if (FAILED(dataFormatResult) || FAILED(cooperativeResult) || FAILED(acquireResult)) {
            device->Release();
            return DIENUM_CONTINUE;
        }

        DeviceEntry entry;
        entry.Device = device;
        entry.InstanceGuid = instance.guidInstance;
        entry.ProductGuid = instance.guidProduct;
        entry.VendorId = ExtractVendorId(instance.guidProduct);
        entry.ProductId = ExtractProductId(instance.guidProduct);
        Devices.push_back(entry);
        return DIENUM_CONTINUE;
    }

    /// Releases every DirectInput device owned by the backend.
    void WindowsDirectInputBackend::ReleaseDevices() {
        for (std::size_t index = 0; index < Devices.size(); index++) {
            if (Devices[index].Device != nullptr) {
                Devices[index].Device->Unacquire();
                Devices[index].Device->Release();
                Devices[index].Device = nullptr;
            }
        }

        Devices.clear();
    }
}
