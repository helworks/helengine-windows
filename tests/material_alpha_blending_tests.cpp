#include "platform/windows/win32/win32_camera_toon_pass.hpp"
#include "platform/windows/win32/win32_material_output_state.hpp"
#include <DirectXPackedVector.h>
#include <chrono>
#include <cmath>
#include <cstring>
#include <d3dcompiler.h>
#include <iostream>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace helengine::windows;

/// <summary>Exercises ordinary and actual toon/baked transparent draws on windowless WARP attachments.</summary>
class MaterialAlphaBlendingTests {
    /// <summary>Owns the software rendering device used by all cases.</summary>
    ComPtr<ID3D11Device> Device;
    /// <summary>Submits only synthetic fixture geometry.</summary>
    ComPtr<ID3D11DeviceContext> Context;
    /// <summary>Uses exactly the fixed-function output states bound by the production renderer.</summary>
    Win32MaterialOutputState States;
    /// <summary>Provides a straight-alpha ordinary shader matching the packaged standard shader contract.</summary>
    ComPtr<ID3D11PixelShader> Forward;
    /// <summary>Provides actual depth testing for the pre-fix regression reproduction.</summary>
    ComPtr<ID3D11DepthStencilState> OriginalDepth;
    /// <summary>Selects Gevo material eligibility while retaining the original globally enabled registered-pane cases.</summary>
    bool SelectiveOutlines = false;
    /// <summary>Records resolved-color error against separated coverage at mixed samples containing glass.</summary>

    /// <summary>Preserves native resource and shader failures with their original status code.</summary>
    static void Check(HRESULT result) {
        if (FAILED(result))
            throw std::runtime_error("Alpha fixture D3D failure " + std::to_string(result));
    }
    /// <summary>Reports an independently specified composition or occlusion requirement.</summary>
    static void Require(bool condition, const std::string &message) {
        if (!condition)
            throw std::runtime_error(message);
    }
    /// <summary>Uploads an IEEE32 buffer to standard shader slots.</summary>
    void Constants(UINT slot, const std::vector<float> &values) {
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = static_cast<UINT>(values.size() * sizeof(float));
        description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA data{values.data(), 0, 0};
        ComPtr<ID3D11Buffer> buffer;
        Check(Device->CreateBuffer(&description, &data, &buffer));
        auto *binding = buffer.Get();
        Context->VSSetConstantBuffers(slot, 1, &binding);
        Context->PSSetConstantBuffers(slot, 1, &binding);
    }
    /// <summary>Reads a center pixel from an owned floating-point render target, resolving color samples when
    /// necessary.</summary>
    std::vector<float> ReadFloat(ID3D11RenderTargetView *target, UINT samples, UINT pixelX = 8) {
        ComPtr<ID3D11Resource> resource;
        target->GetResource(&resource);
        ComPtr<ID3D11Texture2D> texture;
        Check(resource.As(&texture));
        D3D11_TEXTURE2D_DESC description{};
        texture->GetDesc(&description);
        if (samples > 1) {
            description.SampleDesc.Count = 1;
            ComPtr<ID3D11Texture2D> resolved;
            Check(Device->CreateTexture2D(&description, nullptr, &resolved));
            Context->ResolveSubresource(resolved.Get(), 0, texture.Get(), 0, description.Format);
            texture = resolved;
        }
        description.BindFlags = 0;
        description.Usage = D3D11_USAGE_STAGING;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        Check(Device->CreateTexture2D(&description, nullptr, &staging));
        Context->CopyResource(staging.Get(), texture.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Check(Context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
        const auto *row = static_cast<const unsigned char *>(mapped.pData) + 8 * mapped.RowPitch;
        std::vector<float> values(4, 0);
        if (description.Format == DXGI_FORMAT_R32_FLOAT)
            std::memcpy(values.data(), row + pixelX * sizeof(float), sizeof(float));
        else if (description.Format == DXGI_FORMAT_R8G8B8A8_UNORM) {
            for (int channel = 0; channel < 4; ++channel)
                values[channel] = row[pixelX * 4 + channel] / 255.f;
        } else {
            const auto *pixel = reinterpret_cast<const uint16_t *>(row) + pixelX * 4;
            for (int channel = 0; channel < 4; ++channel)
                values[channel] = DirectX::PackedVector::XMConvertHalfToFloat(pixel[channel]);
        }
        Context->Unmap(staging.Get(), 0);
        return values;
    }
    /// <summary>Reads every integer part sample through a compute copy, never blending or resolving integer
    /// identities.</summary>
    std::vector<uint32_t> ReadParts(ID3D11RenderTargetView *target, UINT samples) {
        ComPtr<ID3D11Resource> texture;
        target->GetResource(&texture);
        ComPtr<ID3D11ShaderResourceView> view;
        Check(Device->CreateShaderResourceView(texture.Get(), nullptr, &view));
        const std::string source = std::string(samples == 4 ? "Texture2DMS<uint,4> Parts:register(t0);"
                                                            : "Texture2D<uint> Parts:register(t0);") +
                                   "RWStructuredBuffer<uint> Result:register(u0);[numthreads(1,1,1)]void main(){" +
                                   (samples == 4 ? "for(uint i=0;i<4;++i) Result[i]=Parts.Load(int2(8,8),i);"
                                                 : "Result[0]=Parts.Load(int3(8,8,0));") +
                                   "}";
        ComPtr<ID3DBlob> code;
        Check(D3DCompile(source.data(), source.size(), "IntegerPartReadback", nullptr, nullptr, "main", "cs_5_0",
                         D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, nullptr));
        ComPtr<ID3D11ComputeShader> shader;
        Check(Device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader));
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = 16;
        description.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        description.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        description.StructureByteStride = 4;
        ComPtr<ID3D11Buffer> output;
        Check(Device->CreateBuffer(&description, nullptr, &output));
        ComPtr<ID3D11UnorderedAccessView> uav;
        Check(Device->CreateUnorderedAccessView(output.Get(), nullptr, &uav));
        ID3D11RenderTargetView *bindings[6]{};
        ComPtr<ID3D11DepthStencilView> depth;
        Context->OMGetRenderTargets(6, bindings, &depth);
        Context->OMSetRenderTargets(0, nullptr, nullptr);
        auto *srv = view.Get();
        auto *writable = uav.Get();
        Context->CSSetShader(shader.Get(), nullptr, 0);
        Context->CSSetShaderResources(0, 1, &srv);
        Context->CSSetUnorderedAccessViews(0, 1, &writable, nullptr);
        Context->Dispatch(1, 1, 1);
        srv = nullptr;
        writable = nullptr;
        Context->CSSetShaderResources(0, 1, &srv);
        Context->CSSetUnorderedAccessViews(0, 1, &writable, nullptr);
        Context->CSSetShader(nullptr, nullptr, 0);
        Context->OMSetRenderTargets(6, bindings, depth.Get());
        for (auto *binding : bindings)
            if (binding)
                binding->Release();
        description.BindFlags = 0;
        description.MiscFlags = 0;
        description.StructureByteStride = 0;
        description.Usage = D3D11_USAGE_STAGING;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Buffer> staging;
        Check(Device->CreateBuffer(&description, nullptr, &staging));
        Context->CopyResource(staging.Get(), output.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Check(Context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
        std::vector<uint32_t> result(samples);
        std::memcpy(result.data(), mapped.pData, samples * sizeof(uint32_t));
        Context->Unmap(staging.Get(), 0);
        return result;
    }
    /// <summary>Draws a full-frame standard-material pane at a chosen depth and alpha.</summary>
    void Draw(Win32CameraToonPass &pass, int path, float depth, const std::vector<float> &color, bool transparent, bool unregistered = false, const void* owner = nullptr) {
        std::vector<float> transforms(76, 0);
        for (int matrix = 0; matrix < 3; ++matrix)
            for (int diagonal = 0; diagonal < 4; ++diagonal)
                transforms[matrix * 16 + diagonal * 5] = 1;
        transforms[27] = depth;
        transforms[11] = depth;
        transforms[50] = 2;
        transforms[54] = transparent ? 1.f : 0.f;
        Constants(0, transforms);
        Constants(3, color);
        Constants(1, {1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
        Constants(7, {0, 0, 0, 0});
        Constants(11, {1, 0, 0, 0});
        Constants(4, {1, 0, 0, 0});
        const void *material = (transparent && SelectiveOutlines) || unregistered ? static_cast<const void *>(&States) : this;
        pass.BindRegisteredGeometry(Context.Get(), material, SelectiveOutlines || unregistered, transparent, owner);
        if (path == 0)
            Context->PSSetShader(Forward.Get(), nullptr, 0);
        if (path == 2)
            pass.BindLightmapGeometry(Context.Get(), material);
        States.Bind(Context.Get(), transparent, true, true, path != 0, pass.WritesAuxiliaryContours(material));
        Context->Draw(6, 0);
    }
    /// <summary>Checks actual color math, opaque foreground depth, auxiliary preservation and read-only glass
    /// depth.</summary>
    void Case(Win32CameraToonPass &pass, UINT samples, int path, bool foreground, bool reverseSubmission) {
        Context->ClearState();
        D3D11_VIEWPORT viewport{0, 0, 16, 16, 0, 1};
        pass.Begin(Context.Get(), viewport, samples);
        Context->OMSetDepthStencilState(OriginalDepth.Get(), 0);
        D3D11_RASTERIZER_DESC raster{};
        raster.FillMode = D3D11_FILL_SOLID;
        raster.CullMode = D3D11_CULL_NONE;
        raster.DepthClipEnable = TRUE;
        raster.MultisampleEnable = samples > 1;
        ComPtr<ID3D11RasterizerState> rasterizer;
        Check(Device->CreateRasterizerState(&raster, &rasterizer));
        Context->RSSetState(rasterizer.Get());
        const float vertices[] = {-1, -1, 0, 0, 0, 1, 0, 0, -1, 1, 0, 0, 0, 1, 0, 1, 1, 1,  0, 0, 0, 1, 1, 1,
                                  -1, -1, 0, 0, 0, 1, 0, 0, 1,  1, 0, 0, 0, 1, 1, 1, 1, -1, 0, 0, 0, 1, 1, 0};
        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth = sizeof(vertices);
        buffer.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA data{vertices, 0, 0};
        ComPtr<ID3D11Buffer> vertex;
        Check(Device->CreateBuffer(&buffer, &data, &vertex));
        auto *binding = vertex.Get();
        UINT stride = 32, offset = 0;
        Context->IASetVertexBuffers(0, 1, &binding, &stride, &offset);
        const float uvs[] = {0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0};
        buffer.ByteWidth = sizeof(uvs);
        data.pSysMem = uvs;
        ComPtr<ID3D11Buffer> uv;
        Check(Device->CreateBuffer(&buffer, &data, &uv));
        binding = uv.Get();
        stride = 8;
        Context->IASetVertexBuffers(1, 1, &binding, &stride, &offset);
        Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        const uint8_t white[] = {255, 255, 255, 255};
        D3D11_TEXTURE2D_DESC texture{};
        texture.Width = texture.Height = texture.MipLevels = texture.ArraySize = texture.SampleDesc.Count = 1;
        texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texture.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA pixels{white, 4, 0};
        ComPtr<ID3D11Texture2D> diffuse;
        Check(Device->CreateTexture2D(&texture, &pixels, &diffuse));
        ComPtr<ID3D11ShaderResourceView> view;
        Check(Device->CreateShaderResourceView(diffuse.Get(), nullptr, &view));
        auto *resource = view.Get();
        Context->PSSetShaderResources(0, 1, &resource);
        Context->PSSetShaderResources(23, 1, &resource);
        D3D11_SAMPLER_DESC sampling{};
        sampling.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sampling.AddressU = sampling.AddressV = sampling.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampling.MaxLOD = D3D11_FLOAT32_MAX;
        ComPtr<ID3D11SamplerState> sampler;
        Check(Device->CreateSamplerState(&sampling, &sampler));
        auto *sample = sampler.Get();
        Context->PSSetSamplers(0, 1, &sample);
        Context->PSSetSamplers(9, 1, &sample);
        // Simulate either registration order; the existing camera queue always visits the opaque draw first.
        std::vector<bool> submitted =
            reverseSubmission ? std::vector<bool>{true, false} : std::vector<bool>{false, true};
        for (bool transparent : submitted)
            if (!transparent)
                Draw(pass, path, foreground ? .1f : .8f, {.2f, .4f, .6f, 1}, false);
        ComPtr<ID3D11RenderTargetView> targets[6];
        ID3D11RenderTargetView *bindings[6]{};
        Context->OMGetRenderTargets(6, bindings, nullptr);
        for (int i = 0; i < 6; ++i)
            targets[i].Attach(bindings[i]);
        const auto opaque = ReadFloat(targets[0].Get(), samples);
        const auto parts = ReadParts(targets[1].Get(), samples);
        const auto position = ReadFloat(targets[4].Get(), samples);
        const auto normal = ReadFloat(targets[5].Get(), samples);
        const float glowSeed[] = {.25f, .125f, .5f, .5f};
        Context->ClearRenderTargetView(targets[3].Get(), glowSeed);
        const auto glow = ReadFloat(targets[3].Get(), samples);
        for (bool transparent : submitted)
            if (transparent)
                Draw(pass, path, .4f, {.8f, .2f, .1f, .25f}, true);
        const auto result = ReadFloat(targets[0].Get(), samples);
        const std::string description = " path=" + std::to_string(path) + " samples=" + std::to_string(samples) +
                                        " foreground=" + std::to_string(foreground) +
                                        " reversed=" + std::to_string(reverseSubmission) +
                                        " selective=" + std::to_string(SelectiveOutlines);
        for (int channel = 0; channel < 3; ++channel) {
            float expected = foreground ? opaque[channel]
                                        : opaque[channel] * .75f + std::vector<float>{.8f, .2f, .1f}[channel] * .25f /
                                                                       (path == 2 ? 3.14159265f : 1.f);
            Require(std::abs(result[channel] - expected) < .003f,
                    "Glass must compose over opaque, with foreground occlusion" + description +
                        " channel=" + std::to_string(channel) + " got=" + std::to_string(result[channel]) +
                        " expected=" + std::to_string(expected));
        }
        Require(std::abs(result[3] - 1) < .003f, "Glass must retain accumulated opaque alpha" + description);
        if (path != 0) {
            Require(parts[0] != 0, "Fixture requires a real opaque toon identity" + description);
            Require(ReadParts(targets[1].Get(), samples) == parts,
                    "Glass must retain every opaque integer part sample" + description);
            Require(ReadFloat(targets[3].Get(), samples) == glow,
                    "Clear glass must retain underlying selected emission" + description);
            Require(ReadFloat(targets[4].Get(), samples) == position,
                    "Glass must retain opaque geometry position" + description);
            Require(ReadFloat(targets[5].Get(), samples) == normal,
                    "Glass must retain opaque geometry normal" + description);
            Require(ReadFloat(targets[2].Get(), samples)[0] == 1,
                    "Glass must retain opaque outline coverage" + description);
        }
        // A later opaque object behind the glass must still pass: transparent geometry never wrote depth.
        if (!foreground) {
            Draw(pass, path, .6f, {.1f, .8f, .3f, 1}, false);
            auto behind = ReadFloat(targets[0].Get(), samples);
            Require(behind[1] > (path == 2 ? .25f : .79f),
                    "Glass must not occlude later geometry by writing depth" + description);
        }
        // Final contours must survive either pane eligibility mode, beyond preserving intermediate ownership targets.
        if (path != 0) {
            D3D11_VIEWPORT halfViewport{0, 0, 8, 16, 0, 1};
            Context->RSSetViewports(1, &halfViewport);
            Draw(pass, path, .05f, {.8f, .8f, .8f, 1}, false);
            Context->RSSetViewports(1, &viewport);
            Draw(pass, path, .01f, {.8f, .2f, .1f, .0627451f}, true);
            D3D11_TEXTURE2D_DESC output{};
            output.Width = output.Height = 16;
            output.MipLevels = output.ArraySize = output.SampleDesc.Count = 1;
            output.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            output.BindFlags = D3D11_BIND_RENDER_TARGET;
            ComPtr<ID3D11Texture2D> texture;
            Check(Device->CreateTexture2D(&output, nullptr, &texture));
            ComPtr<ID3D11RenderTargetView> target;
            Check(Device->CreateRenderTargetView(texture.Get(), nullptr, &target));
            const float clear[] = {0, 0, 0, 0};
            Context->ClearRenderTargetView(target.Get(), clear);
            pass.Composite(Context.Get(), target.Get(), nullptr, viewport);
            const auto outlined = ReadFloat(target.Get(), 1, 7);
            Require(outlined[0] < .05f && outlined[1] < .05f && outlined[2] < .05f,
                    "Clear glass must preserve the actual composed opaque part interface" + description);
            // A transparent half-pane needs its own edge while leaving its interior and opaque edges intact.
            pass.Begin(Context.Get(), viewport, samples);
            Context->RSSetState(rasterizer.Get());
            Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            Draw(pass, path, .8f, {.8f, .8f, .8f, 1}, false);
            Context->RSSetViewports(1, &halfViewport);
            Draw(pass, path, .4f, {.8f, .2f, .1f, .0627451f}, true);
            Context->RSSetViewports(1, &viewport);
            Context->ClearRenderTargetView(target.Get(), clear);
            pass.Composite(Context.Get(), target.Get(), nullptr, viewport);
            const auto glassEdge = ReadFloat(target.Get(), 1);
            Require(glassEdge[0] > .05f,
                    "Visual glass must never contribute contour geometry" + description);
            Require(ReadFloat(target.Get(), 1, 4)[0] > .05f,
                    "Transparent contour pass must leave the pane interior visible" + description);
            // Outline-only geometry must retain its contour at alpha zero without altering the scene interior.
            if (!SelectiveOutlines) {
                for (bool occluded : {false, true}) {
                    pass.Begin(Context.Get(), viewport, samples);
                    Context->RSSetState(rasterizer.Get());
                    Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    Draw(pass, path, occluded ? .1f : .8f, {.8f, .8f, .8f, 1}, false);
                    const auto unchanged = ReadFloat(targets[0].Get(), samples);
                    pass.ConfigureMaterialEffects(this, 0, 0, false, true, 1, 0, true);
                    // Outer and inner proxy regions share one material but must retain distinct part identities.
                    Draw(pass, path, .45f, {1, 0, 0, 0}, true, false, this);
                    Context->RSSetViewports(1, &halfViewport);
                    Draw(pass, path, .4f, {1, 0, 0, 0}, true, false, this);
                    Require(ReadFloat(targets[0].Get(), samples)==unchanged,
                        "Outline-only shade modified scene color or opacity" + description);
                    Context->RSSetViewports(1, &viewport);
                    // The visible white glass is independent and may be submitted after its contour proxy.
                    pass.ConfigureMaterialEffects(&States, 0, 0, false, false, 0);
                    Draw(pass, path, .3f, {1, 1, 1, .125f}, true, true);
                    pass.RemoveMaterialEffects(&States);
                    Context->ClearRenderTargetView(target.Get(), clear);
                    pass.Composite(Context.Get(), target.Get(), nullptr, viewport);
                    const auto edgeOnly=ReadFloat(target.Get(),1);
                    Require(edgeOnly[0]>.05f,
                        "Auxiliary mask must not draw internal submesh boundaries" + description);
                    Require(ReadFloat(target.Get(),1,4)[0]>.05f,"Outline-only shade filled its interior" + description);
                    pass.ConfigureMaterialEffects(this, 0, 0, false, true);
                }
            }
            // An outline-only shade hides an opaque seam behind it without changing the glass color.
            if(!SelectiveOutlines) {
                for(bool nearSeam : {false,true}) {
                    pass.Begin(Context.Get(),viewport,samples);Context->RSSetState(rasterizer.Get());
                    Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    Draw(pass,path,.95f,{.8f,.8f,.8f,1},false);
                    Context->RSSetViewports(1,&halfViewport);Draw(pass,path,nearSeam ? .1f : .6f,{.8f,.8f,.8f,1},false);
                    Context->RSSetViewports(1,&viewport);pass.ConfigureMaterialEffects(this,0,0,false,true,1,0,true);
                    Draw(pass,path,.4f,{1,1,1,0},true);
                    Context->ClearRenderTargetView(target.Get(),clear);pass.Composite(Context.Get(),target.Get(),nullptr,viewport);
                    float value=ReadFloat(target.Get(),1,7)[0];
                    Require(nearSeam ? value<.05f : value>.05f,"Outline proxy must hide only the opaque ink behind it"+description+" nearer="+std::to_string(nearSeam));
                    pass.ConfigureMaterialEffects(this,0,0,false,true);
                }
            }
            // A silhouette may expand onto a wall behind it, never onto an adjacent nearer opaque surface.
            if(!SelectiveOutlines) {
                for(bool nearer : {false,true}) {
                    pass.Begin(Context.Get(),viewport,samples);Context->RSSetState(rasterizer.Get());
                    Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    Draw(pass,path,.9f,{.8f,.8f,.8f,1},false,true);
                    D3D11_VIEWPORT rightHalf{8,0,8,16,0,1};Context->RSSetViewports(1,&rightHalf);
                    Draw(pass,path,nearer ? .1f : .8f,{.8f,.8f,.8f,1},false,true);
                    Context->RSSetViewports(1,&halfViewport);pass.ConfigureMaterialEffects(this,0,0,false,true,1,0,true);Draw(pass,path,.4f,{1,1,1,0},true);
                    Context->RSSetViewports(1,&viewport);Context->ClearRenderTargetView(target.Get(),clear);
                    pass.Composite(Context.Get(),target.Get(),nullptr,viewport);
                    float value=ReadFloat(target.Get(),1,7)[0];
                    Require(nearer ? value>.05f : value<.05f,"Expanded auxiliary ink must compare destination foreground depth"+description+" nearer="+std::to_string(nearer));
                    pass.ConfigureMaterialEffects(this,0,0,false,true);
                }
            }
            // Depth rejection and later opaque coverage must both suppress invisible glass edges.
            for (bool lateOpaque : {false, true}) {
                pass.Begin(Context.Get(), viewport, samples);
                Context->RSSetState(rasterizer.Get());
                Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                Draw(pass, path, lateOpaque ? .8f : .1f, {.8f, .8f, .8f, 1}, false);
                Context->RSSetViewports(1, &halfViewport);
                Draw(pass, path, .4f, {.8f, .2f, .1f, .0627451f}, true);
                Context->RSSetViewports(1, &viewport);
                if (lateOpaque) Draw(pass, path, .1f, {.8f, .8f, .8f, 1}, false);
                Context->ClearRenderTargetView(target.Get(), clear);
                pass.Composite(Context.Get(), target.Get(), nullptr, viewport);
                Require(ReadFloat(target.Get(), 1)[0] > .05f,
                        "Opaque foreground must hide transparent silhouettes regardless of submission order" + description);
            }
            for (float paneAlpha : {0.f, 16.f / 255.f, .5f}) {
                for (bool hdr : {false, true}) {
                    pass.Begin(Context.Get(), viewport, samples);
                    Context->RSSetState(rasterizer.Get());
                    Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    if (foreground)
                        Draw(pass, path, .9f, {.3f, .4f, .5f, 1}, false, true);
                    float edgeVertices[std::size(vertices)];
                    std::memcpy(edgeVertices, vertices, sizeof(vertices));
                    for (UINT index = 0; index < std::size(edgeVertices); index += 8)
                        edgeVertices[index] = (edgeVertices[index] + 1) * (reverseSubmission ? 8.25f : 8.f) / 16 - 1;
                    Context->UpdateSubresource(vertex.Get(), 0, nullptr, edgeVertices, 0, 0);
                    Draw(pass, path, .4f, {.8f, .8f, .8f, 1}, false);
                    Context->UpdateSubresource(vertex.Get(), 0, nullptr, vertices, 0, 0);
                    Draw(pass, path, .2f, {1, 1, 1, paneAlpha}, true);
                    const auto source = ReadFloat(targets[0].Get(), samples);
                    const auto identities = ReadParts(targets[1].Get(), samples);
                    UINT owned = 0;
                    for (uint32_t identity : identities)
                        if (identity != 0) ++owned;
                    output.Format = hdr ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
                    Check(Device->CreateTexture2D(&output, nullptr, texture.ReleaseAndGetAddressOf()));
                    Check(Device->CreateRenderTargetView(texture.Get(), nullptr, target.ReleaseAndGetAddressOf()));
                    Context->ClearRenderTargetView(target.Get(), clear);
                    pass.Composite(Context.Get(), target.Get(), nullptr, viewport);
                    const auto external = ReadFloat(target.Get(), 1, 9);
                    const auto edge = ReadFloat(target.Get(), 1);
                    const std::string silhouette = description + " glass=" + std::to_string(paneAlpha) +
                                                   " hdr=" + std::to_string(hdr);
                    const float ink[] = {hdr ? .012f / 12.92f : .012f,
                                         hdr ? .016f / 12.92f : .016f,
                                         hdr ? .022f / 12.92f : .022f};
                    const auto interior=ReadFloat(target.Get(),1,7);
                    for(int channel=0;channel<3;++channel)
                        Require(std::abs(interior[channel]-ink[channel])<.004f,"Near-side contour must remain visible through excluded glass"+silhouette);
                    Require(foreground || paneAlpha>0 ? external[0]>.01f : external[3]<.004f,
                        "Depth outlines must not expand onto background pixels"+silhouette);
                    const float expectedAlpha=foreground ? 1.f : owned/static_cast<float>(samples)+paneAlpha*(1-owned/static_cast<float>(samples));
                    Require(std::abs(edge[3]-expectedAlpha)<.004f,"Depth ink must preserve existing edge alpha"+silhouette);
                    for(float channel:edge) Require(std::isfinite(channel) && channel>=0 && channel<=1.004f,
                        "Depth edge must retain finite premultiplied channels"+silhouette);

                }
            }
        }
    }

  public:
    /// <summary>Creates an offscreen software device and the ordinary straight-alpha shader.</summary>
    MaterialAlphaBlendingTests() {
        Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &Device,
                                nullptr, &Context));
        States.Initialize(Device.Get());
        D3D11_DEPTH_STENCIL_DESC depth{};
        depth.DepthEnable = TRUE;
        depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        Check(Device->CreateDepthStencilState(&depth, &OriginalDepth));
        const char shader[] =
            "cbuffer Color:register(b3){float4 BaseColor;} float4 main():SV_Target0{return BaseColor;}";
        ComPtr<ID3DBlob> code;
        Check(D3DCompile(shader, sizeof(shader), "ForwardAlphaFixture", nullptr, nullptr, "main", "ps_5_0",
                         D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, nullptr));
        Check(Device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &Forward));
    }
    /// <summary>Checks all three rendering paths at both supported sample counts and submission orders.</summary>
    void Run() {
        Win32CameraToonPass pass(Device.Get(), 2, 2, 4);

        pass.ConfigureMaterialEffects(this, 0, 0, false, true);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (!pass.TryPrepare()) {
            Require(std::chrono::steady_clock::now() < deadline, "Toon shader preparation timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        for (bool selective : {false, true}) {
            SelectiveOutlines = selective;
            CameraToonEffectsConfiguration effects;
            effects.ToonEnabled = !selective;
            pass.ConfigureEffects(effects);
            for (UINT samples : {1u, 4u})
                for (int path : {0, 1, 2})
                    for (bool foreground : {false, true})
                        for (bool reverse : {false, true})
                            Case(pass, samples, path, foreground, reverse);
        }
        std::cout << "PASS outline-only: 32 alpha-zero cases; shared-mesh seams suppressed through visible white glass; unchanged interior; foreground occlusion; toon/baked; 1x/4x\n";
        std::cout << "PASS material alpha: 48 WARP cases; forward/toon/baked; 1x/4x; foreground/background; both "
                     "submission orders; global/registered and selective/unregistered; 32 opaque and 96 transparent contour/occlusion composites; 192 near-side depth silhouette cases at 0/6/50-percent glass, SDR/HDR\n";
    }
};
/// <summary>Returns the original failed composition contract without creating a window or capturing the
/// desktop.</summary>
int main() {
    try {
        MaterialAlphaBlendingTests tests;
        tests.Run();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
