#pragma once

#include <Windows.h>

#include <cstdint>
#include <string>

#include "platform/windows/directx11/directx11_hit_test_sampler.hpp"
#include "platform/windows/win32/win32_click_through_controller.hpp"
#include "platform/windows/win32/win32_command_line_options.hpp"

namespace helengine::windows {
    class DirectX11Bootstrap;

    /// Decides the overlay window's per-pixel click-through. Each frame it samples the drawn alpha under the cursor
    /// (or under the fixed --hit-test-probe point) through its DirectX11HitTestSampler, and turns the newest completed
    /// sample into a click-through state with the alpha < 8 threshold, applied through its Win32ClickThroughController.
    /// Probe runs apply the same real toggle and also remember the sampled alpha and the resulting extended window style
    /// for the HIT_TEST line. It also watches the cursor for the idle-throttled loop, because a click-through window
    /// receives no mouse messages. Only constructed in overlay mode, so normal mode never runs any of this.
    class Win32OverlayHitTestController {
    public:
        /// Creates the sampler on the bootstrap's device and the click-through controller for the overlay window.
        /// Throws std::runtime_error when the sampler's staging textures cannot be created.
        /// <param name="bootstrap">Bootstrap whose back buffer is sampled; not owned, must outlive the
        /// controller.</param>
        /// <param name="windowHandle">The overlay window whose cursor position is mapped and whose click-through state
        /// is toggled; not owned.</param>
        /// <param name="commandLineOptions">Parsed command line that says whether this is a --hit-test-probe run and
        /// supplies the probe point; not owned, must outlive the controller.</param>
        Win32OverlayHitTestController(DirectX11Bootstrap& bootstrap, HWND windowHandle, const Win32CommandLineOptions& commandLineOptions);

        /// Runs one frame's hit test; must be called after the frame is drawn and before it is presented. Captures the
        /// pixel under the cursor, mapped to client coordinates, or the probe point in --hit-test-probe runs; when
        /// GetCursorPos fails (another desktop such as the lock screen is active) nothing is captured, the same as a
        /// cursor outside the window. Then reads the newest completed sample and applies click-through on for an alpha
        /// below ClickThroughAlphaThreshold and off otherwise. Probe runs go through the same real toggle and then
        /// remember the alpha and the window's resulting GWL_EXSTYLE for the HIT_TEST line. Throws std::runtime_error
        /// when ScreenToClient, the capture, the readback, the style toggle or the extended-style read fails.
        void SampleFrame();

        /// Samples the cursor for the idle-throttled loop: while click-through is on the window receives no mouse
        /// messages, so the activity tracker cannot see the cursor moving over transparent areas. Returns true when the
        /// cursor moved since the previous call and is now inside the overlay's window rectangle. When GetCursorPos
        /// fails (another desktop such as the lock screen is active) the cursor is not over the overlay, so it returns
        /// false. The first call compares against the origin, so it may report activity, which is harmless because the
        /// player starts in active mode. Throws std::runtime_error when GetWindowRect fails.
        /// <returns>True when the cursor movement counts as activity.</returns>
        bool ObserveCursorForActivity();

        /// Gets whether at least one --hit-test-probe readback completed, so the HIT_TEST line never reports a guessed
        /// alpha; always false outside probe runs.
        bool HasProbeAlpha() const;

        /// Returns the HIT_TEST line of a --hit-test-probe run:
        /// `HIT_TEST x=<x> y=<y> alpha=<a> clickThrough=<on|off> exStyle=0x<hex>`, where clickThrough is the state the
        /// click-through controller actually applied and exStyle is the window's GWL_EXSTYLE read after that toggle, as
        /// eight upper-case hexadecimal digits; or `HIT_TEST x=<x> y=<y> alpha=pending` when no readback completed.
        std::string DescribeProbeResult() const;

    private:
        /// Alpha, out of 255, below which a sampled pixel counts as transparent, so the mouse passes through to the
        /// window behind.
        static constexpr int ClickThroughAlphaThreshold = 8;

        /// Stores the overlay window whose cursor position is mapped and whose rectangle bounds cursor activity; not
        /// owned.
        HWND WindowHandle;

        /// Stores the parsed command line that selects the probe point in --hit-test-probe runs; not owned.
        const Win32CommandLineOptions& CommandLineOptions;

        /// Stores the sampler that reads the back-buffer alpha under the sample point without stalling.
        DirectX11HitTestSampler HitTestSampler;

        /// Stores the controller that toggles WS_EX_TRANSPARENT from the sampled alpha, for the cursor and for the
        /// --hit-test-probe point alike.
        Win32ClickThroughController ClickThroughController;

        /// Stores the newest alpha read back at the --hit-test-probe point; only meaningful once ProbeAlphaAvailable is
        /// true, and never written outside probe runs.
        int ProbeAlpha;

        /// Stores the overlay window's GWL_EXSTYLE read right after the newest probe toggle, so the HIT_TEST line shows
        /// whether WS_EX_TRANSPARENT really changed; only meaningful once ProbeAlphaAvailable is true.
        std::uint32_t ProbeExStyle;

        /// Tracks whether at least one --hit-test-probe readback completed; stays false outside probe runs.
        bool ProbeAlphaAvailable;

        /// Stores the last cursor position ObserveCursorForActivity saw, in screen coordinates; starts at the origin.
        POINT LastCursorPosition;
    };
}
