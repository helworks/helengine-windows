#include <Windows.h>
#include <iostream>
#include <stdexcept>
#include "platform/windows/win32/win32_pointer_window_routing.hpp"
#include "platform/windows/win32/win32_pointer_button_filter.hpp"
#include "platform/windows/win32/win32_window.hpp"
#include "platform/windows/win32/win32_click_through_controller.hpp"
#include "platform/windows/directx11/directx11_bootstrap.hpp"

namespace helengine::windows {
    /// Checks gesture transitions by default and reserves real native overlay windows for explicit opt-in runs.
    class PointerWindowRoutingTests {
    public:
        /// Checks physical gesture filtering without creating windows or initializing a graphics device.
        static void RunHeadless() { TestButtonTransitions(); }

        /// Checks opaque, transparent, hidden, foreign and child-window hits on a nonactivating overlay.
        static void Run() {
            TestButtonTransitions();
            HWND foreground = GetForegroundWindow();
            WNDCLASSW description {};
            description.lpfnWndProc = DefWindowProcW;
            description.hInstance = GetModuleHandleW(nullptr);
            description.lpszClassName = L"HelEnginePointerRoutingTests";
            Require(RegisterClassW(&description) != 0, "Unable to register pointer test windows.");
            TestNativeOverlay(description);
            HWND overlay = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, description.lpszClassName,
                L"Passive overlay", WS_POPUP, -32000, -32000, 64, 64, nullptr, nullptr, description.hInstance, nullptr);
            Require(overlay != nullptr, "Unable to create passive overlay.");
            ShowWindow(overlay, SW_SHOWNOACTIVATE);
            Require(Win32PointerWindowRouting::IsPointerTarget(overlay, overlay), "A visible passive overlay must receive its own pointer hits.");
            HWND child = CreateWindowExW(0, description.lpszClassName, L"Owned child", WS_CHILD | WS_VISIBLE,
                0, 0, 16, 16, overlay, nullptr, description.hInstance, nullptr);
            Require(child != nullptr, "Unable to create overlay child.");
            Require(Win32PointerWindowRouting::IsPointerTarget(overlay, child), "Child hits must route to their registered root view.");
            Require(!Win32PointerWindowRouting::IsPointerTarget(overlay, GetDesktopWindow()), "Foreign windows must not route to an overlay.");
            Require(!Win32PointerWindowRouting::IsPointerTarget(overlay, nullptr), "An absent hit must not retain a stale hover.");
            SetWindowLongPtrW(overlay, GWL_EXSTYLE, GetWindowLongPtrW(overlay, GWL_EXSTYLE) | WS_EX_TRANSPARENT);
            Require(!Win32PointerWindowRouting::IsPointerTarget(overlay, overlay), "Click-through overlays must not receive passive input.");
            SetWindowLongPtrW(overlay, GWL_EXSTYLE, GetWindowLongPtrW(overlay, GWL_EXSTYLE) & ~WS_EX_TRANSPARENT);
            ShowWindow(overlay, SW_HIDE);
            Require(!Win32PointerWindowRouting::IsPointerTarget(overlay, overlay), "Hidden overlays must not retain pointer input.");
            DestroyWindow(overlay);
            Require(!Win32PointerWindowRouting::IsPointerTarget(overlay, overlay), "Destroyed views must not receive input.");
            Require(GetForegroundWindow() == foreground, "Passive pointer routing must not change keyboard focus.");
            UnregisterClassW(description.lpszClassName, description.hInstance);
        }
    private:
        /// Verifies actual User32 hit ownership above another owned window using the production overlay and controller.
        static void TestNativeOverlay(const WNDCLASSW& description) {
            HWND foreground = GetForegroundWindow();
            MONITORINFO monitor { sizeof(MONITORINFO) };
            Require(GetMonitorInfoW(MonitorFromWindow(foreground, MONITOR_DEFAULTTOPRIMARY), &monitor), "Unable to locate a monitor for native overlay hit tests.");
            int left = monitor.rcWork.left + 8;
            int top = monitor.rcWork.top + 8;
            HWND underneath = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                description.lpszClassName, L"Owned pointer target", WS_POPUP, left, top, 96, 64,
                nullptr, nullptr, description.hInstance, nullptr);
            Require(underneath != nullptr, "Unable to create the isolated underlying window.");
            ShowWindow(underneath, SW_SHOWNOACTIVATE);
            POINT probe { left + 32, top + 32 };
            Require(WindowFromPoint(probe) == underneath, "The isolated underlying window must own the probe before creating the glass.");
            {
                Win32Window glass(L"Owned glass hit test", left, top, 96, 64, Win32WindowStyle::Overlay());
                glass.SetSecondaryWindow();
                glass.Create();
                BYTE layeredAlpha = 0;
                DWORD layeredFlags = 0;
                Require(GetLayeredWindowAttributes(glass.GetHandle(), nullptr, &layeredAlpha, &layeredFlags)
                    && layeredAlpha == 255 && layeredFlags == LWA_ALPHA,
                    "Glass must initialize its native layered opacity before showing DirectComposition content.");
                glass.ShowSecondary();
                SetWindowLongPtrW(glass.GetHandle(), GWL_EXSTYLE, GetWindowLongPtrW(glass.GetHandle(), GWL_EXSTYLE) | WS_EX_NOACTIVATE);
                DirectX11Bootstrap composition(glass.GetHandle(), 96, 64, true);
                const float opaque[] = { 0.2f, 0.3f, 0.4f, 1.0f };
                for (int frame = 0; frame < 4; frame++) {
                    composition.GetDeviceContext()->ClearRenderTargetView(composition.GetRenderTargetView(), opaque);
                    Require(SUCCEEDED(composition.GetSwapChain()->Present(1, 0)), "Unable to present the synthetic glass.");
                }
                Win32ClickThroughController clicks(glass.GetHandle());
                Require(WindowFromPoint(probe) == underneath, "A newly transparent glass must let input reach the owned window below.");
                clicks.Apply(false);
                HWND hit = WindowFromPoint(probe);
                std::cout << "Native glass hit: glass=" << glass.GetHandle() << " hit=" << hit
                    << " exStyle=" << std::hex << GetWindowLongPtrW(glass.GetHandle(), GWL_EXSTYLE) << std::dec << '\n';
                Require(hit == glass.GetHandle(), "Opaque glass must own the real Windows hit, not merely clear WS_EX_TRANSPARENT.");
                Require(Win32PointerWindowRouting::IsPointerTarget(glass.GetHandle(), hit), "The native glass hit must route to its engine view.");
                clicks.Apply(true);
                Require(WindowFromPoint(probe) == underneath, "Transparent glass must restore the real underlying input target.");
                clicks.Apply(false);
                Require(WindowFromPoint(probe) == glass.GetHandle(), "Reopened opaque glass must receive native input again.");
            }
            DestroyWindow(underneath);
            Require(GetForegroundWindow() == foreground, "Native glass hit testing must preserve the foreground keyboard window.");
        }

        /// Rejects external held-button entry and abandons old gestures while allowing fresh overlay clicks.
        static void TestButtonTransitions() {
            HWND outside = reinterpret_cast<HWND>(1);
            HWND overlay = reinterpret_cast<HWND>(2);
            Win32PointerButtonFilter buttons;
            Require(buttons.Capture(0, false, outside) == 0, "Idle external input must remain released.");
            Require(buttons.Capture(1, false, outside) == 0, "External presses must remain released.");
            Require(buttons.Capture(1, true, overlay) == 0, "Entering a passive overlay with a held button must not synthesize a press.");
            Require(buttons.Capture(0, true, overlay) == 0, "An external gesture must not synthesize an overlay release.");
            Require(buttons.Capture(1, true, overlay) == 1, "A fresh press inside the overlay must be accepted.");
            Require(buttons.Capture(1, false, outside) == 0, "Leaving the overlay must abandon its captured gesture.");
            Require(buttons.Capture(1, true, overlay) == 0, "Reentering must not resurrect an abandoned gesture.");
            Require(buttons.Capture(0, true, overlay) == 0, "Releasing an abandoned gesture must remain inactive.");
            Require(buttons.Capture(2, true, overlay) == 2, "Other mouse buttons must accept their own fresh presses.");
            Require(buttons.Capture(2, true, outside) == 0, "Changing between eligible views must not transfer a held press.");
            Require(buttons.Capture(0, true, outside) == 0, "Transferred presses must stay suppressed through release.");
            Require(buttons.Capture(3, true, outside) == 3, "Independent fresh buttons must remain usable after suppression clears.");
            Win32PointerButtonFilter startup;
            Require(startup.Capture(1, true, overlay) == 0, "Starting the backend must not adopt an already-held gesture.");
        }

        /// Reports failed input ownership invariants through the test process exit code.
        static void Require(bool condition, const char* message) {
            if (!condition) { throw std::runtime_error(message); }
        }
    };
}

/// Runs windowless gesture checks by default; --native-windows explicitly enables native overlay and graphics checks.
int main(int argc, char** argv) {
    try {
        if (argc == 1 || (argc == 2 && std::string(argv[1]) == "--headless")) {
            helengine::windows::PointerWindowRoutingTests::RunHeadless();
            std::cout << "PASS headless pointer gesture transitions (no windows or graphics device)\n";
            return 0;
        }
        if (argc != 2 || std::string(argv[1]) != "--native-windows") {
            throw std::invalid_argument("Pointer tests accept --headless or the explicit --native-windows opt-in.");
        }
        helengine::windows::PointerWindowRoutingTests::Run(); std::cout << "PASS passive pointer window routing\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
