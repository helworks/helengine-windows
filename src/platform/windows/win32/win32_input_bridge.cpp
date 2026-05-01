#include "platform/windows/win32/win32_input_bridge.hpp"

#include <vector>

namespace helengine::windows {
#if __has_include("InputManager.hpp")
    namespace {
        /// Converts one Win32 button-state snapshot into the generated button-state enum.
        ButtonState ToButtonState(SHORT keyState) {
            return (keyState & 0x8000) != 0
                ? ButtonState::Pressed
                : ButtonState::Released;
        }
    }

    /// Creates an inactive keyboard backend with an empty cached state.
    Win32Keyboard::Win32Keyboard()
        : IsActive(false)
        , KeyStates() {
    }

    /// Reads the current keyboard state when the backend is active.
    KeyboardState Win32Keyboard::GetState() {
        List<Keys>* pressedKeys = new List<Keys>();
        if (IsActive) {
            KeyStates.fill(0);
            if (::GetKeyboardState(KeyStates.data())) {
                for (int keyCode = 1; keyCode <= 255; keyCode++) {
                    if ((KeyStates[static_cast<size_t>(keyCode)] & 0x80) == 0) {
                        continue;
                    }

                    pressedKeys->Add(static_cast<Keys>(keyCode));
                }
            }
        }

        bool capsLock = (::GetKeyState(VK_CAPITAL) & 0x0001) != 0;
        bool numLock = (::GetKeyState(VK_NUMLOCK) & 0x0001) != 0;
        return KeyboardState(pressedKeys, capsLock, numLock);
    }

    /// Enables or disables keyboard capture for the backend.
    void Win32Keyboard::SetActive(bool isActive) {
        IsActive = isActive;
    }

    /// Creates a mouse backend bound to the host window.
    Win32Mouse::Win32Mouse(Win32Window* window)
        : Window(window)
        , ScrollWheelAccumulator(0)
        , State()
        , PointerWrapEnabled(false)
        , PointerWrapDeltaOffset() {
    }

    /// Reads the current mouse state relative to the host client area.
    MouseState Win32Mouse::GetState() {
        if (Window == nullptr || Window->GetHandle() == nullptr) {
            return State;
        }

        HWND windowHandle = Window->GetHandle();
        ScrollWheelAccumulator += Window->ConsumeMouseWheelDelta();
        State.set_ScrollWheelValue(ScrollWheelAccumulator);
        PointerWrapDeltaOffset = int2(0, 0);

        POINT cursorPoint {};
        if (!::GetCursorPos(&cursorPoint)) {
            return State;
        }

        ::ScreenToClient(windowHandle, &cursorPoint);
        State.set_X(cursorPoint.x);
        State.set_Y(cursorPoint.y);
        ApplyPointerWrap(windowHandle);

        if (!IsWindowForegroundActive(windowHandle)) {
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

    /// Sets the cursor position in host client coordinates.
    void Win32Mouse::SetPosition(int32_t x, int32_t y) {
        if (Window == nullptr || Window->GetHandle() == nullptr) {
            return;
        }

        POINT clientPoint { x, y };
        ::ClientToScreen(Window->GetHandle(), &clientPoint);
        ::SetCursorPos(clientPoint.x, clientPoint.y);
    }

    /// Enables or disables client-edge pointer wrapping.
    void Win32Mouse::SetPointerWrapEnabled(bool isEnabled) {
        PointerWrapEnabled = isEnabled;
        if (!PointerWrapEnabled) {
            PointerWrapDeltaOffset = int2(0, 0);
        }
    }

    /// Returns and clears the delta offset produced by the last wrap operation.
    int2 Win32Mouse::ConsumePointerWrapDeltaOffset() {
        int2 pointerWrapDeltaOffset = PointerWrapDeltaOffset;
        PointerWrapDeltaOffset = int2(0, 0);
        return pointerWrapDeltaOffset;
    }

    /// Releases all button states when the host window is not foreground-active.
    void Win32Mouse::ReleaseAllButtons() {
        State.set_LeftButton(ButtonState::Released);
        State.set_MiddleButton(ButtonState::Released);
        State.set_RightButton(ButtonState::Released);
        State.set_XButton1(ButtonState::Released);
        State.set_XButton2(ButtonState::Released);
    }

    /// Wraps the cursor across the host client area when edge wrapping is enabled.
    void Win32Mouse::ApplyPointerWrap(HWND windowHandle) {
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
        PointerWrapDeltaOffset = int2(deltaOffsetX, deltaOffsetY);
        SetPosition(wrappedX, wrappedY);
    }

    /// Returns true when the host window currently owns foreground input focus.
    bool Win32Mouse::IsWindowForegroundActive(HWND windowHandle) const {
        HWND foregroundWindow = ::GetForegroundWindow();
        if (foregroundWindow == nullptr) {
            return false;
        }

        return foregroundWindow == windowHandle || ::IsChild(windowHandle, foregroundWindow);
    }

    /// Creates an input manager bound to the host window.
    Win32InputManager::Win32InputManager(Win32Window* window)
        : InputManager()
        , NativeKeyboard(new Win32Keyboard())
        , NativeMouse(new Win32Mouse(window)) {
        Keyboard = NativeKeyboard;
        Mouse = NativeMouse;
    }

    /// Releases the owned native keyboard and mouse backends.
    Win32InputManager::~Win32InputManager() {
        Keyboard = nullptr;
        Mouse = nullptr;
        delete NativeMouse;
        delete NativeKeyboard;
    }
#endif
}
