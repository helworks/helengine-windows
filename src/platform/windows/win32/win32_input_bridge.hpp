#pragma once

#include <Windows.h>

#include <array>

#include "platform/windows/win32/win32_window.hpp"

#if __has_include("InputManager.hpp")
#include "ButtonState.hpp"
#include "InputManager.hpp"
#include "Keyboard.hpp"
#include "KeyboardState.hpp"
#include "Mouse.hpp"
#include "MouseState.hpp"
#endif

namespace helengine::windows {
#if __has_include("InputManager.hpp")
    /// Implements the generated keyboard contract using Win32 keyboard-state APIs.
    class Win32Keyboard : public Keyboard {
    public:
        /// Creates an inactive keyboard backend with an empty cached state.
        Win32Keyboard();

        /// Reads the current keyboard state when the backend is active.
        KeyboardState GetState() override;

        /// Enables or disables keyboard capture for the backend.
        void SetActive(bool isActive) override;

    private:
        /// Tracks whether the backend should query Win32 keyboard state.
        bool IsActive;

        /// Stores the raw 256-key Win32 snapshot.
        std::array<BYTE, 256> KeyStates;
    };

    /// Implements the generated mouse contract using Win32 cursor APIs.
    class Win32Mouse : public Mouse {
    public:
        /// Creates a mouse backend bound to the host window.
        explicit Win32Mouse(Win32Window* window);

        /// Reads the current mouse state relative to the host client area.
        MouseState GetState() override;

        /// Sets the cursor position in host client coordinates.
        void SetPosition(int32_t x, int32_t y) override;

        /// Enables or disables client-edge pointer wrapping.
        void SetPointerWrapEnabled(bool isEnabled) override;

        /// Returns and clears the delta offset produced by the last wrap operation.
        int2 ConsumePointerWrapDeltaOffset() override;

    private:
        /// Releases all button states when the host window is not foreground-active.
        void ReleaseAllButtons();

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

        /// Stores the delta offset produced by the most recent wrap.
        int2 PointerWrapDeltaOffset;
    };

    /// Wires generated input management to native Win32 keyboard and mouse backends.
    class Win32InputManager : public InputManager {
    public:
        /// Creates an input manager bound to the host window.
        explicit Win32InputManager(Win32Window* window);

        /// Releases the owned native keyboard and mouse backends.
        ~Win32InputManager();

    private:
        /// Stores the owned keyboard backend.
        Win32Keyboard* NativeKeyboard;

        /// Stores the owned mouse backend.
        Win32Mouse* NativeMouse;
    };
#endif
}
