#include "platform/windows/directx11/directx11_back_buffer_capture.hpp"

#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <d3d11.h>
#include <wrl/client.h>

#include "platform/windows/directx11/directx11_bootstrap.hpp"
#include "platform/windows/directx11/directx11_hresult_formatter.hpp"
#include "platform/windows/directx11/directx11_output_pixel_conversion.hpp"
#include "platform/windows/win32/bmp_image_writer.hpp"

namespace helengine::windows {
    /// Creates a capture helper bound to the bootstrap that owns the device, context and swap chain.
    DirectX11BackBufferCapture::DirectX11BackBufferCapture(DirectX11Bootstrap& bootstrap)
        : Bootstrap(bootstrap) {
    }

    /// Copies the back buffer through a CPU-readable staging texture and writes it as a top-down 32-bit BMP to path.
    /// Preserves SDR bytes and explicitly exports scRGB as bounded premultiplied SDR with the current OS white reference.
    void DirectX11BackBufferCapture::CaptureToBmp(const std::wstring& path) {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
        ThrowIfFailed(
            Bootstrap.GetSwapChain()->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(backBuffer.GetAddressOf())),
            "IDXGISwapChain1::GetBuffer");

        D3D11_TEXTURE2D_DESC backBufferDesc = {};
        backBuffer->GetDesc(&backBufferDesc);
        if (backBufferDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM && backBufferDesc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT) {
            std::ostringstream messageBuilder;
            messageBuilder << "Back-buffer capture requires BGRA8 SDR or RGBA16F scRGB but the back buffer uses DXGI format "
                           << static_cast<int>(backBufferDesc.Format) << ".";
            throw std::runtime_error(messageBuilder.str());
        }
        if (backBufferDesc.Format != Bootstrap.GetOutputFormat() || backBufferDesc.SampleDesc.Count != 1) {
            throw std::runtime_error("Back-buffer capture requires a single-sample texture matching the active output format.");
        }

        D3D11_TEXTURE2D_DESC stagingDesc = {};
        stagingDesc.Width = backBufferDesc.Width;
        stagingDesc.Height = backBufferDesc.Height;
        stagingDesc.Format = backBufferDesc.Format;
        stagingDesc.MipLevels = 1;
        stagingDesc.ArraySize = 1;
        stagingDesc.SampleDesc.Count = 1;
        stagingDesc.SampleDesc.Quality = 0;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingTexture;
        ThrowIfFailed(
            Bootstrap.GetDevice()->CreateTexture2D(&stagingDesc, nullptr, stagingTexture.GetAddressOf()),
            "ID3D11Device::CreateTexture2D (staging)");

        ID3D11DeviceContext* deviceContext = Bootstrap.GetDeviceContext();
        deviceContext->CopyResource(stagingTexture.Get(), backBuffer.Get());

        const size_t tightRowByteCount = static_cast<size_t>(stagingDesc.Width) * 4u;
        std::vector<uint8_t> bgraRows(tightRowByteCount * static_cast<size_t>(stagingDesc.Height));
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        ThrowIfFailed(
            deviceContext->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped),
            "ID3D11DeviceContext::Map");

        const uint8_t* sourceBytes = static_cast<const uint8_t*>(mapped.pData);
        try {
            for (UINT rowIndex = 0; rowIndex < stagingDesc.Height; rowIndex++) {
                DirectX11OutputPixelConversion::ConvertRowToSdrBgra(
                    sourceBytes + static_cast<size_t>(rowIndex) * mapped.RowPitch,
                    bgraRows.data() + static_cast<size_t>(rowIndex) * tightRowByteCount,
                    stagingDesc.Width, stagingDesc.Format, Bootstrap.GetSdrWhiteScale());
            }
        }
        catch (...) {
            deviceContext->Unmap(stagingTexture.Get(), 0);
            throw;
        }
        deviceContext->Unmap(stagingTexture.Get(), 0);

        BmpImageWriter::WriteTopDownBgra32(
            path,
            static_cast<int>(stagingDesc.Width),
            static_cast<int>(stagingDesc.Height),
            bgraRows);
    }

    /// Throws std::runtime_error carrying the operation name and the failing HRESULT in hexadecimal.
    void DirectX11BackBufferCapture::ThrowIfFailed(HRESULT result, const char* operation) {
        if (SUCCEEDED(result)) {
            return;
        }

        std::ostringstream messageBuilder;
        messageBuilder << "Back-buffer capture failed in " << operation << " with HRESULT " << DirectX11HResultFormatter::ToHex(result) << ".";
        throw std::runtime_error(messageBuilder.str());
    }
}
