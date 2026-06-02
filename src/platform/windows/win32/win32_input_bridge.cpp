#include "platform/windows/win32/win32_input_bridge.hpp"

#include <array>

#include "runtime/native_list.hpp"

namespace helengine::windows {
    namespace {
        /// Converts one Win32 button-state snapshot into the generated button-state enum.
        ButtonState ToButtonState(SHORT keyState) {
            return (keyState & 0x8000) != 0
                ? ButtonState::Pressed
                : ButtonState::Released;
        }
    }

    /// Creates an input backend bound to the host window.
    Win32InputBackend::Win32InputBackend(Win32Window* window)
        : Window(window)
        , ScrollWheelAccumulator(0)
        , State()
        , PointerWrapEnabled(false)
        , ReceiveInputInBackground(false)
        , PointerWrapDeltaOffset() {
    }

    /// Returns whether the backend continues reporting input while the host window is inactive.
    bool Win32InputBackend::get_ReceiveInputInBackground() {
        return ReceiveInputInBackground;
    }

    /// Updates whether the backend continues reporting input while the host window is inactive.
    void Win32InputBackend::set_ReceiveInputInBackground(bool value) {
        ReceiveInputInBackground = value;
    }

    /// Captures one input frame from the current Windows host state.
    InputFrameState Win32InputBackend::CaptureFrame() {
        InputFrameState frame;
        frame.set_Keyboard(CaptureKeyboardState());
        frame.set_Mouse(CaptureMouseState());
        return frame;
    }

    /// Reads the current keyboard state from Win32 keyboard APIs.
    KeyboardState Win32InputBackend::CaptureKeyboardState() {
        if (!ReceiveInputInBackground && Window != nullptr && Window->GetHandle() != nullptr && !IsWindowForegroundActive(Window->GetHandle())) {
            return KeyboardState();
        }

        List<Keys> pressedKeys;
        std::array<BYTE, 256> keyStates {};
        if (::GetKeyboardState(keyStates.data())) {
            for (int keyCode = 1; keyCode <= 255; keyCode++) {
                if ((keyStates[static_cast<size_t>(keyCode)] & 0x80) == 0) {
                    continue;
                }

                pressedKeys.Add(static_cast<Keys>(keyCode));
            }
        }

        bool capsLock = (::GetKeyState(VK_CAPITAL) & 0x0001) != 0;
        bool numLock = (::GetKeyState(VK_NUMLOCK) & 0x0001) != 0;
        KeyboardState keyboardState = KeyboardState(&pressedKeys, capsLock, numLock);
        return keyboardState;
    }

    /// Reads the current mouse state from Win32 cursor APIs.
    MouseState Win32InputBackend::CaptureMouseState() {
        if (Window == nullptr || Window->GetHandle() == nullptr) {
            return State;
        }

        HWND windowHandle = Window->GetHandle();
        ScrollWheelAccumulator += Window->ConsumeMouseWheelDelta();
        State.set_ScrollWheelValue(ScrollWheelAccumulator);
        PointerWrapDeltaOffset = helengine_int2(0, 0);

        POINT cursorPoint {};
        if (!::GetCursorPos(&cursorPoint)) {
            return State;
        }

        ::ScreenToClient(windowHandle, &cursorPoint);
        State.set_X(cursorPoint.x);
        State.set_Y(cursorPoint.y);
        ApplyPointerWrap(windowHandle);

        if (!ReceiveInputInBackground && !IsWindowForegroundActive(windowHandle)) {
            ReleaseAllButtons();
            return State;
        }

        State.set_LeftButton(ToButtonState(::GetAsyncKeyState(VK_LBUTTON)));
        State.set_MiddleButton(ToButtonState(::GetAsyncKeyState(VK_MBUTTON)));
        State.set_RightButton(ToButtonState(::GetAsyncKeyState(VK_RBUTTON)));
        State.set_XButton1(ToButtonState(::GetAsyncKeyState(VK_XBUTTON1)));
        State.set_XButton2(ToButtonState(::GetAsyncKeyState(VK_XBUTTON2)));
        return State;
    }

    /// Sets the cursor position using host client coordinates.
    void Win32InputBackend::SetPosition(int32_t x, int32_t y) {
        if (Window == nullptr || Window->GetHandle() == nullptr) {
            return;
        }

        POINT clientPoint { x, y };
        ::ClientToScreen(Window->GetHandle(), &clientPoint);
        ::SetCursorPos(clientPoint.x, clientPoint.y);
    }

    /// Releases all button states when the host window is not foreground-active.
    void Win32InputBackend::ReleaseAllButtons() {
        State.set_LeftButton(ButtonState::Released);
        State.set_MiddleButton(ButtonState::Released);
        State.set_RightButton(ButtonState::Released);
        State.set_XButton1(ButtonState::Released);
        State.set_XButton2(ButtonState::Released);
    }

    /// Wraps the cursor across the host client area when edge wrapping is enabled.
    void Win32InputBackend::ApplyPointerWrap(HWND windowHandle) {
        if (!PointerWrapEnabled) {
            return;
        }

        RECT clientRectangle {};
        if (!::GetClientRect(windowHandle, &clientRectangle)) {
            return;
        }

        int clientWidth = clientRectangle.right - clientRectangle.left;
        int clientHeight = clientRectangle.bottom - clientRectangle.top;
        if (clientWidth <= 1 || clientHeight <= 1) {
            return;
        }

        int wrappedX = State.get_X();
        int wrappedY = State.get_Y();
        int deltaOffsetX = 0;
        int deltaOffsetY = 0;

        if (wrappedX <= 0) {
            wrappedX = clientWidth - 2;
            deltaOffsetX = -(clientWidth - 2);
        } else if (wrappedX >= clientWidth - 1) {
            wrappedX = 1;
            deltaOffsetX = clientWidth - 2;
        }

        if (wrappedY <= 0) {
            wrappedY = clientHeight - 2;
            deltaOffsetY = -(clientHeight - 2);
        } else if (wrappedY >= clientHeight - 1) {
            wrappedY = 1;
            deltaOffsetY = clientHeight - 2;
        }

        if (deltaOffsetX == 0 && deltaOffsetY == 0) {
            return;
        }

        State.set_X(wrappedX);
        State.set_Y(wrappedY);
        PointerWrapDeltaOffset = helengine_int2(deltaOffsetX, deltaOffsetY);
        SetPosition(wrappedX, wrappedY);
    }

    /// Returns true when the host window currently owns foreground input focus.
    bool Win32InputBackend::IsWindowForegroundActive(HWND windowHandle) const {
        HWND foregroundWindow = ::GetForegroundWindow();
        if (foregroundWindow == nullptr) {
            return false;
        }

        return foregroundWindow == windowHandle || ::IsChild(windowHandle, foregroundWindow);
    }
}
