#include "platform/windows/win32/win32_overlay_hit_test_controller.hpp"

#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace helengine::windows {
    /// Creates the sampler on the bootstrap's device and the click-through controller for the overlay window. Throws
    /// std::runtime_error when the sampler's staging textures cannot be created.
    Win32OverlayHitTestController::Win32OverlayHitTestController(DirectX11Bootstrap& bootstrap, HWND windowHandle, const Win32CommandLineOptions& commandLineOptions)
        : WindowHandle(windowHandle),
          CommandLineOptions(commandLineOptions),
          HitTestSampler(bootstrap),
          ClickThroughController(windowHandle),
          ProbeAlpha(0),
          ProbeExStyle(0),
          ProbeAlphaAvailable(false),
          LastCursorPosition() {
    }

    /// Runs one frame's hit test; must be called after the frame is drawn and before it is presented. Captures the
    /// pixel under the cursor, mapped to client coordinates, or the probe point in --hit-test-probe runs; when
    /// GetCursorPos fails (another desktop such as the lock screen is active) nothing is captured, the same as a cursor
    /// outside the window. Then reads the newest completed sample and applies click-through on for an alpha below
    /// ClickThroughAlphaThreshold and off otherwise. Probe runs go through the same real toggle and then remember the
    /// alpha and the window's resulting GWL_EXSTYLE for the HIT_TEST line. Throws std::runtime_error when
    /// ScreenToClient, the capture, the readback, the style toggle or the extended-style read fails.
    void Win32OverlayHitTestController::SampleFrame() {
        POINT samplePoint {};
        bool hasSamplePoint = false;
        if (CommandLineOptions.HasHitTestProbe()) {
            samplePoint.x = CommandLineOptions.GetHitTestProbeX();
            samplePoint.y = CommandLineOptions.GetHitTestProbeY();
            hasSamplePoint = true;
        } else if (GetCursorPos(&samplePoint)) {
            if (!ScreenToClient(WindowHandle, &samplePoint)) {
                throw std::runtime_error("ScreenToClient failed for the overlay hit test with error " + std::to_string(GetLastError()) + ".");
            }
            hasSamplePoint = true;
        }
        if (hasSamplePoint) {
            HitTestSampler.Capture(samplePoint.x, samplePoint.y);
        }

        int sampledAlpha = 0;
        if (HitTestSampler.TryReadLatestAlpha(sampledAlpha)) {
            if (CommandLineOptions.HasHitTestProbe()) {
                ClickThroughController.Apply(sampledAlpha < ClickThroughAlphaThreshold);
                // GetWindowLongPtrW reports a failure as 0 with a non-zero last error, so the last error is cleared
                // first to tell a failure from a style value of 0.
                SetLastError(0);
                LONG_PTR probeExStyle = GetWindowLongPtrW(WindowHandle, GWL_EXSTYLE);
                if (probeExStyle == 0 && GetLastError() != 0) {
                    throw std::runtime_error("GetWindowLongPtrW(GWL_EXSTYLE) failed for the overlay hit-test probe with Win32 error " + std::to_string(GetLastError()) + ".");
                }
                ProbeAlpha = sampledAlpha;
                ProbeExStyle = static_cast<std::uint32_t>(probeExStyle);
                ProbeAlphaAvailable = true;
            } else {
                ClickThroughController.Apply(sampledAlpha < ClickThroughAlphaThreshold);
            }
        }
    }

    /// Samples the cursor for the idle-throttled loop: while click-through is on the window receives no mouse
    /// messages, so the activity tracker cannot see the cursor moving over transparent areas. Returns true when the
    /// cursor moved since the previous call and is now inside the overlay's window rectangle. When GetCursorPos fails
    /// (another desktop such as the lock screen is active) the cursor is not over the overlay, so it returns false. The
    /// first call compares against the origin, so it may report activity, which is harmless because the player starts
    /// in active mode. Throws std::runtime_error when GetWindowRect fails.
    bool Win32OverlayHitTestController::ObserveCursorForActivity() {
        POINT cursorPosition {};
        if (!GetCursorPos(&cursorPosition)) {
            return false;
        }

        bool cursorMoved = cursorPosition.x != LastCursorPosition.x || cursorPosition.y != LastCursorPosition.y;
        LastCursorPosition = cursorPosition;
        if (!cursorMoved) {
            return false;
        }

        RECT windowRectangle {};
        if (!GetWindowRect(WindowHandle, &windowRectangle)) {
            throw std::runtime_error("GetWindowRect failed in the idle-throttled loop with error " + std::to_string(GetLastError()) + ".");
        }
        return PtInRect(&windowRectangle, cursorPosition) != FALSE;
    }

    /// Gets whether at least one --hit-test-probe readback completed, so the HIT_TEST line never reports a guessed
    /// alpha; always false outside probe runs.
    bool Win32OverlayHitTestController::HasProbeAlpha() const {
        return ProbeAlphaAvailable;
    }

    /// Returns the HIT_TEST line of a --hit-test-probe run:
    /// `HIT_TEST x=<x> y=<y> alpha=<a> clickThrough=<on|off> exStyle=0x<hex>`, where clickThrough is the state the
    /// click-through controller actually applied and exStyle is the window's GWL_EXSTYLE read after that toggle, as
    /// eight upper-case hexadecimal digits; or `HIT_TEST x=<x> y=<y> alpha=pending` when no readback completed.
    std::string Win32OverlayHitTestController::DescribeProbeResult() const {
        std::ostringstream hitTestBuilder;
        hitTestBuilder << "HIT_TEST x=" << CommandLineOptions.GetHitTestProbeX() << " y=" << CommandLineOptions.GetHitTestProbeY();
        if (!ProbeAlphaAvailable) {
            hitTestBuilder << " alpha=pending";
            return hitTestBuilder.str();
        }

        hitTestBuilder << " alpha=" << ProbeAlpha << " clickThrough=" << (ClickThroughController.IsClickThrough() ? "on" : "off")
                       << " exStyle=0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << ProbeExStyle;
        return hitTestBuilder.str();
    }
}
