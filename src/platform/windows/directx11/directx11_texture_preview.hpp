#pragma once
#include <d3d11.h>
#include <wrl/client.h>

namespace helengine::windows {
    class DirectX11PreviewProcessor;
    /// Owns one bounded, shader-readable engine image independently of transient external capture frames.
    class DirectX11TexturePreview {
        /// Allows the shared processor to update compatible storage without exposing writable graphics internals to capture adapters.
        friend class DirectX11PreviewProcessor;
        /// Retains the device that owns every preview resource.
        Microsoft::WRL::ComPtr<ID3D11Device> Device;
        /// Retains the latest bounded RGBA image across capture suspension.
        Microsoft::WRL::ComPtr<ID3D11Texture2D> Texture;
        /// Owns the view consumed by ordinary engine materials.
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> View;
        /// Owns the output view used only during GPU reduction.
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> Target;
        /// Owns compatible shader-readable scratch when an external frame cannot be sampled directly.
        Microsoft::WRL::ComPtr<ID3D11Texture2D> SourceCopy;
        /// Bounds output width without imposing a global capture-resolution policy on the engine.
        UINT MaximumWidth;
        /// Bounds output height without upscaling small source images.
        UINT MaximumHeight;
        /// Allocates a complete matching output set before replacing the last valid preview.
        void SetDimensions(UINT width,UINT height);
    public:
        /// Requires a real device and positive supported output limits; image storage is allocated on the first frame.
        DirectX11TexturePreview(ID3D11Device* device,UINT maximumWidth,UINT maximumHeight);
        /// Keeps one source's mutable GPU storage from being accidentally shared through value copies.
        DirectX11TexturePreview(const DirectX11TexturePreview&)=delete;
        /// Keeps preview identity and device ownership fixed for its capture source.
        DirectX11TexturePreview& operator=(const DirectX11TexturePreview&)=delete;
        /// Borrows the latest bounded image, or null before the first valid frame.
        ID3D11Texture2D* GetTexture() const;
        /// Borrows the corresponding material sampling view, or null before the first valid frame.
        ID3D11ShaderResourceView* GetShaderResourceView() const;
        /// Releases capture-sized scratch while retaining the last bounded image and its sampling view.
        void ResetSourceCopy();
    };
}