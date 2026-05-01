#include "platform/windows/directx11/directx11_presenter.hpp"

#include "platform/windows/directx11/directx11_bootstrap.hpp"

namespace helengine::windows {
    /// Creates a presenter bound to one DirectX11 bootstrap.
    DirectX11Presenter::DirectX11Presenter(DirectX11Bootstrap& bootstrap)
        : Bootstrap(bootstrap) {
    }

    /// Releases the presenter without owning the bootstrap resources.
    DirectX11Presenter::~DirectX11Presenter() = default;

    /// Presents the current swap-chain back buffer.
    void DirectX11Presenter::RenderFrame() {
        Bootstrap.GetSwapChain()->Present(1, 0);
    }
}
