#include <Windows.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include "desktop_live_probe.hpp"
#include "platform/windows/win32/win32_desktop_host.hpp"
#include "platform/windows/directx11/directx11_bootstrap.hpp"

namespace helengine::windows {
    /// Exercises the production desktop resolver with process-owned shell substitutes, leaving Explorer untouched.
    class DesktopHostTests {
    public:
        /// Registers private test classes and checks raised, classic, malformed, detached, and destroyed desktop hosts.
        static void Run() {
            DPI_AWARENESS_CONTEXT previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            Require(previous != nullptr, "Unable to create per-monitor-aware test shell windows.");
            Register(L"Progman");
            Register(L"WorkerW");
            Register(L"SHELLDLL_DefView");
            Register(L"HelEngineDesktopTest");
            TestRaisedDesktop();
            TestClassicDesktop();
            ExpectInvalidProgramManager();
            std::vector<RECT> monitors = Win32DesktopHost::GetMonitorRectangles();
            Require(!monitors.empty(), "No physical monitor rectangles were returned.");
            Require(SetThreadDpiAwarenessContext(previous) != nullptr, "Unable to restore test DPI context.");
            for (const auto& bounds : monitors) {
                Require(bounds.right > bounds.left && bounds.bottom > bounds.top, "A monitor had invalid physical bounds.");
            }
        }

    private:
        /// Converts every failed invariant into a nonzero test exit with the precise reason.
        static void Require(bool value, const char* message) {
            if (!value) { throw std::runtime_error(message); }
        }

        /// Registers one substitute shell class in this process, independent of Explorer's real class registrations.
        static void Register(const wchar_t* name) {
            WNDCLASSEXW description {};
            description.cbSize = sizeof(description);
            description.lpfnWndProc = DefWindowProcW;
            description.hInstance = GetModuleHandleW(nullptr);
            description.lpszClassName = name;
            Require(RegisterClassExW(&description) != 0, "Unable to register a test window class.");
        }

        /// Creates a hidden, offscreen substitute owned entirely by the test process.
        static HWND Create(const wchar_t* className, HWND parent, DWORD extendedStyle = 0) {
            HWND window = CreateWindowExW(extendedStyle, className, L"Desktop host isolated test",
                parent == nullptr ? WS_POPUP : WS_CHILD, parent == nullptr ? -32000 : 0, parent == nullptr ? -32000 : 0,
                800, 600, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
            Require(window != nullptr, "Unable to create a test window.");
            return window;
        }

        /// Checks creation, opacity, DPI-context restoration, input exclusion, placement, and blt presentation.
        static HWND CheckChild(const Win32DesktopHost& host, HWND parent, HWND icons, HWND worker, bool raised) {
            DPI_AWARENESS_CONTEXT shellContext = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE);
            DPI_AWARENESS_CONTEXT previous = GetThreadDpiAwarenessContext();
            HWND foreground = GetForegroundWindow();
            RECT bounds { -32000, -32000, -31360, -31640 };
            HWND child = host.CreateChild(L"HelEngineDesktopTest", L"Owned test wallpaper", GetModuleHandleW(nullptr), nullptr, bounds);
            Require(GetParent(child) == parent, "Desktop child was attached to the wrong shell window.");
            Require(AreDpiAwarenessContextsEqual(previous, GetThreadDpiAwarenessContext()), "Desktop creation changed the caller's DPI context.");
            Require(AreDpiAwarenessContextsEqual(GetWindowDpiAwarenessContext(child), GetWindowDpiAwarenessContext(parent)),
                "Child and parent use different DPI contexts.");
            DWORD style = static_cast<DWORD>(GetWindowLongPtrW(child, GWL_STYLE));
            DWORD extendedStyle = static_cast<DWORD>(GetWindowLongPtrW(child, GWL_EXSTYLE));
            Require((style & WS_CHILD) != 0 && (style & WS_DISABLED) != 0, "Desktop windows must be disabled children.");
            Require((extendedStyle & (WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_LAYERED))
                == (WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_LAYERED), "Desktop input and layered styles were incomplete.");
            BYTE alpha = 0;
            DWORD flags = 0;
            Require(GetLayeredWindowAttributes(child, nullptr, &alpha, &flags) && alpha == 255 && flags == LWA_ALPHA,
                "Desktop windows must be initialized with opaque layered alpha.");
            RECT actualBounds = Win32DesktopHost::ReadPhysicalWindowRectangle(child);
            RECT client = Win32DesktopHost::ReadClientRectangle(child);
            Require(EqualRect(&bounds, &actualBounds) && client.right == 640 && client.bottom == 360,
                "Desktop child did not preserve requested physical bounds and client dimensions.");
            Require(AreDpiAwarenessContextsEqual(previous, GetThreadDpiAwarenessContext()), "Desktop bounds queries changed the caller's DPI context.");
            host.ShowChild(child);
            Require(GetForegroundWindow() == foreground, "Showing desktop wallpaper changed foreground focus.");
            Require(IsWindow(icons) && IsWindow(worker), "Creating wallpaper damaged a shell-owned substitute.");
            if (raised) {
                Require(GetWindow(child, GW_HWNDPREV) == icons && GetWindow(child, GW_HWNDNEXT) == worker,
                    "Raised wallpaper was not placed between icons and WorkerW.");
            }
            DirectX11Bootstrap first(child, 640, 360, false, true);
            DXGI_SWAP_CHAIN_DESC1 description {};
            Require(SUCCEEDED(first.GetSwapChain()->GetDesc1(&description)), "Unable to read desktop swap-chain settings.");
            Require(description.SwapEffect == DXGI_SWAP_EFFECT_DISCARD && description.BufferCount == 1
                && description.AlphaMode == DXGI_ALPHA_MODE_IGNORE, "Desktop children require opaque blt presentation.");
            const float clear[4] { 0.2f, 0.3f, 0.4f, 1.0f };
            first.GetDeviceContext()->ClearRenderTargetView(first.GetRenderTargetView(), clear);
            Require(SUCCEEDED(first.GetSwapChain()->Present(0, 0)), "Desktop blt presentation failed.");
            RECT nextBounds { -31360, -32000, -30720, -31640 };
            HWND secondChild = host.CreateChild(L"HelEngineDesktopTest", L"Second wallpaper", GetModuleHandleW(nullptr), nullptr, nextBounds);
            {
                DirectX11Bootstrap second(secondChild, 640, 360, false, first, true);
                Require(first.GetDevice() == second.GetDevice() && first.GetDeviceContext() == second.GetDeviceContext(),
                    "Desktop views did not share the primary device and context.");
                second.Resize(320, 180);
                Require(second.GetWidth() == 320 && second.GetHeight() == 180, "Desktop back-buffer resize failed.");
            }
            DestroyWindow(secondChild);
            Require(SetThreadDpiAwarenessContext(shellContext) != nullptr, "Unable to restore test shell DPI context.");
            return child;
        }

        /// Verifies the raised Progman arrangement and explicit failure when its wallpaper worker disappears.
        static void TestRaisedDesktop() {
            HWND programManager = Create(L"Progman", nullptr, WS_EX_NOREDIRECTIONBITMAP);
            HWND worker = Create(L"WorkerW", programManager);
            HWND icons = Create(L"SHELLDLL_DefView", programManager);
            Require(SetWindowPos(worker, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE), "Unable to order raised wallpaper host.");
            Require(SetWindowPos(icons, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE), "Unable to order raised icon host.");
            Win32DesktopHost host = Win32DesktopHost::ResolveFromProgramManager(programManager);
            HWND child = CheckChild(host, programManager, icons, worker, true);
            SetParent(child, nullptr);
            ExpectLostAttachment(host, child);
            DestroyWindow(child);
            Require(IsWindow(programManager) && IsWindow(icons) && IsWindow(worker), "Child cleanup destroyed shell substitutes.");
            DestroyWindow(worker);
            bool rejected = false;
            try { Win32DesktopHost::ResolveFromProgramManager(programManager); }
            catch (const std::runtime_error&) { rejected = true; }
            Require(rejected, "A raised desktop without WorkerW was accepted.");
            DestroyWindow(programManager);
        }

        /// Verifies a classic worker behind the icon host and the loss of a destroyed shell parent.
        static void TestClassicDesktop() {
            HWND programManager = Create(L"Progman", nullptr);
            HWND icons = Create(L"SHELLDLL_DefView", programManager);
            HWND worker = Create(L"WorkerW", nullptr);
            Require(SetWindowPos(programManager, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE),
                "Unable to order the classic icon host.");
            Require(SetWindowPos(worker, programManager, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE),
                "Unable to order the classic wallpaper host.");
            Win32DesktopHost host = Win32DesktopHost::ResolveFromProgramManager(programManager);
            HWND child = CheckChild(host, worker, icons, worker, false);
            DestroyWindow(worker);
            ExpectLostAttachment(host, child);
            DestroyWindow(programManager);
        }

        /// Requires shell loss to reach the caller instead of silently reparenting or rebuilding the window.
        static void ExpectLostAttachment(const Win32DesktopHost& host, HWND child) {
            bool rejected = false;
            try { host.ValidateAttachment(child); }
            catch (const std::runtime_error&) { rejected = true; }
            Require(rejected, "A lost shell attachment did not fail explicitly.");
        }

        /// Requires valid program manager input before topology discovery begins.
        static void ExpectInvalidProgramManager() {
            bool rejected = false;
            try { Win32DesktopHost::ResolveFromProgramManager(nullptr); }
            catch (const std::invalid_argument&) { rejected = true; }
            Require(rejected, "A missing program manager was accepted.");
        }
    };
}

/// Reports behavioral desktop-host failures as a nonzero native test result.
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--live-shell") { helengine::windows::DesktopLiveProbe::Run(); return 0; }
        if (argc != 1) { throw std::invalid_argument("Desktop tests accept only the optional --live-shell probe."); }
        helengine::windows::DesktopHostTests::Run(); std::cout << "PASS native desktop host tests\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
