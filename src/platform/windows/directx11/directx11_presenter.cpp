#include "platform/windows/directx11/directx11_presenter.hpp"

#include "platform/windows/directx11/directx11_bootstrap.hpp"
#include "platform/windows/runtime/windows_tracy_profiler.hpp"

namespace helengine::windows {
    /// Creates a presenter bound to one DirectX11 bootstrap.
    DirectX11Presenter::DirectX11Presenter(DirectX11Bootstrap& bootstrap)
        : Bootstrap(bootstrap) {
    }

    /// Releases the presenter without owning the bootstrap resources.
    DirectX11Presenter::~DirectX11Presenter() = default;

    /// Presents the current swap-chain back buffer.
    void DirectX11Presenter::RenderFrame() {
        HELENGINE_TRACY_ZONE_N("D3D11.Present");
        HELENGINE_TRACY_GPU_ZONE_N("D3D11.Present");
        Bootstrap.GetSwapChain()->Present(1, 0);
    }
}
