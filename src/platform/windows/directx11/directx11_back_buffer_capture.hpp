#pragma once

#include <Windows.h>

#include <string>

namespace helengine::windows {
    class DirectX11Bootstrap;

    /// Copies the current swap-chain back buffer to CPU memory and saves it as a BMP. Only constructed when the player
    /// was started with --capture, and only used on the final requested frame before it is presented.
    class DirectX11BackBufferCapture {
    public:
        /// Creates a capture helper bound to the bootstrap that owns the device, context and swap chain.
        explicit DirectX11BackBufferCapture(DirectX11Bootstrap& bootstrap);

        /// Copies the back buffer through a CPU-readable staging texture and writes it as a top-down 32-bit BMP to path.
        /// Throws std::runtime_error on any failed DirectX call or when the back buffer is not DXGI_FORMAT_B8G8R8A8_UNORM.
        void CaptureToBmp(const std::wstring& path);

    private:
        /// Throws std::runtime_error carrying the operation name and the failing HRESULT in hexadecimal.
        static void ThrowIfFailed(HRESULT result, const char* operation);

        /// Stores the DirectX11 bootstrap whose back buffer is captured; not owned.
        DirectX11Bootstrap& Bootstrap;
    };
}
