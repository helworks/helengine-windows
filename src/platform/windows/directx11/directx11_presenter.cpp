#include "platform/windows/directx11/directx11_presenter.hpp"

#include "platform/windows/directx11/directx11_bootstrap.hpp"

namespace helengine::windows {
    /// Creates a presenter bound to one DirectX11 bootstrap.
    DirectX11Presenter::DirectX11Presenter(DirectX11Bootstrap& bootstrap)
        : Bootstrap(bootstrap) {
    }

    /// Releases the presenter without owning the bootstrap resources.
    DirectX11Presenter::~DirectX11Presenter() = default;

    /// Binds the back buffer, clears to black, and presents one frame.
    void DirectX11Presenter::RenderFrame() {
        static const float ClearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };

        ID3D11RenderTargetView* renderTargetView = Bootstrap.GetRenderTargetView();
        Bootstrap.GetDeviceContext()->OMSetRenderTargets(1, &renderTargetView, nullptr);
        Bootstrap.GetDeviceContext()->ClearRenderTargetView(renderTargetView, ClearColor);
        Bootstrap.GetSwapChain()->Present(1, 0);
    }
}
