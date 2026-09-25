#pragma once

#include <Windows.h>

namespace helengine::windows {
    /// Switches the overlay window between catching the mouse and letting it pass through to the windows behind, by
    /// toggling WS_EX_TRANSPARENT on the layered overlay window. DWM does not hit-test by pixel alpha on its own, so
    /// the application decides the state from the sampled alpha under the cursor and this class applies it. The window
    /// is created without WS_EX_TRANSPARENT, so the initial state is click-through off. Only constructed in overlay
    /// mode.
    class Win32ClickThroughController {
    public:
        /// Creates a controller for the overlay window, starting in the click-through-off state the window was created
        /// with.
        /// <param name="windowHandle">The overlay window whose extended style is toggled; not owned.</param>
        explicit Win32ClickThroughController(HWND windowHandle);

        /// Applies the requested state: sets or clears WS_EX_TRANSPARENT with SetWindowLongPtrW and makes the change
        /// take effect with SetWindowPos(SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE).
        /// Does nothing when the state is already applied, so a steady state costs no Win32 calls. Throws
        /// std::runtime_error carrying the Win32 error when either call fails.
        /// <param name="clickThrough">True to let the mouse pass through the window, false to catch it.</param>
        void Apply(bool clickThrough);

        /// Gets whether the mouse currently passes through the window.
        bool IsClickThrough() const;

    private:
        /// Stores the overlay window whose extended style is toggled; not owned.
        HWND WindowHandle;

        /// Stores whether WS_EX_TRANSPARENT is currently applied to the window.
        bool ClickThrough;
    };
}
