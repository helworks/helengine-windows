#pragma once

#include <Windows.h>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
#include "platform/windows/win32/win32_desktop_host.hpp"
#include "platform/windows/win32/win32_window.hpp"
#include "platform/windows/directx11/directx11_bootstrap.hpp"
#include "platform/windows/directx11/directx11_host_fingerprint.hpp"

namespace helengine::windows {
    /// Performs an explicitly selected, finite live wallpaper check using metadata and owned DirectX surfaces only.
    /// It never reads screen pixels, moves the cursor, or mutates Explorer-owned windows.
    class DesktopLiveProbe {
    public:
        /// Covers startup displays for thirty shared-device frames and verifies focus, layering, sizing, and cleanup.
        static void Run() {
            HWND foreground = GetForegroundWindow();
            DPI_AWARENESS_CONTEXT caller = GetThreadDpiAwarenessContext();
            std::vector<RECT> monitors = Win32DesktopHost::GetMonitorRectangles();
            std::vector<HWND> parents;
            {
                std::vector<std::unique_ptr<Win32Window>> windows;
                std::vector<std::unique_ptr<DirectX11Bootstrap>> surfaces;
                std::vector<std::unique_ptr<DirectX11HostFingerprint>> fingerprints;
                for (size_t index = 0; index < monitors.size(); index++) {
                    const RECT& bounds = monitors[index];
                    auto window = std::make_unique<Win32Window>(L"HelEngine Desktop Acceptance", bounds.left, bounds.top,
                        bounds.right - bounds.left, bounds.bottom - bounds.top, Win32WindowStyle::Desktop());
                    window->SetSecondaryWindow();
                    window->Create();
                    window->ShowSecondary();
                    window->RethrowPendingException();
                    window->ValidateDesktopAttachment();
                    Require(GetForegroundWindow() == foreground, "Live desktop creation stole foreground focus.");
                    Require(AreDpiAwarenessContextsEqual(caller, GetThreadDpiAwarenessContext()), "Live desktop creation changed thread DPI awareness.");
                    RECT actual = Win32DesktopHost::ReadPhysicalWindowRectangle(window->GetHandle());
                    Require(EqualRect(&actual, &bounds), "Live desktop did not cover the physical monitor rectangle.");
                    Require(window->GetClientWidth() == bounds.right - bounds.left && window->GetClientHeight() == bounds.bottom - bounds.top,
                        "Live desktop client dimensions differed from the monitor pixel dimensions.");
                    HWND parent = GetParent(window->GetHandle());
                    parents.push_back(parent);
                    ValidateIconLayer(parent, window->GetHandle());
                    auto surface = index == 0
                        ? std::make_unique<DirectX11Bootstrap>(window->GetHandle(), window->GetClientWidth(), window->GetClientHeight(), false, true)
                        : std::make_unique<DirectX11Bootstrap>(window->GetHandle(), window->GetClientWidth(), window->GetClientHeight(), false, *surfaces.front(), true);
                    fingerprints.push_back(std::make_unique<DirectX11HostFingerprint>(*surface, window->GetHandle(), "desktop-monitor-" + std::to_string(index + 1)));
                    windows.push_back(std::move(window));
                    surfaces.push_back(std::move(surface));
                }
                for (int frame = 0; frame < 30; frame++) {
                    MSG message {};
                    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
                    for (size_t index = 0; index < surfaces.size(); index++) {
                        windows[index]->RethrowPendingException();
                        windows[index]->ValidateDesktopAttachment();
                        const float color[4] { 0.04f, 0.06f, 0.09f, 1.0f };
                        surfaces[index]->GetDeviceContext()->ClearRenderTargetView(surfaces[index]->GetRenderTargetView(), color);
                        HRESULT present = surfaces[index]->GetSwapChain()->Present(index == 0 ? 1 : 0, 0);
                        Require(SUCCEEDED(present), "Live desktop presentation failed.");
                        fingerprints[index]->RecordPresent(present);
                    }
                }
                for (const auto& fingerprint : fingerprints) {
                    std::cout << fingerprint->Describe(30, false, 0, 30, Win32WindowMode::Desktop) << '\n';
                }
                Require(GetForegroundWindow() == foreground, "Live desktop rendering changed foreground focus.");
            }
            for (HWND parent : parents) { Require(IsWindow(parent), "Player cleanup destroyed an Explorer-owned parent."); }
            Require(GetForegroundWindow() == foreground, "Live desktop cleanup changed foreground focus.");
            std::cout << "PASS live desktop probe monitors=" << monitors.size() << " frames=30 focus=preserved cleanup=preserved\n";
        }

    private:
        /// Fails the probe at the first violated native invariant instead of recording an assumed success.
        static void Require(bool condition, const char* message) {
            if (!condition) { throw std::runtime_error(message); }
        }

        /// Verifies the raised layout's icons precede the owned child and its WorkerW follows it in sibling order.
        /// The classic layout is already resolved and validated by the production desktop host.
        static void ValidateIconLayer(HWND parent, HWND child) {
            if ((GetWindowLongPtrW(parent, GWL_EXSTYLE) & WS_EX_NOREDIRECTIONBITMAP) == 0) { return; }
            HWND icons = FindWindowExW(parent, nullptr, L"SHELLDLL_DefView", nullptr);
            HWND worker = FindWindowExW(parent, nullptr, L"WorkerW", nullptr);
            bool iconsSeen = false;
            bool childSeen = false;
            for (HWND sibling = GetTopWindow(parent); sibling != nullptr; sibling = GetWindow(sibling, GW_HWNDNEXT)) {
                if (sibling == icons) { iconsSeen = true; }
                if (sibling == child) { Require(iconsSeen, "Live wallpaper covered the desktop icon layer."); childSeen = true; }
                if (sibling == worker) { Require(childSeen, "Live wallpaper was hidden below Explorer's wallpaper worker."); return; }
            }
            throw std::runtime_error("Live desktop sibling order omitted a required shell layer.");
        }
    };
}
