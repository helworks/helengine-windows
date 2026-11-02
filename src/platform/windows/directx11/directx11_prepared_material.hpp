#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
class RuntimeTexture;
namespace helengine::windows {
/// <summary>Owns resolved native material bindings while dynamic frame/object constants remain owned by the renderer.</summary>
class DirectX11PreparedMaterial final {
public:
    /// <summary>Retains an immutable content-shared constant resource, or an explicit missing slot to clear.</summary>
    struct Constant {
        /// <summary>Preserves the original shader register used by both vertex and pixel stages.</summary>
        int32_t Slot;
        /// <summary>Shares GPU ownership with every prepared material using the same exact constant bytes.</summary>
        std::shared_ptr<const Microsoft::WRL::ComPtr<ID3D11Buffer>> Buffer;
    };
    /// <summary>Borrows a resolved texture identity while deferring live resource-view lookup until binding.</summary>
    struct Texture {
        /// <summary>Owns the authored name for diagnostics without borrowing layout storage.</summary>
        std::string Name;
        /// <summary>Preserves the authored unified slot for diagnostics.</summary>
        int32_t UnifiedSlot;
        /// <summary>Stores the pre-resolved native DirectX register.</summary>
        int32_t Slot;
        /// <summary>Borrows the runtime texture under the material's existing ownership contract.</summary>
        RuntimeTexture* Value;
        /// <summary>Resolves the current renderer pixel texture at draw time when the diffuse binding is unset.</summary>
        bool DiffuseFallback;
    };
    /// <summary>Retains authored constant bindings, excluding engine-managed dynamic slots.</summary>
    std::vector<Constant> Constants;
    /// <summary>Retains authored sampled texture identities, excluding engine-managed shadow resources.</summary>
    std::vector<Texture> Textures;
};
}
