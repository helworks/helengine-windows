#pragma once

#include <Windows.h>

#include <array>
#include <cstdint>

#include "ButtonState.hpp"
#include "IInputBackend.hpp"
#include "InputFrameState.hpp"
#include "KeyboardState.hpp"
#include "Keys.hpp"
#include "MouseState.hpp"
#include "int2.hpp"

#include "runtime/array.hpp"

#include "platform/windows/input/windows_gamepad_backend_mux.hpp"
#include "platform/windows/win32/win32_window.hpp"

namespace helengine::windows {
    /// Captures Windows keyboard and mouse state into the generated portable input frame.
    class Win32InputBackend : public IInputBackend {
    public:
        /// Creates a backend bound to the host window.
        explicit Win32InputBackend(Win32Window* window);

        /// Releases any gamepad-frame buffers owned by the backend.
        ~Win32InputBackend();

        /// Returns whether the backend continues reporting input while the host window is inactive.
        bool get_ReceiveInputInBackground();

        /// Updates whether the backend continues reporting input while the host window is inactive.
        void set_ReceiveInputInBackground(bool value);

        /// Captures one input frame from the current Windows host state.
        InputFrameState CaptureFrame() override;

    private:
        /// Reads the current keyboard state from Win32 keyboard APIs.
        KeyboardState CaptureKeyboardState();

        /// Reads the current mouse state from Win32 cursor APIs.
        MouseState CaptureMouseState();

        /// Sets the cursor position using host client coordinates.
        void SetPosition(int32_t x, int32_t y);

        /// Releases all button states when the host window is not foreground-active.
        void ReleaseAllButtons();

        /// Stores the captured gamepad list in the backend-owned rotating frame buffers.
        Array<InputGamepadState>* StoreCapturedGamepads(const List<InputGamepadState>& gamepads);

        /// Releases one backend-owned gamepad-frame buffer.
        void ReleaseGamepadFrameBuffer(Array<InputGamepadState>* buffer);

        /// Wraps the cursor across the host client area when edge wrapping is enabled.
        void ApplyPointerWrap(HWND windowHandle);

        /// Returns true when the host window currently owns foreground input focus.
        bool IsWindowForegroundActive(HWND windowHandle) const;

        /// Stores the host window used for cursor coordinate translation.
        Win32Window* Window;

        /// Accumulates wheel delta reported through the host message pump.
        int ScrollWheelAccumulator;

        /// Stores the last reported mouse state.
        MouseState State;

        /// Tracks whether client-edge pointer wrapping is enabled.
        bool PointerWrapEnabled;

        /// Tracks whether background keyboard and mouse button capture is enabled.
        bool ReceiveInputInBackground;

        /// Stores the delta offset produced by the most recent wrap.
        int2 PointerWrapDeltaOffset;

        /// Coordinates the enabled Windows gamepad backends for the current player build.
        WindowsGamepadBackendMux GamepadBackendMux;

        /// Stores the two rotating frame buffers used to preserve current and previous gamepad snapshots.
        Array<InputGamepadState>* GamepadFrameBuffers[2];

        /// Stores the active rotating gamepad buffer index.
        int CurrentGamepadFrameBufferIndex;
    };
}
