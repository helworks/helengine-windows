#include "platform/windows/directx11/directx11_hit_test_sampler.hpp"

#include <cstdint>
#include <sstream>
#include <stdexcept>

#include "platform/windows/directx11/directx11_bootstrap.hpp"
#include "platform/windows/directx11/directx11_hresult_formatter.hpp"

namespace helengine::windows {
    /// Creates the three 1x1 B8G8R8A8 staging textures on the bootstrap's device. The format matches the swap chain's
    /// back buffer, which both swap-chain paths create as DXGI_FORMAT_B8G8R8A8_UNORM. Throws std::runtime_error
    /// carrying the HRESULT in hexadecimal when a texture cannot be created.
    DirectX11HitTestSampler::DirectX11HitTestSampler(DirectX11Bootstrap& bootstrap)
        : Bootstrap(bootstrap),
          StagingTextures(),
          NextSlot(0),
          PendingCount(0) {
        D3D11_TEXTURE2D_DESC stagingDesc = {};
        stagingDesc.Width = 1;
        stagingDesc.Height = 1;
        stagingDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        stagingDesc.MipLevels = 1;
        stagingDesc.ArraySize = 1;
        stagingDesc.SampleDesc.Count = 1;
        stagingDesc.SampleDesc.Quality = 0;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;
        for (int slotIndex = 0; slotIndex < RingSize; slotIndex++) {
            HRESULT createResult = Bootstrap.GetDevice()->CreateTexture2D(&stagingDesc, nullptr, StagingTextures[slotIndex].GetAddressOf());
            if (FAILED(createResult)) {
                std::ostringstream messageBuilder;
                messageBuilder << "Hit-test sampler: ID3D11Device::CreateTexture2D (staging) failed with HRESULT "
                               << DirectX11HResultFormatter::ToHex(createResult) << ".";
                throw std::runtime_error(messageBuilder.str());
            }
        }
    }

    /// Copies the back-buffer pixel at (x, y) into the next ring slot with CopySubresourceRegion. Does nothing when the
    /// point lies outside the current client area (the cursor is outside the window), so the click-through state stays
    /// unchanged. When all three slots are still unread, the oldest one is overwritten. Must be called after the frame
    /// is drawn and before it is presented. Throws std::runtime_error when the back buffer cannot be obtained.
    void DirectX11HitTestSampler::Capture(int x, int y) {
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

    /// Tries to read the alpha of the oldest unread capture by mapping its slot with D3D11_MAP_FLAG_DO_NOT_WAIT. Returns
    /// false without waiting when nothing is pending or the GPU has not finished the copy yet
    /// (DXGI_ERROR_WAS_STILL_DRAWING); the slot then stays pending for the next call. Any other failed Map throws
    /// std::runtime_error carrying the HRESULT in hexadecimal.
    bool DirectX11HitTestSampler::TryReadLatestAlpha(int& alpha) {
        if (PendingCount == 0) {
            return false;
        }

        int oldestSlot = (NextSlot - PendingCount + RingSize) % RingSize;
        ID3D11DeviceContext* deviceContext = Bootstrap.GetDeviceContext();
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        HRESULT mapResult = deviceContext->Map(StagingTextures[oldestSlot].Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
        if (mapResult == DXGI_ERROR_WAS_STILL_DRAWING) {
            return false;
        }
        if (FAILED(mapResult)) {
            std::ostringstream messageBuilder;
            messageBuilder << "Hit-test sampler: ID3D11DeviceContext::Map failed with HRESULT "
                           << DirectX11HResultFormatter::ToHex(mapResult) << ".";
            throw std::runtime_error(messageBuilder.str());
        }

        // The pixel is stored as B, G, R, A bytes, so the alpha is the fourth byte.
        alpha = static_cast<int>(static_cast<const std::uint8_t*>(mapped.pData)[3]);
        deviceContext->Unmap(StagingTextures[oldestSlot].Get(), 0);
        PendingCount--;
        return true;
    }
}
