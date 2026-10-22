#include "platform/windows/win32/win32_input_bridge.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "runtime/native_list.hpp"
#include "platform/windows/runtime/windows_tracy_profiler.hpp"
#include "platform/windows/win32/win32_pointer_window_routing.hpp"

namespace helengine::windows {
    namespace {
        /// Converts one Win32 button-state snapshot into the generated button-state enum.
        ButtonState ToButtonState(std::uint32_t pressedButtons, std::uint32_t buttonMask) {
            return (pressedButtons & buttonMask) != 0
                ? ButtonState::Pressed
                : ButtonState::Released;
        }

        /// Resolves the application-adjacent watched-input diagnostics log path.
        std::filesystem::path ResolveInputTraceLogPath() {
            wchar_t modulePathBuffer[MAX_PATH] {};
            DWORD characterCount = ::GetModuleFileNameW(nullptr, modulePathBuffer, MAX_PATH);
            if (characterCount == 0 || characterCount >= MAX_PATH) {
                return std::filesystem::path("helengine_windows.input.log");
            }

            return std::filesystem::path(modulePathBuffer).parent_path() / "helengine_windows.input.log";
        }

        /// Returns whether the supplied key state buffer marks one Win32 virtual key as pressed.
        bool IsVirtualKeyPressed(const std::array<BYTE, 256>& keyStates, int virtualKey) {
            if (virtualKey < 0 || virtualKey > 255) {
                return false;
            }

            return (keyStates[static_cast<size_t>(virtualKey)] & 0x80) != 0;
        }

        /// Builds one compact watched-key bit mask for menu-navigation diagnostics.
        std::uint32_t BuildWatchedKeyMask(const std::array<BYTE, 256>& keyStates) {
            std::uint32_t mask = 0;
            if (IsVirtualKeyPressed(keyStates, VK_UP)) {
                mask |= 1u << 0;
            }
            if (IsVirtualKeyPressed(keyStates, VK_DOWN)) {
                mask |= 1u << 1;
            }
            if (IsVirtualKeyPressed(keyStates, VK_RETURN)) {
                mask |= 1u << 2;
            }
            if (IsVirtualKeyPressed(keyStates, VK_SPACE)) {
                mask |= 1u << 3;
            }
            if (IsVirtualKeyPressed(keyStates, VK_ESCAPE)) {
                mask |= 1u << 4;
            }
            if (IsVirtualKeyPressed(keyStates, 'W')) {
                mask |= 1u << 5;
            }
            if (IsVirtualKeyPressed(keyStates, 'S')) {
                mask |= 1u << 6;
            }

            return mask;
        }

        /// Builds one compact watched-key bit mask from the generated keyboard-state container.
        std::uint32_t BuildKeyboardStateWatchedKeyMask(KeyboardState& keyboardState) {
            std::uint32_t mask = 0;
            if (keyboardState.IsKeyDown(Keys::Up)) {
                mask |= 1u << 0;
            }
            if (keyboardState.IsKeyDown(Keys::Down)) {
                mask |= 1u << 1;
            }
            if (keyboardState.IsKeyDown(Keys::Enter)) {
                mask |= 1u << 2;
            }
            if (keyboardState.IsKeyDown(Keys::Space)) {
                mask |= 1u << 3;
            }
            if (keyboardState.IsKeyDown(Keys::Escape)) {
                mask |= 1u << 4;
            }
            if (keyboardState.IsKeyDown(Keys::W)) {
                mask |= 1u << 5;
            }
            if (keyboardState.IsKeyDown(Keys::S)) {
                mask |= 1u << 6;
            }

            return mask;
        }

        /// Appends one single-line watched-input trace message beside the packaged player executable.
        void AppendInputTraceLine(const std::string& message) {
            std::filesystem::path logPath = ResolveInputTraceLogPath();
            std::ofstream stream(logPath, std::ios::out | std::ios::app);
            if (!stream.is_open()) {
                return;
            }

            stream << message << '\n';
            stream.flush();
        }
    }

    /// Creates an input backend bound to the host window.
    Win32InputBackend::Win32InputBackend(Win32Window* window)
        : Window(window)
        , KeyboardWindow(window)
        , ScrollWheelAccumulator(0)
        , State()
        , PointerWrapEnabled(false)
        , ReceiveInputInBackground(false)
        , PointerWrapDeltaOffset()
        , HasLoggedInputTraceSample(false)
        , LastLoggedForegroundActive(false)
        , LastLoggedWatchedKeyMask(0) {
        DWORD length = GetEnvironmentVariableW(L"HELENGINE_INPUT_REPLAY_FILE", nullptr, 0);
        if (length > 0) {
            if (length > 32768) { throw std::invalid_argument("Input replay path exceeds the Windows environment path limit."); }
            std::wstring path(length, L'\0');
            DWORD copied = GetEnvironmentVariableW(L"HELENGINE_INPUT_REPLAY_FILE", path.data(), length);
            if (copied == 0 || copied >= length) { throw std::runtime_error("Unable to read the input replay path."); }
            path.resize(copied);
            std::ifstream recording { std::filesystem::path(path) };
            if (!recording) { throw std::runtime_error("Unable to open the explicitly requested input replay."); }
            Replay.Load(recording);
            AppendInputTraceLine("replay=enabled");
        }
    }

    /// Selects the focused keyboard view while restoring its pointer routing for this frame.
    void Win32InputBackend::SelectWindow(Win32Window& window) {
        SelectPointerWindow(window);
        KeyboardWindow = &window;
    }

    /// Changes pointer coordinates independently of keyboard focus for passive overlay interaction.
    void Win32InputBackend::SelectPointerWindow(Win32Window& window) {
        if (window.GetHandle() == nullptr) {
            throw std::invalid_argument("Cannot select a closed input window.");
        }
        if (Window != &window) {
            Window = &window;
            PointerWrapDeltaOffset = int2(0, 0);
        }
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
        HELENGINE_TRACY_ZONE_N("Input.Capture");
        InputFrameState frame;
        frame.set_Keyboard(CaptureKeyboardState());
        frame.set_Mouse(CaptureMouseState());
        return frame;
    }

    /// Reads the current keyboard state from Win32 keyboard APIs.
    KeyboardState Win32InputBackend::CaptureKeyboardState() {
        if (Replay.IsEnabled()) {
            List<Keys> pressedKeys;
            for (int key : Replay.NextKeys()) { pressedKeys.Add(static_cast<Keys>(key)); }
            return KeyboardState(&pressedKeys, false, false);
        }
        HWND windowHandle = KeyboardWindow != nullptr ? KeyboardWindow->GetHandle() : nullptr;
        bool isForegroundActive = windowHandle != nullptr && IsWindowForegroundActive(windowHandle);
        if (!ReceiveInputInBackground && windowHandle != nullptr && !isForegroundActive) {
            if (!HasLoggedInputTraceSample || LastLoggedForegroundActive || LastLoggedWatchedKeyMask != 0) {
                std::ostringstream messageBuilder;
                messageBuilder
                    << "foreground=0 receive_background=" << (ReceiveInputInBackground ? 1 : 0)
                    << " watched_mask=0";
                AppendInputTraceLine(messageBuilder.str());
                HasLoggedInputTraceSample = true;
                LastLoggedForegroundActive = false;
                LastLoggedWatchedKeyMask = 0;
            }
            return KeyboardState();
        }

        List<Keys> pressedKeys;
        std::array<BYTE, 256> keyStates {};
        bool capturedKeyboardState = ::GetKeyboardState(keyStates.data()) == TRUE;
        if (capturedKeyboardState) {
            for (int keyCode = 1; keyCode <= 255; keyCode++) {
                if ((keyStates[static_cast<size_t>(keyCode)] & 0x80) == 0) {
                    continue;
                }

                pressedKeys.Add(static_cast<Keys>(keyCode));
            }
        }

        std::uint32_t watchedKeyMask = capturedKeyboardState
            ? BuildWatchedKeyMask(keyStates)
            : 0;
        bool capsLock = (::GetKeyState(VK_CAPITAL) & 0x0001) != 0;
        bool numLock = (::GetKeyState(VK_NUMLOCK) & 0x0001) != 0;
        KeyboardState keyboardState = KeyboardState(&pressedKeys, capsLock, numLock);
        std::uint32_t keyboardStateWatchedKeyMask = BuildKeyboardStateWatchedKeyMask(keyboardState);
        if (!HasLoggedInputTraceSample
            || LastLoggedForegroundActive != isForegroundActive
            || LastLoggedWatchedKeyMask != watchedKeyMask) {
            std::ostringstream messageBuilder;
            messageBuilder
                << "foreground=" << (isForegroundActive ? 1 : 0)
                << " receive_background=" << (ReceiveInputInBackground ? 1 : 0)
                << " get_keyboard_state=" << (capturedKeyboardState ? 1 : 0)
                << " watched_mask=" << watchedKeyMask
                << " keyboard_state_mask=" << keyboardStateWatchedKeyMask
                << " pressed_key_count=" << keyboardState.GetPressedKeyCount();
            AppendInputTraceLine(messageBuilder.str());
            HasLoggedInputTraceSample = true;
            LastLoggedForegroundActive = isForegroundActive;
            LastLoggedWatchedKeyMask = watchedKeyMask;
        }
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
        PointerWrapDeltaOffset = int2(0, 0);

        POINT cursorPoint {};
        if (!::GetCursorPos(&cursorPoint)) {
            return State;
        }

        bool passivePointerTarget = Window != KeyboardWindow
            && Win32PointerWindowRouting::IsPointerTarget(windowHandle, ::WindowFromPoint(cursorPoint));
        ::ScreenToClient(windowHandle, &cursorPoint);
        State.set_X(cursorPoint.x);
        State.set_Y(cursorPoint.y);
        ApplyPointerWrap(windowHandle);

        const int virtualKeys[] = { VK_LBUTTON, VK_MBUTTON, VK_RBUTTON, VK_XBUTTON1, VK_XBUTTON2 };
        std::uint32_t physicalButtons = 0;
        for (std::uint32_t index = 0; index < 5; index++) {
            if ((::GetAsyncKeyState(virtualKeys[index]) & 0x8000) != 0) { physicalButtons |= 1u << index; }
        }
        bool eligible = ReceiveInputInBackground || IsWindowForegroundActive(windowHandle) || passivePointerTarget;
        std::uint32_t pressedButtons = PointerButtons.Capture(physicalButtons, eligible, windowHandle);
        State.set_LeftButton(ToButtonState(pressedButtons, 1u << 0));
        State.set_MiddleButton(ToButtonState(pressedButtons, 1u << 1));
        State.set_RightButton(ToButtonState(pressedButtons, 1u << 2));
        State.set_XButton1(ToButtonState(pressedButtons, 1u << 3));
        State.set_XButton2(ToButtonState(pressedButtons, 1u << 4));
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
        PointerWrapDeltaOffset = int2(deltaOffsetX, deltaOffsetY);
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
