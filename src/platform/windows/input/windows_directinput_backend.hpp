#pragma once

#define DIRECTINPUT_VERSION 0x0800
#include <Windows.h>
#include <dinput.h>

#include <cstdint>
#include <string>
#include <vector>

#include "platform/windows/input/i_windows_gamepad_backend.hpp"

namespace helengine::windows {
    /// Captures Windows gamepad state through the DirectInput 8 API.
    class WindowsDirectInputBackend : public IWindowsGamepadBackend {
    public:
        /// Creates one DirectInput backend bound to the supplied host window handle.
        explicit WindowsDirectInputBackend(HWND windowHandle);

        /// Releases DirectInput devices and interfaces owned by the backend.
        ~WindowsDirectInputBackend() override;

        /// Returns which backend this adapter represents.
        WindowsGamepadBackendKind GetBackendKind() const override;

        /// Initializes the DirectInput interface and enumerates attached controllers.
        bool TryInitialize(std::string& failureMessage) override;

        /// Captures the current DirectInput controllers.
        void CaptureFrame(List<WindowsGamepadDeviceState>& devices) override;

    private:
        /// Stores one DirectInput device together with the identity data used for dedupe.
        struct DeviceEntry {
            IDirectInputDevice8W* Device = nullptr;
            GUID InstanceGuid {};
            GUID ProductGuid {};
            std::uint32_t VendorId = 0;
            std::uint32_t ProductId = 0;
        };

        /// Enumerates one attached DirectInput controller.
        static BOOL CALLBACK EnumerateAttachedControllers(const DIDEVICEINSTANCEW* instance, VOID* context);

        /// Adds one DirectInput controller to the backend.
        BOOL AddDevice(const DIDEVICEINSTANCEW& instance);

        /// Releases every DirectInput device owned by the backend.
        void ReleaseDevices();

        /// Stores the host window handle required for cooperative-level setup.
        HWND WindowHandle;

        /// Stores the DirectInput root interface.
        IDirectInput8W* DirectInput;

        /// Stores the attached DirectInput controller list.
        std::vector<DeviceEntry> Devices;
    };
}
