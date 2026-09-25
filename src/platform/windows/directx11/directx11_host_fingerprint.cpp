#include "platform/windows/directx11/directx11_host_fingerprint.hpp"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#include <dxgi1_2.h>

#include "platform/windows/directx11/directx11_bootstrap.hpp"

namespace helengine::windows {
    /// Creates a fingerprint bound to the bootstrap that owns the swap chain and to the player's main window.
    DirectX11HostFingerprint::DirectX11HostFingerprint(DirectX11Bootstrap& bootstrap, HWND windowHandle)
        : Bootstrap(bootstrap),
          WindowHandle(windowHandle),
          PresentRecorded(false),
          FirstPresentTime(),
          LastPresentTime(),
          PresentFailureCount(0),
          ReportedPresentFailures() {
    }

    /// Records one presented frame: stamps the wall clock (the first call starts it, every call moves its end) and
    /// counts a failed Present. Returns true only for a failing HRESULT that has not been seen before in this run,
    /// so the caller logs each distinct failure once.
    bool DirectX11HostFingerprint::RecordPresent(HRESULT presentResult) {
        LastPresentTime = std::chrono::steady_clock::now();
        if (!PresentRecorded) {
            FirstPresentTime = LastPresentTime;
            PresentRecorded = true;
        }

        if (!FAILED(presentResult)) {
            return false;
        }

        PresentFailureCount++;
        if (std::find(ReportedPresentFailures.begin(), ReportedPresentFailures.end(), presentResult) != ReportedPresentFailures.end()) {
            return false;
        }

        ReportedPresentFailures.push_back(presentResult);
        return true;
    }

    /// Builds the HOST_FINGERPRINT line from the current swap-chain description, window styles, client rectangle
    /// and Present count, plus the recorded failures, the given frame count and the milliseconds between the first
    /// and the last recorded Present. Throws std::runtime_error when a DXGI or Win32 query fails.
    std::string DirectX11HostFingerprint::Describe(int frameCount) const {
        IDXGISwapChain1* swapChain = Bootstrap.GetSwapChain();
        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        ThrowIfFailed(swapChain->GetDesc1(&swapChainDesc), "IDXGISwapChain1::GetDesc1");

        UINT presentCount = 0;
        ThrowIfFailed(swapChain->GetLastPresentCount(&presentCount), "IDXGISwapChain::GetLastPresentCount");

        std::uint32_t windowStyle = static_cast<std::uint32_t>(GetWindowLongPtrW(WindowHandle, GWL_STYLE));
        std::uint32_t windowExStyle = static_cast<std::uint32_t>(GetWindowLongPtrW(WindowHandle, GWL_EXSTYLE));

        RECT clientRect = {};
        if (!GetClientRect(WindowHandle, &clientRect)) {
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()), "GetClientRect");
        }

        long long elapsedMilliseconds = 0;
        if (PresentRecorded) {
            elapsedMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(LastPresentTime - FirstPresentTime).count();
        }

        std::ostringstream lineBuilder;
        lineBuilder << "HOST_FINGERPRINT format=" << static_cast<int>(swapChainDesc.Format)
                    << " alpha=" << static_cast<int>(swapChainDesc.AlphaMode)
                    << " swapEffect=" << static_cast<int>(swapChainDesc.SwapEffect)
                    << " buffers=" << swapChainDesc.BufferCount
                    << " scaling=" << static_cast<int>(swapChainDesc.Scaling)
                    << " style=0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << windowStyle
                    << " exStyle=0x" << std::setw(8) << windowExStyle << std::dec << std::setfill(' ')
                    << " client=" << (clientRect.right - clientRect.left) << "x" << (clientRect.bottom - clientRect.top)
                    << " presentCount=" << presentCount
                    << " presentFailures=" << PresentFailureCount
                    << " frames=" << frameCount
                    << " elapsedMs=" << elapsedMilliseconds;
        return lineBuilder.str();
    }

    /// Formats a failing Present HRESULT as a readable startup-log line with the value in hexadecimal.
    std::string DirectX11HostFingerprint::DescribePresentFailure(HRESULT presentResult) {
        std::ostringstream messageBuilder;
        messageBuilder << "IDXGISwapChain1::Present failed with HRESULT 0x" << std::hex << std::uppercase << std::setw(8)
                       << std::setfill('0') << static_cast<std::uint32_t>(presentResult) << ".";
        return messageBuilder.str();
    }

    /// Throws std::runtime_error carrying the operation name and the failing HRESULT in hexadecimal.
    void DirectX11HostFingerprint::ThrowIfFailed(HRESULT result, const char* operation) {
        if (FAILED(result)) {
            std::ostringstream messageBuilder;
            messageBuilder << "Host fingerprint: " << operation << " failed with HRESULT 0x" << std::hex << std::uppercase
                           << std::setw(8) << std::setfill('0') << static_cast<std::uint32_t>(result) << ".";
            throw std::runtime_error(messageBuilder.str());
        }
    }
}
