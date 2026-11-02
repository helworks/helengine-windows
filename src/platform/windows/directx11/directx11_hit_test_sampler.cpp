#include "platform/windows/directx11/directx11_hit_test_sampler.hpp"
#include "platform/windows/directx11/directx11_debug_diagnostics.hpp"

#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "platform/windows/directx11/directx11_bootstrap.hpp"
#include "platform/windows/directx11/directx11_hresult_formatter.hpp"
#include "platform/windows/directx11/directx11_output_pixel_conversion.hpp"

namespace helengine::windows {
    /// Creates three single-pixel staging textures matching the current SDR or scRGB back buffer. Throws std::runtime_error
    /// carrying the HRESULT in hexadecimal when a texture cannot be created.
    DirectX11HitTestSampler::DirectX11HitTestSampler(DirectX11Bootstrap& bootstrap)
        : Bootstrap(bootstrap),
          StagingTextures(),
          NextSlot(0),
          PendingCount(0),
          OutputFormat(DXGI_FORMAT_UNKNOWN) {
        UpdateOutputFormat();
    }

    /// Builds the replacement ring completely before publishing its format, discarding captures from a previous encoding.
    void DirectX11HitTestSampler::UpdateOutputFormat() {
        DXGI_FORMAT format = Bootstrap.GetOutputFormat();
        DirectX11OutputPixelConversion::GetPixelByteCount(format);
        if (format == OutputFormat) { return; }
        std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>, RingSize> replacement;
        D3D11_TEXTURE2D_DESC stagingDesc = {};
        stagingDesc.Width = 1;
        stagingDesc.Height = 1;
        stagingDesc.Format = format;
        stagingDesc.MipLevels = 1;
        stagingDesc.ArraySize = 1;
        stagingDesc.SampleDesc.Count = 1;
        stagingDesc.SampleDesc.Quality = 0;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;
        for (int slotIndex = 0; slotIndex < RingSize; slotIndex++) {
            HRESULT createResult = Bootstrap.GetDevice()->CreateTexture2D(&stagingDesc, nullptr, replacement[slotIndex].GetAddressOf());
            if (FAILED(createResult)) {
                std::ostringstream messageBuilder;
                messageBuilder << "Hit-test sampler: ID3D11Device::CreateTexture2D (staging) failed with HRESULT "
                               << DirectX11HResultFormatter::ToHex(createResult) << ".";
                throw std::runtime_error(messageBuilder.str());
            }
        }
        StagingTextures = std::move(replacement);
        OutputFormat = format;
        NextSlot = 0;
        PendingCount = 0;
    }

    /// Copies the back-buffer pixel at (x, y) into the next ring slot with CopySubresourceRegion. Does nothing when the
    /// point lies outside the current client area (the cursor is outside the window), so the click-through state stays
    /// unchanged. When all three slots are still unread, the oldest one is overwritten. Must be called after the frame
    /// is drawn and before it is presented. Throws std::runtime_error when the back buffer cannot be obtained.
    void DirectX11HitTestSampler::Capture(int x, int y) {
        UpdateOutputFormat();
        if (x < 0 || y < 0 || x >= Bootstrap.GetWidth() || y >= Bootstrap.GetHeight()) {
            return;
        }

        Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
        HRESULT bufferResult = Bootstrap.GetSwapChain()->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(backBuffer.GetAddressOf()));
        if (FAILED(bufferResult)) {
            std::ostringstream messageBuilder;
            messageBuilder << "Hit-test sampler: IDXGISwapChain1::GetBuffer failed with HRESULT "
                           << DirectX11HResultFormatter::ToHex(bufferResult) << ".";
            throw std::runtime_error(messageBuilder.str());
        }

        D3D11_TEXTURE2D_DESC description {};
        backBuffer->GetDesc(&description);
        if (description.Format != OutputFormat || description.SampleDesc.Count != 1) {
            throw std::runtime_error("Hit-test sampler requires a single-sample back buffer matching the active output format.");
        }

        D3D11_BOX pixelBox = {};
        pixelBox.left = static_cast<UINT>(x);
        pixelBox.top = static_cast<UINT>(y);
        pixelBox.front = 0;
        pixelBox.right = static_cast<UINT>(x) + 1u;
        pixelBox.bottom = static_cast<UINT>(y) + 1u;
        pixelBox.back = 1;
        Bootstrap.GetDeviceContext()->CopySubresourceRegion(StagingTextures[NextSlot].Get(), 0, 0, 0, 0, backBuffer.Get(), 0, &pixelBox);

        NextSlot = (NextSlot + 1) % RingSize;
        if (PendingCount < RingSize) {
            PendingCount++;
        }
    }

    /// Drains the ring to the newest completed capture: maps the unread slots from oldest to newest, each with
    /// D3D11_MAP_FLAG_DO_NOT_WAIT, and stops at the first one the GPU has not finished yet
    /// (DXGI_ERROR_WAS_STILL_DRAWING), which stays pending for the next call together with every newer slot. Every
    /// slot read leaves the ring, so a backlog of completed captures cannot add permanent latency. Returns false
    /// without waiting when nothing completed. Any other failed Map throws std::runtime_error carrying the HRESULT in
    /// hexadecimal.
    bool DirectX11HitTestSampler::TryReadLatestAlpha(int& alpha) {
        DirectX11DebugDiagnostics::Drain(Bootstrap.GetDevice());
        UpdateOutputFormat();
        ID3D11DeviceContext* deviceContext = Bootstrap.GetDeviceContext();
        bool sampleRead = false;
        while (PendingCount > 0) {
            int oldestSlot = (NextSlot - PendingCount + RingSize) % RingSize;
            D3D11_MAPPED_SUBRESOURCE mapped = {};
            HRESULT mapResult = deviceContext->Map(StagingTextures[oldestSlot].Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
            if (mapResult == DXGI_ERROR_WAS_STILL_DRAWING) {
                break;
            }
            if (FAILED(mapResult)) {
                DirectX11DebugDiagnostics::Drain(Bootstrap.GetDevice());
                std::ostringstream messageBuilder;
                messageBuilder << "Hit-test sampler: ID3D11DeviceContext::Map failed with HRESULT "
                               << DirectX11HResultFormatter::ToHex(mapResult) << ". Device removed reason: "
                               << DirectX11HResultFormatter::ToHex(Bootstrap.GetDevice()->GetDeviceRemovedReason()) << ".";
                throw std::runtime_error(messageBuilder.str());
            }

            try {
                alpha = DirectX11OutputPixelConversion::ReadAlpha(mapped.pData, OutputFormat);
            }
            catch (...) {
                deviceContext->Unmap(StagingTextures[oldestSlot].Get(), 0);
                throw;
            }
            deviceContext->Unmap(StagingTextures[oldestSlot].Get(), 0);
            PendingCount--;
            sampleRead = true;
        }

        return sampleRead;
    }
}
