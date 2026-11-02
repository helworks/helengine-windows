#include <Windows.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <DirectXPackedVector.h>
#include "platform/windows/directx11/directx11_bootstrap.hpp"
#include "platform/windows/directx11/directx11_hit_test_sampler.hpp"
#include "platform/windows/directx11/directx11_output_pixel_conversion.hpp"

namespace helengine::windows {
    /// Exercises production HDR composition using hidden process-owned windows and synthetic pixels, without desktop capture.
    class NativeHdrCompositionTests {
        /// Identifies a window class owned exclusively by this fixture.
        static constexpr const wchar_t* ClassName = L"HelEngineHiddenHdrCompositionTests";
        /// Owns an invisible substitute parent; no Explorer HWND is used.
        HWND Parent = nullptr;
        /// Owns the invisible layered disabled desktop child.
        HWND Desktop = nullptr;
        /// Owns the invisible layered glass popup.
        HWND Glass = nullptr;
        /// Tracks class registration for exception-safe cleanup.
        ATOM RegisteredClass = 0;
        /// Restores the caller's DPI context after hidden fixture teardown.
        DPI_AWARENESS_CONTEXT PreviousDpi = nullptr;

    public:
        /// Releases only fixture-owned windows and class registrations, including when a graphics assertion fails.
        ~NativeHdrCompositionTests() {
            if (Glass) { DestroyWindow(Glass); }
            if (Desktop) { DestroyWindow(Desktop); }
            if (Parent) { DestroyWindow(Parent); }
            if (RegisteredClass) { UnregisterClassW(ClassName, GetModuleHandleW(nullptr)); }
            if (PreviousDpi) { SetThreadDpiAwarenessContext(PreviousDpi); }
        }

        /// Validates real desktop/glass swapchains, shared-device MSAA resolve, alpha readback and format-preserving resize.
        void Run() {
            HWND foreground = GetForegroundWindow();
            CreateHiddenWindows();
            DirectX11Bootstrap desktop(Desktop, 32, 24, false, true, 4);
            DirectX11Bootstrap glass(Glass, 32, 24, true, desktop, false, 4);
            Require(desktop.GetDevice() == glass.GetDevice() && desktop.GetDeviceContext() == glass.GetDeviceContext(),
                "Desktop and Glass must share the actual device and immediate context.");
            VerifyOutput(desktop, Desktop, true);
            VerifyOutput(glass, Glass, false);
            DirectX11HitTestSampler desktopSampler(desktop);
            DirectX11HitTestSampler glassSampler(glass);
            for (float alpha : {0.0f, 0.25f, 0.5f, 1.0f}) {
                ClearAndResolve(desktop, alpha);
                VerifySyntheticPixel(desktop, alpha);
                VerifySampler(desktop, desktopSampler, alpha);
                ClearAndResolve(glass, alpha);
                VerifySyntheticPixel(glass, alpha);
                VerifySampler(glass, glassSampler, alpha);
                VerifySyntheticPixel(desktop, alpha);
            }
            desktop.Resize(47, 19);
            glass.Resize(29, 37);
            Require(desktop.GetWidth() == 47 && desktop.GetHeight() == 19 && glass.GetWidth() == 29 && glass.GetHeight() == 37,
                "Resize must update each output's independent dimensions.");
            desktop.RefreshOutputColorState();
            glass.RefreshOutputColorState();
            VerifyOutput(desktop, Desktop, true);
            VerifyOutput(glass, Glass, false);
            ClearAndResolve(desktop, 0.25f);
            VerifySampler(desktop, desktopSampler, 0.25f);
            ClearAndResolve(glass, 0.5f);
            VerifySampler(glass, glassSampler, 0.5f);
            VerifySyntheticPixel(desktop, 0.25f);
            VerifySyntheticPixel(glass, 0.5f);
            VerifyHiddenWindows();
            Require(GetForegroundWindow() == foreground, "Hidden graphics validation must preserve foreground focus.");
            std::cout << "PASS hidden native composition: Desktop/Glass, shared device, HDR format/color-space support, 4x resolve, alpha 0/.25/.5/1 and resize\n";
        }

    private:
        /// Creates hidden windows over the primary monitor so read-only monitor discovery uses a real active output.
        void CreateHiddenWindows() {
            PreviousDpi = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            Require(PreviousDpi != nullptr, "Cannot enter per-monitor-aware hidden fixture context.");
            WNDCLASSEXW description {};
            description.cbSize = sizeof(description);
            description.lpfnWndProc = DefWindowProcW;
            description.hInstance = GetModuleHandleW(nullptr);
            description.lpszClassName = ClassName;
            RegisteredClass = RegisterClassExW(&description);
            Require(RegisteredClass != 0, "Cannot register hidden HDR fixture class.");
            MONITORINFO monitor {};
            monitor.cbSize = sizeof(monitor);
            Require(GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor) != FALSE,
                "Cannot read primary monitor bounds for hidden fixture placement.");
            int x = monitor.rcMonitor.left + 16;
            int y = monitor.rcMonitor.top + 16;
            Parent = CreateWindowExW(WS_EX_NOACTIVATE, ClassName, L"Hidden owned HDR parent", WS_POPUP,
                x, y, 96, 96, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
            Require(Parent != nullptr, "Cannot create hidden owned HDR parent.");
            Desktop = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT, ClassName,
                L"Hidden owned HDR desktop", WS_CHILD | WS_DISABLED, 0, 0, 64, 64,
                Parent, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (!Desktop) { throw std::runtime_error("Cannot create hidden layered desktop child, Win32=" + std::to_string(GetLastError())); }
            Glass = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT, ClassName,
                L"Hidden owned HDR Glass", WS_POPUP, x, y, 64, 64,
                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
            Require(Glass != nullptr, "Cannot create hidden layered Glass popup.");
            Require(SetLayeredWindowAttributes(Desktop, 0, 255, LWA_ALPHA) != FALSE,
                "Cannot initialize owned desktop opacity.");
            VerifyHiddenWindows();
        }

        /// Confirms presentation does not change parenting, input exclusion, layered styles or window visibility.
        void VerifyHiddenWindows() const {
            Require(!IsWindowVisible(Parent) && !IsWindowVisible(Desktop) && !IsWindowVisible(Glass),
                "Every native HDR fixture window must stay hidden.");
            Require(GetParent(Desktop) == Parent, "Composition must preserve the original desktop parent.");
            LONG_PTR style = GetWindowLongPtrW(Desktop, GWL_STYLE);
            Require((style & (WS_CHILD | WS_DISABLED)) == (WS_CHILD | WS_DISABLED),
                "Desktop composition must preserve child and disabled styles.");
            Require((GetWindowLongPtrW(Desktop, GWL_EXSTYLE) & WS_EX_LAYERED) != 0
                && (GetWindowLongPtrW(Glass, GWL_EXSTYLE) & WS_EX_LAYERED) != 0,
                "Both approved composition windows must remain layered.");
        }

        /// Compares production output state against independent read-only monitor discovery and actual swapchain descriptors.
        static void VerifyOutput(DirectX11Bootstrap& output, HWND window, bool desktop) {
            auto expected = DirectX11OutputColorState::Query(window, output.GetDevice(), 4);
            Require(output.IsHdrOutput() == expected.Hdr, "Actual HDR presentation must match the discovered supported HDR monitor.");
            DXGI_FORMAT format = expected.Hdr ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_B8G8R8A8_UNORM;
            DXGI_SWAP_CHAIN_DESC1 description {};
            Check(output.GetSwapChain()->GetDesc1(&description), "Read hidden swapchain descriptor");
            Require(output.GetOutputFormat() == format && description.Format == format && description.SampleDesc.Count == 1,
                "Swapchain format must match actual HDR/SDR state and remain single-sample.");
            Require(description.Width == static_cast<UINT>(output.GetWidth()) && description.Height == static_cast<UINT>(output.GetHeight()),
                "Swapchain dimensions must track the output after resize.");
            Require(description.AlphaMode == (desktop ? DXGI_ALPHA_MODE_IGNORE : DXGI_ALPHA_MODE_PREMULTIPLIED),
                "Desktop opacity and Glass premultiplied-alpha contracts must be distinct.");
            if (expected.Hdr || !desktop) {
                Require(description.SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL && description.BufferCount == 2,
                    "HDR Desktop and Glass must use two-buffer flip-sequential composition.");
            }
            if (expected.Hdr) {
                Microsoft::WRL::ComPtr<IDXGISwapChain3> advanced;
                Check(output.GetSwapChain()->QueryInterface(IID_PPV_ARGS(&advanced)), "Query actual HDR swapchain interface");
                UINT support = 0;
                Check(advanced->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, &support), "Read actual scRGB color-space support");
                Require((support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) != 0,
                    "Created FP16 swapchain must explicitly support linear scRGB presentation.");
                Require(std::abs(output.GetSdrWhiteScale() - expected.SdrWhiteScale) < 0.0001f,
                    "Output must preserve the independently queried Windows SDR-white reference.");
            }
            VerifyAttachment(output, output.GetRenderTargetView(), format);
            VerifyAttachment(output, output.GetDepthStencilView(), DXGI_FORMAT_D24_UNORM_S8_UINT);
            std::cout << "OUTPUT " << (desktop ? "Desktop" : "Glass") << " hdr=" << output.IsHdrOutput()
                << " format=" << description.Format << " white_scale=" << output.GetSdrWhiteScale() << " reason=" << expected.Reason << '\n';
        }

        /// Requires independent color/depth storage to retain four samples and exact per-output dimensions and format.
        static void VerifyAttachment(DirectX11Bootstrap& output, ID3D11View* view, DXGI_FORMAT format) {
            Microsoft::WRL::ComPtr<ID3D11Resource> resource;
            view->GetResource(&resource);
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            Check(resource.As(&texture), "Read hidden output attachment texture");
            D3D11_TEXTURE2D_DESC description {};
            texture->GetDesc(&description);
            Require(description.Format == format && description.SampleDesc.Count == 4 && output.GetSampleCount() == 4
                && description.Width == static_cast<UINT>(output.GetWidth()) && description.Height == static_cast<UINT>(output.GetHeight()),
                "Hidden color/depth attachments must preserve typed format, 4x MSAA and resized dimensions.");
        }

        /// Writes only synthetic premultiplied radiance to owned multisampled attachments before the real output resolve.
        static void ClearAndResolve(DirectX11Bootstrap& output, float alpha) {
            const float color[4] {6 * alpha, 3 * alpha, 1.5f * alpha, alpha};
            output.GetDeviceContext()->ClearRenderTargetView(output.GetRenderTargetView(), color);
            output.ResolveOutput();
        }

        /// Reads only the test's explicitly cleared swapchain storage to verify HDR values survive the real bootstrap resolve.
        static void VerifySyntheticPixel(DirectX11Bootstrap& output, float alpha) {
            Microsoft::WRL::ComPtr<ID3D11Texture2D> source;
            Check(output.GetSwapChain()->GetBuffer(0, IID_PPV_ARGS(&source)), "Get owned synthetic swapchain buffer");
            D3D11_TEXTURE2D_DESC description {};
            source->GetDesc(&description);
            description.Width = 1; description.Height = 1;
            description.Usage = D3D11_USAGE_STAGING; description.BindFlags = 0;
            description.CPUAccessFlags = D3D11_CPU_ACCESS_READ; description.MiscFlags = 0;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
            Check(output.GetDevice()->CreateTexture2D(&description, nullptr, &staging), "Create owned synthetic pixel staging");
            D3D11_BOX box {3, 3, 0, 4, 4, 1};
            output.GetDeviceContext()->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, source.Get(), 0, &box);
            D3D11_MAPPED_SUBRESOURCE mapped {};
            Check(output.GetDeviceContext()->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Read owned resolved synthetic pixel");
            std::array<std::uint16_t, 4> half {};
            std::memcpy(half.data(), mapped.pData, output.IsHdrOutput() ? 8 : 4);
            output.GetDeviceContext()->Unmap(staging.Get(), 0);
            if (output.IsHdrOutput()) {
                const std::array<float, 4> expected {6 * alpha, 3 * alpha, 1.5f * alpha, alpha};
                for (std::size_t channel = 0; channel < 4; channel++) {
                    Require(std::abs(DirectX::PackedVector::XMConvertHalfToFloat(half[channel]) - expected[channel]) < 0.002f,
                        "Native FP16 resolve must preserve synthetic HDR radiance and alpha.");
                }
            }
        }

        /// Uses the real asynchronous sampler at an explicit synthetic coordinate, bounded to 500 ms without cursor access.
        static void VerifySampler(DirectX11Bootstrap& output, DirectX11HitTestSampler& sampler, float alpha) {
            sampler.Capture(3, 3);
            output.GetDeviceContext()->Flush();
            ULONGLONG deadline = GetTickCount64() + 500;
            int actual = -1;
            while (!sampler.TryReadLatestAlpha(actual)) {
                Require(GetTickCount64() < deadline, "Synthetic hit-test alpha did not complete within 500 ms.");
                Sleep(1);
            }
            Require(actual == static_cast<int>(std::lround(alpha * 255)), "Real hit-test sampler must retain 0-to-255 alpha semantics on native output.");
        }

        /// Reports the failed invariant without substituting a weaker renderer or skipping unsupported behavior.
        static void Require(bool condition, const char* message) { if (!condition) { throw std::runtime_error(message); } }
        /// Preserves the actual failing operation and HRESULT for reproducible native graphics failures.
        static void Check(HRESULT result, const char* operation) {
            if (FAILED(result)) { throw std::runtime_error(std::string(operation) + " failed, HRESULT=" + std::to_string(static_cast<unsigned long>(result))); }
        }
    };
}

/// Executes only hidden owned-window composition fixtures, never player startup or desktop capture.
int main() {
    try { helengine::windows::NativeHdrCompositionTests tests; tests.Run(); return 0; }
    catch (const std::exception& error) { std::cerr << "FAIL native HDR composition: " << error.what() << '\n'; return 1; }
}
