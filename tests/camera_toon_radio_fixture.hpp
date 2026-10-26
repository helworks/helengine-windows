#pragma once

#include "platform/windows/win32/win32_camera_toon_pass.hpp"
#include <array>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace helengine::windows {
    /// Renders an explicitly exported radio mesh through the shipped toon pass using owned offscreen resources.
    class CameraToonRadioFixture {
        /// Matches the physical viewport reported for the radio application.
        static constexpr UINT Width = 703;
        /// Retains the physical viewport height independently of its width.
        static constexpr UINT Height = 627;
        /// Selects the explicit diagnostic render resolution while keeping the exported bitmap at the application size.
        int RenderScale;
        /// Selects one or four local geometry samples independently of the diagnostic render scale.
        UINT Samples;
        /// Selects the opt-in pearl surface shader for the fixture material mask.
        bool PearlEnabled = false;
        /// Selects optional effects without changing legacy outline-only fixture output.
        bool EffectsEnabled;
        /// Owns the hardware device without creating a native window or presentation surface.
        Microsoft::WRL::ComPtr<ID3D11Device> Device;
        /// Owns the immediate context used solely for this exported-geometry fixture.
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> Context;
        /// Retains the renderer's standard 304-byte transform/material ABI.
        Microsoft::WRL::ComPtr<ID3D11Buffer> Transforms;
        /// Supplies the authored warm ambient light and the saved directional key light.
        Microsoft::WRL::ComPtr<ID3D11Buffer> Lights;
        /// Supplies each primitive's saved palette as its diffuse base color.
        Microsoft::WRL::ComPtr<ID3D11Buffer> Base;
        /// Supplies emission only for explicitly selected segment materials.
        Microsoft::WRL::ComPtr<ID3D11Buffer> Emission;
        /// Supplies standard roughness for filtered environment sampling.
        Microsoft::WRL::ComPtr<ID3D11Buffer> Roughness;
        /// Supplies a white diffuse texel independently of external textures or user media.
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> WhiteTexture;
        /// Samples the fixture-owned white diffuse texel.
        Microsoft::WRL::ComPtr<ID3D11SamplerState> Sampler;
        /// Preserves ordinary depth testing between the radio's overlapping surfaces.
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> Depth;
        /// Draws all exported triangles without culling and with multisampling enabled.
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> Rasterizer;
        /// Receives the hardware MSAA resolve, or aliases the source directly when local sampling is one.
        Microsoft::WRL::ComPtr<ID3D11Texture2D> Resolved;
        /// Exposes only the owned resolved source to the diagnostic downsampling shader.
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ResolvedView;
        /// Retains the final application-size output independently of the supersampled source.
        Microsoft::WRL::ComPtr<ID3D11Texture2D> Output;
        /// Receives the diagnostic GPU box resolve when the render scale is two.
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> OutputTarget;
        /// Produces a fullscreen triangle for the diagnostic 2 by 2 sample resolve.
        Microsoft::WRL::ComPtr<ID3D11VertexShader> DownsampleVertex;
        /// Averages exactly four resolved source texels without filtering neighboring output pixels.
        Microsoft::WRL::ComPtr<ID3D11PixelShader> DownsamplePixel;
        /// Disables depth reads and writes while resolving the owned supersampled image.
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> DownsampleDepth;
        /// Owns one immutable expanded triangle buffer per exported primitive.
        std::vector<Microsoft::WRL::ComPtr<ID3D11Buffer>> Vertices;
        /// Records the number of vertices in each corresponding primitive buffer.
        std::vector<UINT> VertexCounts;
        /// Records the authored RGBA palette entry corresponding to each primitive.
        std::vector<std::array<float, 4>> Colors;
        /// Retains explicit reflection and selected-emission weights from GRADIO11 material metadata.
        std::vector<std::array<float, 3>> MaterialWeights;

        /// Creates the hardware fixture and validates every byte of the explicit geometry export.
        explicit CameraToonRadioFixture(const std::string& geometryPath, int renderScale, UINT samples, bool effectsEnabled, bool pearlEnabled);
        /// Preserves the precise failing graphics operation and its HRESULT.
        static void Check(HRESULT result, const char* operation);
        /// Reads exactly the requested bytes or reports a truncated geometry export.
        static void ReadExact(std::ifstream& input, void* destination, std::size_t bytes);
        /// Decodes an explicitly little-endian unsigned integer independently of host byte order.
        static std::uint32_t ReadUnsigned(std::ifstream& input);
        /// Decodes one finite IEEE32 scalar from the export's little-endian wire format.
        static float ReadScalar(std::ifstream& input);
        /// Creates an aligned standard-material constant buffer with the specified byte size.
        Microsoft::WRL::ComPtr<ID3D11Buffer> CreateBuffer(UINT bytes);
        /// Loads bounded expanded triangles and rejects malformed, nonfinite or trailing data.
        void LoadGeometry(const std::string& geometryPath);
        /// Binds the real radio transform, perspective projection and explicit emission-only material state.
        void PrepareMaterial(float offsetPixels);
        /// Draws every exported body primitive through the supplied production toon helper.
        void Draw(Win32CameraToonPass& pass);
        /// Creates matching hardware-resolve resources and the optional diagnostic GPU downsampler.
        void CreateResolveResources(ID3D11Texture2D* source);
        /// Resolves MSAA and optionally averages each exact 2 by 2 source footprint on the GPU.
        void ResolveFrame(ID3D11RenderTargetView* destination);
        /// Renders one frame and completes its diagnostic GPU resolve into the application-size output.
        void DrawFrame(Win32CameraToonPass& pass, ID3D11RenderTargetView* destination, float offsetPixels);
        /// Measures a warmed radio toon pass including any required MSAA and diagnostic 2 by 2 GPU resolves.
        void Measure(Win32CameraToonPass& pass, ID3D11RenderTargetView* destination, float offsetPixels);
        /// Reads only the application-size owned output and writes its pixels over gray as an explicit BMP artifact.
        void Save(ID3D11Texture2D* destination, const std::string& outputPath);
        /// Creates the owned destination, records GPU timing and saves the explicit radio fixture image.
        void Run(const std::string& outputPath, float outsidePixels, float insidePixels, float offsetPixels);

    public:
        /// Saves 703 by 627 pixels with one or four local samples; scale two adds an explicit diagnostic GPU supersampling resolve.
        static void Render(const std::string& geometryPath, const std::string& outputPath,
            float outsidePixels, float insidePixels, float offsetPixels = 0.0f, int renderScale = 1, UINT samples = 4, bool effectsEnabled = false, bool pearlEnabled = false);
    };
}
