#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <memory>
#include "platform/windows/directx11/directx11_shader_compilation.hpp"
namespace helengine::windows {
    /// <summary>Owns one shared opaque-geometry capture and bounded screen-space irradiance field, independent of output windows.</summary>
    class Win32SceneSsgi {
        /// <summary>Retains the device shared by both presentation surfaces.</summary>
        Microsoft::WRL::ComPtr<ID3D11Device> Device;
        /// <summary>Compiles CPU shader bytecode asynchronously using the standard renderer loading contract.</summary>
        std::unique_ptr<DirectX11ShaderCompilation> Compilation;
        /// <summary>Retains preparation failures rather than retrying partial initialization.</summary>
        std::exception_ptr Failure;
        /// <summary>Marks the complete immutable pipeline as ready for scene capture.</summary>
        bool Prepared=false;
        /// <summary>Projects the renderer's standard position-normal-UV vertices.</summary>
        Microsoft::WRL::ComPtr<ID3D11VertexShader> CaptureVertex;
        /// <summary>Writes world position, normal and direct diffuse outgoing radiance.</summary>
        Microsoft::WRL::ComPtr<ID3D11PixelShader> CapturePixel;
        /// <summary>Generates a fullscreen triangle without geometry buffers.</summary>
        Microsoft::WRL::ComPtr<ID3D11VertexShader> FullscreenVertex;
        /// <summary>Integrates bounded hemisphere rays against only the direct source capture.</summary>
        Microsoft::WRL::ComPtr<ID3D11PixelShader> TracePixel;
        /// <summary>Matches the renderer's standard 32-byte mesh layout.</summary>
        Microsoft::WRL::ComPtr<ID3D11InputLayout> Layout;
        /// <summary>Uploads the shared projection, source sunlight and display contract.</summary>
        Microsoft::WRL::ComPtr<ID3D11Buffer> Parameters;
        /// <summary>Stores position, normal, direct radiance and the separate irradiance result.</summary>
        std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>,4> Textures;
        /// <summary>Receives capture attachments and the independent trace destination.</summary>
        std::array<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>,4> Targets;
        /// <summary>Exposes capture and result resources without any source feedback path.</summary>
        std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>,4> Views;
        /// <summary>Retains nearest-surface visibility for the shared capture.</summary>
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> Depth;
        /// <summary>Enables ordinary less-equal geometry depth writes.</summary>
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> GeometryDepth;
        /// <summary>Disables depth testing for the fullscreen trace.</summary>
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> TraceDepth;
        /// <summary>Disables inherited UI scissor state and geometry culling for a two-sided capture.</summary>
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> Rasterizer;
        /// <summary>Tracks shared capture width for allocation and viewport binding.</summary>
        UINT Width=0;
        /// <summary>Tracks shared capture height without changing the source camera aspect ratio.</summary>
        UINT Height=0;
        /// <summary>Preserves native resource failures with the operation responsible.</summary>
        static void Check(HRESULT result,const char* operation);
        /// <summary>Replaces attachments only when the bounded capture dimensions change.</summary>
        void Resize(UINT width,UINT height);
    public:
        /// <summary>Starts compilation without waiting for a device worker or introducing scene entities.</summary>
        explicit Win32SceneSsgi(ID3D11Device* device);
        /// <summary>Polls compilation and prepares all resources atomically on the render thread.</summary>
        bool TryPrepare();
        /// <summary>Allocates and clears one half-resolution capture capped to 960x540, preserving projection aspect.</summary>
        void BeginCapture(ID3D11DeviceContext* context,UINT width,UINT height,const std::array<float,36>& parameters);
        /// <summary>Overrides packaged shading with source-only material radiance after the renderer binds authored material textures.</summary>
        void BindCaptureGeometry(ID3D11DeviceContext* context);
        /// <summary>Traces once into a separate HDR irradiance attachment, leaving source radiance immutable.</summary>
        void EndCapture(ID3D11DeviceContext* context);
        /// <summary>Binds the shared field and validation inputs immediately before an actual output receiver draw.</summary>
        void BindReceiver(ID3D11DeviceContext* context);
        /// <summary>Releases reserved bindings before UI, unrelated cameras, resize or explicit teardown.</summary>
        static void ClearBindings(ID3D11DeviceContext* context);
    };
}
