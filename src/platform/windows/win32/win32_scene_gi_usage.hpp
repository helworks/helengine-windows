#pragma once
#include <d3d11_1.h>
#include <wrl/client.h>
#include <cstdint>
namespace helengine::windows {
    /// <summary>Owns diagnostic-only full-resolution receiver visibility and the union of accepted voxel-reader taps.</summary>
    class Win32SceneGiUsage {
        /// <summary>Retains the graphics device for viewport-dependent depth allocation.</summary>
        Microsoft::WRL::ComPtr<ID3D11Device> Device;
        /// <summary>Stores one atomic usage integer per physical field cell.</summary>
        Microsoft::WRL::ComPtr<ID3D11Buffer> Mask;
        /// <summary>Allows reader fragments to mark the shared mask without CPU synchronization.</summary>
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> WriteView;
        /// <summary>Lets the sphere vertex shader reject probes with no accepted receiver taps.</summary>
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ReadView;
        /// <summary>Isolates all diagnostic resource bindings from ordinary rendering.</summary>
        Microsoft::WRL::ComPtr<ID3DDeviceContextState> DiagnosticState;
        /// <summary>Retains the exact caller state only between Begin and End.</summary>
        Microsoft::WRL::ComPtr<ID3DDeviceContextState> PreviousState;
        /// <summary>Keeps the active immediate context alive until its complete state is restored.</summary>
        Microsoft::WRL::ComPtr<ID3D11DeviceContext1> ActiveContext;
        /// <summary>Retains the exact published atlas used by this tracking draw.</summary>
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> Atlas;
        /// <summary>Retains the matching volume constants through both camera passes.</summary>
        Microsoft::WRL::ComPtr<ID3D11Buffer> Field;
        /// <summary>Transforms the renderer's standard mesh vertices identically to normal camera geometry.</summary>
        Microsoft::WRL::ComPtr<ID3D11VertexShader> Vertex;
        /// <summary>Writes visibility depth using the normal material alpha cutoff.</summary>
        Microsoft::WRL::ComPtr<ID3D11PixelShader> DepthPixel;
        /// <summary>Records sampler taps only after early depth equality and material coverage succeed.</summary>
        Microsoft::WRL::ComPtr<ID3D11PixelShader> ReaderPixel;
        /// <summary>Matches the normal position-normal-UV vertex layout.</summary>
        Microsoft::WRL::ComPtr<ID3D11InputLayout> Layout;
        /// <summary>Uses ordinary less-equal depth writes for the visibility prepass.</summary>
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> WriteDepth;
        /// <summary>Rejects hidden receivers before shader execution without modifying depth.</summary>
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> EqualDepth;
        /// <summary>Matches ordinary solid, two-sided, multisample-capable geometry rasterization.</summary>
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> Rasterizer;
        /// <summary>Matches the ordinary four-sample rasterizer without changing single-sample coverage. </summary>
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> MultisampleRasterizer;
        /// <summary>Retains one reusable camera-local depth attachment.</summary>
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> Depth;
        /// <summary>Tracks the allocated camera viewport width.</summary>
        UINT Width=0;
        /// <summary>Tracks the allocated camera viewport height.</summary>
        UINT Height=0;
        /// <summary>Tracks exact one- or four-sample camera coverage.</summary>
        UINT Samples=0;
        /// <summary>Selects the visibility or reader shader for subsequent actual mesh submissions.</summary>
        bool Reading=false;
        /// <summary>Creates complete replacement depth resources before changing current dimensions.</summary>
        void Resize(UINT width,UINT height,UINT samples);
    public:
        /// <summary>Allocates persistent mask and diagnostic shaders; unsupported context-state facilities fail visibly.</summary>
        Win32SceneGiUsage(ID3D11Device* device,std::uint32_t cellCount);
        /// <summary>Restores an active caller context if an owner is torn down during exception unwinding.</summary>
        ~Win32SceneGiUsage();
        /// <summary>Captures the complete caller pipeline, clears stale usage, and borrows one exact published field.</summary>
        void Begin(ID3D11DeviceContext* context,ID3D11ShaderResourceView* atlas,ID3D11Buffer* parameters);
        /// <summary>Clears this camera's own depth without clearing usage already recorded by another camera.</summary>
        void BeginCamera(float width,float height,UINT samples);
        /// <summary>Switches the completed camera visibility prepass to early depth-equal usage recording.</summary>
        void BeginReaders();
        /// <summary>Overrides only mesh shaders/layout after ordinary material texture and transform binding.</summary>
        void BindGeometry();
        /// <summary>Unbinds diagnostic UAVs and restores every caller pipeline binding without a GPU readback.</summary>
        void End();
        /// <summary>Returns the GPU-only usage mask, initially empty and replaced on every diagnostic draw.</summary>
        ID3D11ShaderResourceView* GetView() const;
    };
}
