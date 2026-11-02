#include "platform/windows/win32/win32_material_rasterizer_state.hpp"
#include "platform/windows/win32/win32_camera_toon_pass.hpp"
#include <DirectXMath.h>
#include <d3dcompiler.h>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace helengine::windows;

/// <summary>Checks authored one-sided geometry through the real toon and baked shader bindings on headless WARP.</summary>
class MaterialCullingTests {
    /// <summary>Owns software rendering resources independent of any application window.</summary>
    ComPtr<ID3D11Device> Device;
    /// <summary>Submits and reads back only this test's synthetic geometry.</summary>
    ComPtr<ID3D11DeviceContext> Context;
    /// <summary>Uses the production immutable material rasterizer cache.</summary>
    Win32MaterialRasterizerState Rasterizers;
    /// <summary>Provides a minimal ordinary forward shader for isolation from toon passes.</summary>
    ComPtr<ID3D11PixelShader> ForwardPixel;

    /// <summary>Preserves failures from actual device operations.</summary>
    static void Check(HRESULT result) {
        if(FAILED(result)) throw std::runtime_error("Culling fixture D3D failure " + std::to_string(result));
    }

    /// <summary>Reports which independently specified material visibility contract was broken.</summary>
    static void Require(bool condition, const std::string& message) {
        if(!condition) throw std::runtime_error(message);
    }

    /// <summary>Uploads IEEE32 shader constants to their existing standard-material slots.</summary>
    void Constants(UINT slot, const std::vector<float>& values) {
        D3D11_BUFFER_DESC description {}; description.ByteWidth = static_cast<UINT>(values.size() * sizeof(float));
        description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA data {values.data(), 0, 0}; ComPtr<ID3D11Buffer> buffer;
        Check(Device->CreateBuffer(&description, &data, &buffer)); auto* binding = buffer.Get();
        Context->VSSetConstantBuffers(slot, 1, &binding); Context->PSSetConstantBuffers(slot, 1, &binding);
    }

    /// <summary>Draws the same inward -Z wall from either side using the engine's right-handed camera convention.</summary>
    bool Draw(Win32CameraToonPass& pass, D3D11_CULL_MODE mode, bool inside, UINT samples, int path) {
        Context->ClearState();
        D3D11_VIEWPORT viewport {0, 0, 16, 16, 0, 1}; pass.Begin(Context.Get(), viewport, samples);
        Rasterizers.Bind(Context.Get(), mode, samples == 4);
        std::vector<float> transforms(76, 0);
        for(int matrix = 0; matrix < 3; ++matrix) for(int diagonal = 0; diagonal < 4; ++diagonal) transforms[matrix * 16 + diagonal * 5] = 1;
        auto eye = DirectX::XMVectorSet(0, 0, inside ? -2.f : 2.f, 1);
        auto view = DirectX::XMMatrixLookAtRH(eye, DirectX::XMVectorZero(), DirectX::XMVectorSet(0, 1, 0, 0));
        auto projection = DirectX::XMMatrixPerspectiveFovRH(DirectX::XM_PIDIV2, 1, .1f, 10);
        DirectX::XMFLOAT4X4 wvp; DirectX::XMStoreFloat4x4(&wvp, DirectX::XMMatrixTranspose(view * projection));
        std::memcpy(transforms.data() + 16, &wvp, sizeof(wvp)); transforms[50] = inside ? -2.f : 2.f;
        Constants(0, transforms); Constants(3, {1, 1, 1, 1}); Constants(1, std::vector<float>(72, 0));
        Constants(7, {0, 0, 0, 0}); Constants(11, {1, 0, 0, 0});
        const unsigned char white[] = {255, 255, 255, 255};
        D3D11_TEXTURE2D_DESC texture {}; texture.Width = texture.Height = texture.MipLevels = texture.ArraySize = texture.SampleDesc.Count = 1;
        texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM; texture.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA pixels {white, 4, 0}; ComPtr<ID3D11Texture2D> diffuse;
        Check(Device->CreateTexture2D(&texture, &pixels, &diffuse)); ComPtr<ID3D11ShaderResourceView> diffuseView;
        Check(Device->CreateShaderResourceView(diffuse.Get(), nullptr, &diffuseView)); auto* diffuseBinding = diffuseView.Get();
        Context->PSSetShaderResources(0, 1, &diffuseBinding); Context->PSSetShaderResources(23, 1, &diffuseBinding);
        D3D11_SAMPLER_DESC sampling {}; sampling.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sampling.AddressU = sampling.AddressV = sampling.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sampling.MaxLOD = D3D11_FLOAT32_MAX;
        ComPtr<ID3D11SamplerState> sampler; Check(Device->CreateSamplerState(&sampling, &sampler)); auto* samplingBinding = sampler.Get();
        Context->PSSetSamplers(0, 1, &samplingBinding); Context->PSSetSamplers(9, 1, &samplingBinding);
        // Clockwise in XY: its geometric cross product and authored normal both point inward along -Z.
        const float vertices[] = {-1,-1,0, 0,0,-1, 0,0, -1,1,0, 0,0,-1, 0,1, 1,1,0, 0,0,-1, 1,1,
            -1,-1,0, 0,0,-1, 0,0, 1,1,0, 0,0,-1, 1,1, 1,-1,0, 0,0,-1, 1,0};
        D3D11_BUFFER_DESC buffer {}; buffer.ByteWidth = sizeof(vertices); buffer.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA data {vertices, 0, 0}; ComPtr<ID3D11Buffer> positions;
        Check(Device->CreateBuffer(&buffer, &data, &positions)); auto* positionBinding = positions.Get(); UINT stride = 32, offset = 0;
        Context->IASetVertexBuffers(0, 1, &positionBinding, &stride, &offset);
        pass.BindRegisteredGeometry(Context.Get(), nullptr);
        if(path == 0) Context->PSSetShader(ForwardPixel.Get(), nullptr, 0);
        if(path == 2) {
            const float uvs[] = {0,0, 0,1, 1,1, 0,0, 1,1, 1,0}; buffer.ByteWidth = sizeof(uvs); data.pSysMem = uvs;
            ComPtr<ID3D11Buffer> uvBuffer; Check(Device->CreateBuffer(&buffer, &data, &uvBuffer)); auto* uvBinding = uvBuffer.Get(); stride = 8;
            Context->IASetVertexBuffers(1, 1, &uvBinding, &stride, &offset); pass.BindLightmapGeometry(Context.Get(), nullptr);
        }
        Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        if(path == 2) Context->Draw(6, 0);
        else {
            const UINT indices[] = {0,1,2,3,4,5}; buffer.ByteWidth = sizeof(indices); buffer.BindFlags = D3D11_BIND_INDEX_BUFFER; data.pSysMem = indices;
            ComPtr<ID3D11Buffer> indexBuffer; Check(Device->CreateBuffer(&buffer, &data, &indexBuffer));
            Context->IASetIndexBuffer(indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0); Context->DrawIndexed(6, 0, 0);
        }
        ComPtr<ID3D11RenderTargetView> targets[3]; ID3D11RenderTargetView* bindings[3] {};
        Context->OMGetRenderTargets(3, bindings, nullptr); for(UINT i = 0; i < 3; ++i) targets[i].Attach(bindings[i]);
        ComPtr<ID3D11Resource> resource; targets[path == 0 ? 0 : 2]->GetResource(&resource); ComPtr<ID3D11Texture2D> output;
        Check(resource.As(&output)); D3D11_TEXTURE2D_DESC description {}; output->GetDesc(&description);
        Context->OMSetRenderTargets(0, nullptr, nullptr);
        if(samples == 4) {
            description.SampleDesc.Count = 1; ComPtr<ID3D11Texture2D> resolved;
            Check(Device->CreateTexture2D(&description, nullptr, &resolved)); Context->ResolveSubresource(resolved.Get(), 0, output.Get(), 0, description.Format); output = resolved;
        }
        description.BindFlags = 0; description.Usage = D3D11_USAGE_STAGING; description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging; Check(Device->CreateTexture2D(&description, nullptr, &staging)); Context->CopyResource(staging.Get(), output.Get());
        D3D11_MAPPED_SUBRESOURCE mapped {}; Check(Context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
        auto* center = static_cast<const unsigned char*>(mapped.pData) + mapped.RowPitch * 8;
        bool visible = false;
        if(path == 0) {for(UINT i = 8 * 8; i < 8 * 8 + 8; ++i) visible = visible || center[i] != 0;}
        else {float coverage; std::memcpy(&coverage, center + 8 * sizeof(float), sizeof(float)); visible = coverage > .5f;}
        Context->Unmap(staging.Get(), 0); return visible;
    }

public:
    /// <summary>Creates a windowless software device and real production rasterizer resources.</summary>
    MaterialCullingTests() {
        Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &Device, nullptr, &Context));
        Rasterizers.Initialize(Device.Get());
        const char shader[] = "float4 main():SV_Target0 {return float4(1,1,1,1);}"; ComPtr<ID3DBlob> code;
        Check(D3DCompile(shader, sizeof(shader), "CullingForward", nullptr, nullptr, "main", "ps_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, nullptr));
        Check(Device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &ForwardPixel));
    }

    /// <summary>Checks all material cull modes, both viewpoints and both coverage modes across indexed and baked draws.</summary>
    void Run() {
        Win32CameraToonPass pass(Device.Get(), 0, 0, 4); const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while(!pass.TryPrepare()) {
            Require(std::chrono::steady_clock::now() < deadline, "Production toon shaders timed out."); std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        for(UINT samples : {1u, 4u}) for(int path : {0, 1, 2}) for(bool inside : {false, true}) {
            const std::string context = " path=" + std::to_string(path) + " samples=" + std::to_string(samples) + " inside=" + std::to_string(inside);
            Require(Draw(pass, D3D11_CULL_NONE, inside, samples, path), "Two-sided material must remain visible" + context);
            Require(Draw(pass, D3D11_CULL_BACK, inside, samples, path) == inside, "Back culling must hide outside and show interior -Z wall" + context);
            Require(Draw(pass, D3D11_CULL_FRONT, inside, samples, path) == !inside, "Front culling must reverse wall visibility" + context);
        }
        std::cout << "PASS material culling: 36 WARP visibility checks; forward, toon, baked; 1x/4x; inside/outside; None/Back/Front\n";
    }
};

/// <summary>Returns the original shader or visibility failure without a window or desktop readback.</summary>
int main() {
    try {MaterialCullingTests tests; tests.Run(); return 0;}
    catch(const std::exception& error) {std::cerr << error.what() << '\n'; return 1;}
}
