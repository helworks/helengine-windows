#pragma once
#include <d3d11.h>
#include <wrl/client.h>

namespace helengine::windows {
    /// Owns matching output color/depth attachments and resolves engine rendering into a retained single-sample presentation surface.
    class DirectX11OutputAttachments {
    public:
        /// Requires a single-sample BGRA8 render target on the supplied device and explicitly supported 1x or 4x sampling.
        DirectX11OutputAttachments(ID3D11Device* device, ID3D11Texture2D* destination, UINT sampleCount);
        /// Gets the output's renderable color view, backed by separate multisampled storage only for 4x.
        ID3D11RenderTargetView* GetRenderTargetView() const;
        /// Gets the matching D24 depth/stencil view for this output's dimensions and sample count.
        ID3D11DepthStencilView* GetDepthStencilView() const;
        /// Gets the validated color/depth sample count.
        UINT GetSampleCount() const;
        /// Unbinds completed multisampled output rendering and resolves all premultiplied color/alpha channels before sampling or presentation.
        void Resolve(ID3D11DeviceContext* context);
    private:
        /// Fails unsupported color or depth sampling explicitly instead of changing requested output quality.
        void RequireMultisampleSupport(DXGI_FORMAT format);
        /// Preserves the failed graphics operation and its HRESULT in a readable exception.
        static void Check(HRESULT result, const char* operation);
        /// Retains the output device while its resources are alive.
        Microsoft::WRL::ComPtr<ID3D11Device> Device;
        /// Retains the single-sample destination until the owner releases attachments before resize.
        Microsoft::WRL::ComPtr<ID3D11Texture2D> Destination;
        /// Owns separate color storage only when rendering more than one sample.
        Microsoft::WRL::ComPtr<ID3D11Texture2D> MultisampledColor;
        /// Owns the selected renderable color view.
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> ColorView;
        /// Owns matching depth/stencil storage.
        Microsoft::WRL::ComPtr<ID3D11Texture2D> DepthBuffer;
        /// Owns the selected depth/stencil view.
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> DepthView;
        /// Stores the validated sample count for both color and depth.
        UINT SampleCount;
    };
}