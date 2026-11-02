#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>

namespace helengine::windows {
    /// <summary>Draws the published voxel atlas as instanced grayscale spheres without scene materials or post effects.</summary>
    class Win32SceneGiSpheres {
        /// <summary>Generates bounded latitude geometry and maps each instance to an existing atlas cell.</summary>
        Microsoft::WRL::ComPtr<ID3D11VertexShader> Vertex;
        /// <summary>Evaluates the six stored irradiance lobes on interpolated sphere normals.</summary>
        Microsoft::WRL::ComPtr<ID3D11PixelShader> Pixel;
        /// <summary>Stores the original camera view projection at the explicit IEEE32 graphics boundary.</summary>
        Microsoft::WRL::ComPtr<ID3D11Buffer> Camera;
        /// <summary>Depth tests and writes the nearest sphere independently of instance ordering.</summary>
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> Depth;
        /// <summary>Draws opaque diagnostic intensity directly to the selected output.</summary>
        Microsoft::WRL::ComPtr<ID3D11BlendState> Blend;
        /// <summary>Uses solid two-sided geometry with clipping and multisample coverage.</summary>
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> Rasterizer;
    public:
        /// <summary>Compiles the production diagnostic shader and creates persistent resources on the supplied device.</summary>
        explicit Win32SceneGiSpheres(ID3D11Device* device);
        /// <summary>Draws spheres only for valid free probes accepted by actual receiver sampling, including black probes; restores changed bindings.</summary>
        void Draw(ID3D11DeviceContext* context,const float* viewProjection,ID3D11ShaderResourceView* atlas,ID3D11Buffer* fieldParameters,std::uint32_t cellCount,ID3D11ShaderResourceView* usage,bool enabled=true);
    };
}
