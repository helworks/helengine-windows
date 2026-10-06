#include "platform/windows/win32/win32_secondary_window.hpp"

#include <filesystem>
#include <stdexcept>
#include "platform/windows/win32/win32_exit_request.hpp"

namespace helengine::windows {
    /// Initializes a complete presentation session using the primary device and the process DPI policy.
    Win32SecondaryWindow::Win32SecondaryWindow(const Win32AdditionalWindowSettings& settings, DirectX11Bootstrap& sharedDevice,
        Win32DpiAwareness awareness, Win32ActivityTracker* activity, const Win32CommandLineOptions& options)
        : Settings(settings), RegisteredHandle(nullptr) {
        std::wstring title(settings.GetTag().begin(), settings.GetTag().end());
        Window = std::make_unique<Win32Window>(title.c_str(), settings.GetLeft(), settings.GetTop(), settings.GetWidth(),
            settings.GetHeight(), settings.GetMode() == Win32WindowMode::Overlay ? Win32WindowStyle::Overlay() : Win32WindowStyle::Normal());
        Window->SetSecondaryWindow();
        Window->SetDpiAwareness(awareness);
        Window->SetActivityTracker(activity);
        Window->Create();
        RegisteredHandle = Window->GetHandle();
        Window->ShowSecondary();
        Window->RethrowPendingException();
        Bootstrap = std::make_unique<DirectX11Bootstrap>(RegisteredHandle, Window->GetClientWidth(), Window->GetClientHeight(),
            settings.GetMode() == Win32WindowMode::Overlay, sharedDevice);
        Presenter = std::make_unique<DirectX11Presenter>(*Bootstrap);
        if (options.HasFrameLimit()) {
            Fingerprint = std::make_unique<DirectX11HostFingerprint>(*Bootstrap, RegisteredHandle, settings.GetTag());
        }
        if (options.HasCapturePath()) {
            std::filesystem::path mainPath(options.GetCapturePath());
            CapturePath = (mainPath.parent_path() / (mainPath.stem().wstring() + L"." + title + mainPath.extension().wstring())).wstring();
            Capture = std::make_unique<DirectX11BackBufferCapture>(*Bootstrap);
        }
        if (settings.GetMode() == Win32WindowMode::Overlay) {
            HitTest = std::make_unique<Win32OverlayHitTestController>(*Bootstrap, RegisteredHandle, options);
            ProbeRequested = options.HasHitTestProbe();
        }
    }

    /// Prevents destruction messages from accessing a tracker already destroyed by the host.
    Win32SecondaryWindow::~Win32SecondaryWindow() { Window->SetActivityTracker(nullptr); }
    /// Gets the native window wrapper.
    Win32Window& Win32SecondaryWindow::GetWindow() { return *Window; }
    /// Gets the view's presentation surface.
    DirectX11Bootstrap& Win32SecondaryWindow::GetBootstrap() { return *Bootstrap; }
    /// Gets the diagnostic tag.
    const std::string& Win32SecondaryWindow::GetTag() const { return Settings.GetTag(); }
    /// Gets the selected native mode.
    Win32WindowMode Win32SecondaryWindow::GetMode() const { return Settings.GetMode(); }
    /// Gets the handle registered before the view could receive WM_DESTROY.
    HWND Win32SecondaryWindow::GetRegisteredHandle() const { return RegisteredHandle; }
    /// Tracks overlay cursor motion without moving the user's cursor.
    bool Win32SecondaryWindow::ObserveCursorForActivity() { return HitTest && HitTest->ObserveCursorForActivity(); }
    /// Completes one drawn view and performs its own optional capture before presentation.
    void Win32SecondaryWindow::CompleteFrame(bool synchronize, bool idle, bool finalFrame) {
        if (HitTest) { HitTest->SampleFrame(); }
        if (Capture && finalFrame) {
            try { Capture->CaptureToBmp(CapturePath); }
            catch (const std::exception& error) {
                throw Win32ExitRequest(3, "Capture failed for window " + Settings.GetTag() + ": " + error.what());
            }
        }
        HRESULT result = synchronize ? Presenter->RenderFrame() : Presenter->RenderFrameWithoutWait();
        if (FAILED(result)) {
            throw std::runtime_error("Present failed for window " + Settings.GetTag() + ": " + DirectX11HostFingerprint::DescribePresentFailure(result));
        }
        if (Fingerprint) {
            Fingerprint->RecordPresent(result);
            PresentedFrames++;
            if (idle) { IdleFrames++; } else { ActiveFrames++; }
        }
    }
    /// Describes only the presentations that this view actually completed.
    std::string Win32SecondaryWindow::DescribeFingerprint(bool idleEnabled) const {
        if (!Fingerprint) { throw std::logic_error("Window fingerprints require --frames."); }
        return Fingerprint->Describe(PresentedFrames, idleEnabled, IdleFrames, ActiveFrames, Settings.GetMode());
    }
    /// Reports whether this view must provide a deterministic probe result.
    bool Win32SecondaryWindow::HasHitTestProbe() const { return ProbeRequested; }
    /// Fails when the requested probe never completed instead of reporting an assumed alpha.
    std::string Win32SecondaryWindow::DescribeProbeResult() const {
        if (!ProbeRequested || !HitTest || !HitTest->HasProbeAlpha()) {
            throw Win32ExitRequest(3, "No completed hit-test probe for window " + Settings.GetTag());
        }
        return HitTest->DescribeProbeResult() + " window=" + Settings.GetTag();
    }
}
