#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <stdexcept>

namespace helengine::windows {
    /// <summary>Caches the fixed material cull modes for single-sample and multisample scene geometry.</summary>
    class Win32MaterialRasterizerState {
        /// <summary>Owns none, front and back culling in each supported coverage configuration.</summary>
        std::array<std::array<Microsoft::WRL::ComPtr<ID3D11RasterizerState>, 3>, 2> States;

        /// <summary>Rejects unsupported native cull values before indexing the immutable device-state cache.</summary>
        static std::size_t Index(D3D11_CULL_MODE mode) {
            switch(mode) {
                case D3D11_CULL_NONE: return 0;
                case D3D11_CULL_FRONT: return 1;
                case D3D11_CULL_BACK: return 2;
                default: throw std::invalid_argument("Unsupported Windows material cull mode.");
            }
        }

    public:
        /// <summary>Creates all supported material rasterizers once while the scene pipeline is initialized.</summary>
        void Initialize(ID3D11Device* device) {
            if(device == nullptr) throw std::invalid_argument("Material rasterizers require a device.");
            constexpr D3D11_CULL_MODE modes[] = {D3D11_CULL_NONE, D3D11_CULL_FRONT, D3D11_CULL_BACK};
            for(std::size_t samples = 0; samples < States.size(); ++samples) {
                for(std::size_t mode = 0; mode < States[samples].size(); ++mode) {
                    D3D11_RASTERIZER_DESC description {};
                    description.FillMode = D3D11_FILL_SOLID;
                    description.CullMode = modes[mode];
                    // Match the existing right-handed scene camera and outward cross-product mesh winding.
                    description.FrontCounterClockwise = TRUE;
                    description.DepthClipEnable = TRUE;
                    description.MultisampleEnable = samples != 0;
                    if(FAILED(device->CreateRasterizerState(&description, States[samples][mode].ReleaseAndGetAddressOf())))
                        throw std::runtime_error("Create Windows material rasterizer state failed.");
                }
            }
        }

        /// <summary>Binds the authored culling choice without allocating per draw or changing shadow and UI state.</summary>
        void Bind(ID3D11DeviceContext* context, D3D11_CULL_MODE mode, bool multisample) const {
            if(context == nullptr) throw std::invalid_argument("Material rasterizers require a context.");
            auto* state = States[multisample ? 1 : 0][Index(mode)].Get();
            if(state == nullptr) throw std::logic_error("Material rasterizers must be initialized before drawing.");
            context->RSSetState(state);
        }

        /// <summary>Releases all scene material states when the owning renderer is disposed.</summary>
        void Reset() {
            for(auto& states : States) for(auto& state : states) state.Reset();
        }
    };
}
