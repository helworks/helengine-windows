#pragma once

#include <memory>
#include <string>
#include "platform/windows/directx11/directx11_bootstrap.hpp"
#include "platform/windows/directx11/directx11_presenter.hpp"
#include "platform/windows/directx11/directx11_back_buffer_capture.hpp"
#include "platform/windows/directx11/directx11_host_fingerprint.hpp"
#include "platform/windows/win32/win32_additional_window_settings.hpp"
#include "platform/windows/win32/win32_command_line_options.hpp"
#include "platform/windows/win32/win32_overlay_hit_test_controller.hpp"
#include "platform/windows/win32/win32_window.hpp"

namespace helengine::windows {
    class Win32ActivityTracker;

    /// Owns one secondary window's presentation and diagnostics while sharing the process device and engine.
    class Win32SecondaryWindow {
    public:
        /// Creates a view without activating it, on the primary window's shared graphics device.
        Win32SecondaryWindow(const Win32AdditionalWindowSettings& settings, DirectX11Bootstrap& sharedDevice,
            Win32DpiAwareness awareness, Win32ActivityTracker* activity, const Win32CommandLineOptions& options);
        /// Detaches the borrowed activity tracker before destruction dispatches window messages.
        ~Win32SecondaryWindow();
        /// Gets the native wrapper used for message and input routing.
        Win32Window& GetWindow();
        /// Gets this view's shared-device presentation surface.
        DirectX11Bootstrap& GetBootstrap();
        /// Gets the configured stable window tag.
        const std::string& GetTag() const;
        /// Gets the presentation mode selected before native creation.
        Win32WindowMode GetMode() const;
        /// Gets the original handle used by the portable window registry, even after WM_DESTROY.
        HWND GetRegisteredHandle() const;
        /// Reports cursor activity over a transparent overlay, where ordinary mouse messages are unavailable.
        bool ObserveCursorForActivity();
        /// Samples overlay alpha, optionally captures the final frame, then presents and counts this view's frame.
        void CompleteFrame(bool synchronize, bool idle, bool finalFrame);
        /// Builds this view's final fingerprint from its own presented-frame counters.
        std::string DescribeFingerprint(bool idleEnabled) const;
        /// Reports whether this overlay has a deterministic hit-test probe result to log.
        bool HasHitTestProbe() const;
        /// Builds the probe result with the window tag appended for multi-window diagnostics.
        std::string DescribeProbeResult() const;
    private:
        /// Stores this view's validated configuration.
        Win32AdditionalWindowSettings Settings;
        /// Owns the window; declared before its graphics resources so it outlives them.
        std::unique_ptr<Win32Window> Window;
        /// Stores the registered handle independently of the wrapper's destruction state.
        HWND RegisteredHandle;
        /// Owns this window's swap chain and depth buffer, retaining the shared device.
        std::unique_ptr<DirectX11Bootstrap> Bootstrap;
        /// Presents this window's swap chain.
        std::unique_ptr<DirectX11Presenter> Presenter;
        /// Owns optional back-buffer capture resources.
        std::unique_ptr<DirectX11BackBufferCapture> Capture;
        /// Owns optional finite-run diagnostics.
        std::unique_ptr<DirectX11HostFingerprint> Fingerprint;
        /// Owns this overlay's independent alpha sampler and click-through controller.
        std::unique_ptr<Win32OverlayHitTestController> HitTest;
        /// Stores this view's capture path with its safe tag inserted before the extension.
        std::wstring CapturePath;
        /// Counts actual presentations for this view, independently of minimized windows.
        int PresentedFrames = 0;
        /// Counts this view's idle presentations.
        int IdleFrames = 0;
        /// Counts this view's active presentations.
        int ActiveFrames = 0;
        /// Records whether the finite run requested a hit-test probe.
        bool ProbeRequested = false;
    };
}
