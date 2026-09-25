#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <array>

namespace helengine::windows {
    class DirectX11Bootstrap;

    /// Reads the alpha of one back-buffer pixel for the overlay window's per-pixel click-through without ever stalling
    /// the frame. Each frame, Capture copies the pixel under the cursor (or the --hit-test-probe point) into the next
    /// slot of a ring of three 1x1 CPU-readable staging textures, and TryReadLatestAlpha maps the oldest copy that is
    /// still unread with D3D11_MAP_FLAG_DO_NOT_WAIT, so results arrive with one or two frames of latency instead of
    /// making the CPU wait for the GPU. Only constructed in overlay mode.
    class DirectX11HitTestSampler {
    public:
        /// Creates the three 1x1 B8G8R8A8 staging textures on the bootstrap's device. The format matches the swap
        /// chain's back buffer, which both swap-chain paths create as DXGI_FORMAT_B8G8R8A8_UNORM. Throws
        /// std::runtime_error carrying the HRESULT in hexadecimal when a texture cannot be created.
        /// <param name="bootstrap">Bootstrap that owns the device, context and swap chain; not owned, must outlive the
        /// sampler.</param>
        explicit DirectX11HitTestSampler(DirectX11Bootstrap& bootstrap);

        /// Copies the back-buffer pixel at (x, y) into the next ring slot with CopySubresourceRegion. Does nothing when
        /// the point lies outside the current client area (the cursor is outside the window), so the click-through
        /// state stays unchanged. When all three slots are still unread, the oldest one is overwritten. Must be called
        /// after the frame is drawn and before it is presented. Throws std::runtime_error when the back buffer cannot
        /// be obtained.
        /// <param name="x">Client-area X coordinate of the pixel.</param>
        /// <param name="y">Client-area Y coordinate of the pixel.</param>
        void Capture(int x, int y);

        /// Tries to read the alpha of the oldest unread capture by mapping its slot with D3D11_MAP_FLAG_DO_NOT_WAIT.
        /// Returns false without waiting when nothing is pending or the GPU has not finished the copy yet
        /// (DXGI_ERROR_WAS_STILL_DRAWING); the slot then stays pending for the next call. Any other failed Map throws
        /// std::runtime_error carrying the HRESULT in hexadecimal.
        /// <param name="alpha">Receives the pixel's alpha, 0 to 255, when the method returns true.</param>
        /// <returns>True when a completed sample was read.</returns>
        bool TryReadLatestAlpha(int& alpha);

    private:
        /// Number of staging textures in the ring: enough for one copy per frame to finish on the GPU before it is
        /// read, with one to spare.
        static constexpr int RingSize = 3;

        /// Stores the bootstrap whose back buffer is sampled; not owned.
        DirectX11Bootstrap& Bootstrap;

        /// Stores the ring of 1x1 CPU-readable staging textures that receive the sampled pixels.
        std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>, RingSize> StagingTextures;

        /// Stores the ring slot the next Capture writes to.
        int NextSlot;

        /// Stores how many captured slots have not been read yet, from 0 to RingSize; the oldest of them is the slot
        /// PendingCount positions before NextSlot.
        int PendingCount;
    };
}
