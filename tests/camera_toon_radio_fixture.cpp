#include "camera_toon_radio_fixture.hpp"
#include <DirectXMath.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <thread>

namespace helengine::windows {
    CameraToonRadioFixture::CameraToonRadioFixture(const std::string& geometryPath, int renderScale, UINT samples, bool effectsEnabled, bool pearlEnabled)
        : RenderScale(renderScale), Samples(samples), PearlEnabled(pearlEnabled), EffectsEnabled(effectsEnabled) {
        Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr, 0, D3D11_SDK_VERSION, &Device, nullptr, &Context), "Create radio fixture hardware device");
        LoadGeometry(geometryPath);
        Transforms = CreateBuffer(304);
        Lights = CreateBuffer(288);
        Base = CreateBuffer(16);
        Emission = CreateBuffer(16);
        Roughness = CreateBuffer(16);
        D3D11_SAMPLER_DESC sampler {};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        Check(Device->CreateSamplerState(&sampler, &Sampler), "Create radio fixture sampler");
        D3D11_DEPTH_STENCIL_DESC depth {};
        depth.DepthEnable = TRUE;
        depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        depth.DepthFunc = D3D11_COMPARISON_LESS;
        Check(Device->CreateDepthStencilState(&depth, &Depth), "Create radio fixture depth state");
        D3D11_RASTERIZER_DESC raster {};
        raster.FillMode = D3D11_FILL_SOLID;
        raster.CullMode = D3D11_CULL_NONE;
        raster.MultisampleEnable = TRUE;
        raster.DepthClipEnable = TRUE;
        Check(Device->CreateRasterizerState(&raster, &Rasterizer), "Create radio fixture rasterizer");
        const unsigned char white[] = { 255, 255, 255, 255 };
        D3D11_TEXTURE2D_DESC texture {};
        texture.Width = texture.Height = texture.MipLevels = texture.ArraySize = 1;
        texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texture.SampleDesc.Count = 1;
        texture.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initial { white, 4, 0 };
        Microsoft::WRL::ComPtr<ID3D11Texture2D> resource;
        Check(Device->CreateTexture2D(&texture, &initial, &resource), "Create radio fixture white texel");
        Check(Device->CreateShaderResourceView(resource.Get(), nullptr, &WhiteTexture), "Create radio fixture white view");
    }

    void CameraToonRadioFixture::Check(HRESULT result, const char* operation) {
        if (FAILED(result)) throw std::runtime_error(std::string(operation) + " HRESULT " + std::to_string(result));
    }

    void CameraToonRadioFixture::ReadExact(std::ifstream& input, void* destination, std::size_t bytes) {
        input.read(static_cast<char*>(destination), static_cast<std::streamsize>(bytes));
        if (!input) throw std::runtime_error("Radio geometry export is truncated or cannot be read.");
    }

    std::uint32_t CameraToonRadioFixture::ReadUnsigned(std::ifstream& input) {
        unsigned char bytes[4] {};
        ReadExact(input, bytes, sizeof(bytes));
        return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8)
            | (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
    }

    float CameraToonRadioFixture::ReadScalar(std::ifstream& input) {
        static_assert(sizeof(float) == sizeof(std::uint32_t));
        std::uint32_t bits = ReadUnsigned(input);
        float scalar;
        std::memcpy(&scalar, &bits, sizeof(scalar));
        if (!std::isfinite(scalar)) throw std::runtime_error("Radio geometry export contains a nonfinite scalar.");
        return scalar;
    }

    Microsoft::WRL::ComPtr<ID3D11Buffer> CameraToonRadioFixture::CreateBuffer(UINT bytes) {
        D3D11_BUFFER_DESC descriptor {};
        descriptor.ByteWidth = bytes;
        descriptor.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
        Check(Device->CreateBuffer(&descriptor, nullptr, &buffer), "Create radio fixture constant buffer");
        return buffer;
    }

    void CameraToonRadioFixture::LoadGeometry(const std::string& geometryPath) {
        std::ifstream input(geometryPath, std::ios::binary);
        if (!input) throw std::runtime_error("Cannot open radio geometry export: " + geometryPath);
        char signature[8] {};
        ReadExact(input, signature, sizeof(signature));
        bool hasPearlWeights=std::memcmp(signature,"GRADIO12",sizeof(signature))==0;
        bool hasMaterialWeights=hasPearlWeights || std::memcmp(signature,"GRADIO11",sizeof(signature))==0;
        if (!hasMaterialWeights && std::memcmp(signature, "GRADIO10", sizeof(signature)) != 0)
            throw std::runtime_error("Radio geometry export requires GRADIO10 or GRADIO11 signature.");
        if(EffectsEnabled && !hasMaterialWeights) throw std::runtime_error("Radio effects require explicit GRADIO11 material weights.");
        if(PearlEnabled && !hasPearlWeights) throw std::runtime_error("Pearl radio rendering requires explicit GRADIO12 surface eligibility.");
        std::uint32_t primitiveCount = ReadUnsigned(input);
        if (primitiveCount == 0 || primitiveCount > 256)
            throw std::runtime_error("Radio geometry export must contain between 1 and 256 primitives.");
        std::uint32_t totalVertices = 0;
        for (std::uint32_t primitive = 0; primitive < primitiveCount; ++primitive) {
            std::uint32_t vertexCount = ReadUnsigned(input);
            if (vertexCount == 0 || vertexCount % 3 != 0 || vertexCount > 1000000 - totalVertices)
                throw std::runtime_error("Radio geometry export requires triangle lists totaling at most one million vertices.");
            totalVertices += vertexCount;
            std::array<float, 4> color {};
            for (float& channel : color) channel = ReadScalar(input);
            std::array<float,3> weights {};
            if(hasMaterialWeights) for(int field=0;field<(hasPearlWeights ? 3 : 2);++field) {
                float& weight=weights[field]; weight=ReadScalar(input);
                if(weight<0 || weight>1) throw std::runtime_error("Radio material weights must lie between zero and one.");
            }
            std::vector<float> vertices(static_cast<std::size_t>(vertexCount) * 8);
            for (float& scalar : vertices) scalar = ReadScalar(input);
            D3D11_BUFFER_DESC descriptor {};
            descriptor.ByteWidth = vertexCount * 32;
            descriptor.Usage = D3D11_USAGE_IMMUTABLE;
            descriptor.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA initial { vertices.data(), 0, 0 };
            Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
            Check(Device->CreateBuffer(&descriptor, &initial, &buffer), "Create exported radio triangle buffer");
            Vertices.push_back(buffer);
            VertexCounts.push_back(vertexCount);
            Colors.push_back(color);
            MaterialWeights.push_back(weights);
        }
        if (input.peek() != std::char_traits<char>::eof()) throw std::runtime_error("Radio geometry export has trailing data.");
        if (input.bad()) throw std::runtime_error("Radio geometry export failed while checking its end.");
        std::cout << "Owned radio geometry: " << primitiveCount << " primitives, " << totalVertices << " expanded vertices.\n";
    }

    void CameraToonRadioFixture::PrepareMaterial(float offsetPixels) {
        using namespace DirectX;
        XMVECTOR rotation = XMQuaternionNormalize(XMVectorSet(0.04184884f, -0.12430773f, -0.00909404f, 0.9913191f));
        XMMATRIX world = XMMatrixScaling(2.8f, 2.8f, 2.8f) * XMMatrixRotationQuaternion(rotation)
            * XMMatrixTranslation(0.0f, 0.0f, -10.0f);
        XMMATRIX projection = XMMatrixPerspectiveFovRH(XMConvertToRadians(80.60606f),
            static_cast<float>(Width) / static_cast<float>(Height), 0.1f, 1000.0f);
        XMMATRIX screenOffset = XMMatrixTranslation(2.0f * offsetPixels / static_cast<float>(Width), 0.0f, 0.0f);
        XMMATRIX normal = XMMatrixTranspose(XMMatrixInverse(nullptr, world));
        std::array<float, 76> transforms {};
        XMFLOAT4X4 packed;
        XMStoreFloat4x4(&packed, XMMatrixTranspose(world));
        std::memcpy(transforms.data(), &packed, sizeof(packed));
        XMStoreFloat4x4(&packed, XMMatrixTranspose(world * projection * screenOffset));
        std::memcpy(transforms.data() + 16, &packed, sizeof(packed));
        XMStoreFloat4x4(&packed, XMMatrixTranspose(normal));
        std::memcpy(transforms.data() + 32, &packed, sizeof(packed));
        Context->UpdateSubresource(Transforms.Get(), 0, nullptr, transforms.data(), 0, 0);
        std::array<float, 72> lights {};
        lights[0]=0.55f; lights[1]=0.517f; lights[2]=0.484f; lights[4]=1;
        lights[8]=1.11656f; lights[9]=1.0719f; lights[10]=1.0161f;
        XMVECTOR lightRotation=XMQuaternionRotationRollPitchYaw(XMConvertToRadians(5.2454f),XMConvertToRadians(19.0909f),0);
        XMVECTOR direction=XMVector3Rotate(XMVectorSet(0,0,-1,0),lightRotation);
        XMFLOAT3 lightDirection; XMStoreFloat3(&lightDirection,direction);
        lights[12]=lightDirection.x; lights[13]=lightDirection.y; lights[14]=lightDirection.z;
        Context->UpdateSubresource(Lights.Get(), 0, nullptr, lights.data(), 0, 0);
        const float base[] = { 0, 0, 0, 1 };
        Context->UpdateSubresource(Base.Get(), 0, nullptr, base, 0, 0);
        ID3D11Buffer* transformsBuffer = Transforms.Get();
        Context->VSSetConstantBuffers(0, 1, &transformsBuffer);
        Context->PSSetConstantBuffers(0, 1, &transformsBuffer);
        ID3D11Buffer* lightBuffer = Lights.Get();
        Context->PSSetConstantBuffers(1, 1, &lightBuffer);
        ID3D11Buffer* baseBuffer = Base.Get();
        Context->PSSetConstantBuffers(3, 1, &baseBuffer);
        ID3D11Buffer* emissionBuffer = Emission.Get();
        Context->PSSetConstantBuffers(7, 1, &emissionBuffer);
        const float roughness[] {0.45f,0,0,0}; Context->UpdateSubresource(Roughness.Get(),0,nullptr,roughness,0,0);
        ID3D11Buffer* roughnessBuffer=Roughness.Get(); Context->PSSetConstantBuffers(4,1,&roughnessBuffer);
        ID3D11ShaderResourceView* white = WhiteTexture.Get();
        Context->PSSetShaderResources(0, 1, &white);
        Context->PSSetShaderResources(6,1,&white);
        ID3D11SamplerState* sampler = Sampler.Get();
        Context->PSSetSamplers(0, 1, &sampler);
        Context->PSSetSamplers(6,1,&sampler);
        Context->RSSetState(Rasterizer.Get());
        Context->OMSetDepthStencilState(Depth.Get(), 0);
        Context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFFu);
    }

    void CameraToonRadioFixture::Draw(Win32CameraToonPass& pass) {
        Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        for (std::size_t primitive = 0; primitive < Vertices.size(); ++primitive) {
            pass.BindGeometry(Context.Get(),EffectsEnabled ? MaterialWeights[primitive][0] : 0.0f,EffectsEnabled ? MaterialWeights[primitive][1] : 0.0f,PearlEnabled && MaterialWeights[primitive][2]>0.5f);
            const std::array<float, 4>& color = Colors[primitive];
            Context->UpdateSubresource(Base.Get(),0,nullptr,color.data(),0,0);
            const float emission[] = { color[0], color[1], color[2], MaterialWeights[primitive][1]>0 ? 0.8f : 0.0f };
            Context->UpdateSubresource(Emission.Get(), 0, nullptr, emission, 0, 0);
            ID3D11Buffer* buffer = Vertices[primitive].Get();
            UINT stride = 32, offset = 0;
            Context->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);
            Context->Draw(VertexCounts[primitive], 0);
        }
    }

    void CameraToonRadioFixture::CreateResolveResources(ID3D11Texture2D* source) {
        D3D11_TEXTURE2D_DESC descriptor {};
        source->GetDesc(&descriptor);
        descriptor.SampleDesc.Count = 1;
        descriptor.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (Samples == 1) Resolved = source;
        else Check(Device->CreateTexture2D(&descriptor, nullptr, &Resolved), "Create owned radio MSAA resolve");
        if (RenderScale == 1) {
            Output = Resolved;
            return;
        }
        Check(Device->CreateShaderResourceView(Resolved.Get(), nullptr, &ResolvedView), "Create owned radio resolve view");
        descriptor.Width = Width;
        descriptor.Height = Height;
        descriptor.BindFlags = D3D11_BIND_RENDER_TARGET;
        Check(Device->CreateTexture2D(&descriptor, nullptr, &Output), "Create diagnostic radio downsample output");
        Check(Device->CreateRenderTargetView(Output.Get(), nullptr, &OutputTarget), "Create diagnostic radio downsample target");
        const char* shader = R"(
Texture2D<float4> Source : register(t0);
float4 Vertex(uint id : SV_VertexID) : SV_POSITION {
    float2 positions[3] = { float2(-1,-1), float2(-1,3), float2(3,-1) };
    return float4(positions[id], 0, 1);
}
float4 Pixel(float4 position : SV_POSITION) : SV_TARGET {
    int2 origin = int2(position.xy) * 2;
    return (Source.Load(int3(origin,0)) + Source.Load(int3(origin+int2(1,0),0))
        + Source.Load(int3(origin+int2(0,1),0)) + Source.Load(int3(origin+int2(1,1),0))) * 0.25;
}
)";
        Microsoft::WRL::ComPtr<ID3DBlob> vertex, pixel, errors;
        HRESULT result = D3DCompile(shader, std::strlen(shader), "OwnedRadioDiagnosticDownsample", nullptr, nullptr,
            "Vertex", "vs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &vertex, &errors);
        if (FAILED(result) && errors) throw std::runtime_error(std::string("Radio downsample vertex shader: ")
            + std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()));
        Check(result, "Compile diagnostic radio downsample vertex shader");
        errors.Reset();
        result = D3DCompile(shader, std::strlen(shader), "OwnedRadioDiagnosticDownsample", nullptr, nullptr,
            "Pixel", "ps_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &pixel, &errors);
        if (FAILED(result) && errors) throw std::runtime_error(std::string("Radio downsample pixel shader: ")
            + std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()));
        Check(result, "Compile diagnostic radio downsample pixel shader");
        Check(Device->CreateVertexShader(vertex->GetBufferPointer(), vertex->GetBufferSize(), nullptr, &DownsampleVertex),
            "Create diagnostic radio downsample vertex shader");
        Check(Device->CreatePixelShader(pixel->GetBufferPointer(), pixel->GetBufferSize(), nullptr, &DownsamplePixel),
            "Create diagnostic radio downsample pixel shader");
        D3D11_DEPTH_STENCIL_DESC depth {};
        depth.DepthEnable = FALSE;
        depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
        Check(Device->CreateDepthStencilState(&depth, &DownsampleDepth), "Create diagnostic radio downsample depth state");
    }

    void CameraToonRadioFixture::ResolveFrame(ID3D11RenderTargetView* destination) {
        Context->OMSetRenderTargets(0, nullptr, nullptr);
        if (Samples > 1) {
            Microsoft::WRL::ComPtr<ID3D11Resource> source;
            destination->GetResource(&source);
            Context->ResolveSubresource(Resolved.Get(), 0, source.Get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM);
        }
        if (RenderScale == 1) return;
        ID3D11RenderTargetView* output = OutputTarget.Get();
        Context->OMSetRenderTargets(1, &output, nullptr);
        Context->OMSetDepthStencilState(DownsampleDepth.Get(), 0);
        Context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFFu);
        Context->RSSetState(Rasterizer.Get());
        D3D11_VIEWPORT viewport { 0, 0, static_cast<float>(Width), static_cast<float>(Height), 0, 1 };
        Context->RSSetViewports(1, &viewport);
        Context->IASetInputLayout(nullptr);
        Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        Context->VSSetShader(DownsampleVertex.Get(), nullptr, 0);
        Context->PSSetShader(DownsamplePixel.Get(), nullptr, 0);
        ID3D11ShaderResourceView* sourceView = ResolvedView.Get();
        Context->PSSetShaderResources(0, 1, &sourceView);
        Context->Draw(3, 0);
        ID3D11ShaderResourceView* empty = nullptr;
        Context->PSSetShaderResources(0, 1, &empty);
    }

    void CameraToonRadioFixture::DrawFrame(Win32CameraToonPass& pass, ID3D11RenderTargetView* destination, float offsetPixels) {
        const float transparent[] = { 0, 0, 0, 0 };
        Context->ClearRenderTargetView(destination, transparent);
        D3D11_VIEWPORT viewport { 0, 0, static_cast<float>(Width * RenderScale), static_cast<float>(Height * RenderScale), 0, 1 };
        PrepareMaterial(offsetPixels);
        pass.Begin(Context.Get(), viewport, Samples);
        Draw(pass);
        pass.Composite(Context.Get(), destination, nullptr, viewport);
        ResolveFrame(destination);
    }

    void CameraToonRadioFixture::Measure(Win32CameraToonPass& pass, ID3D11RenderTargetView* destination, float offsetPixels) {
        Microsoft::WRL::ComPtr<ID3D11Query> disjoint, start, end;
        D3D11_QUERY_DESC query {};
        query.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
        Check(Device->CreateQuery(&query, &disjoint), "Create radio GPU timing interval");
        query.Query = D3D11_QUERY_TIMESTAMP;
        Check(Device->CreateQuery(&query, &start), "Create radio GPU start timestamp");
        Check(Device->CreateQuery(&query, &end), "Create radio GPU end timestamp");
        Context->Begin(disjoint.Get());
        Context->End(start.Get());
        DrawFrame(pass, destination, offsetPixels);
        Context->End(end.Get());
        Context->End(disjoint.Get());
        Context->Flush();
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT interval {};
        HRESULT result;
        ULONGLONG deadline = GetTickCount64() + 10000;
        while ((result = Context->GetData(disjoint.Get(), &interval, sizeof(interval), 0)) == S_FALSE) {
            if (GetTickCount64() >= deadline) throw std::runtime_error("Radio GPU timestamp did not complete within ten seconds.");
            Sleep(0);
        }
        Check(result, "Read radio GPU timing interval");
        UINT64 first = 0, last = 0;
        HRESULT firstResult = Context->GetData(start.Get(), &first, sizeof(first), 0);
        HRESULT lastResult = Context->GetData(end.Get(), &last, sizeof(last), 0);
        Check(firstResult, "Read radio GPU start timestamp");
        Check(lastResult, "Read radio GPU end timestamp");
        if (firstResult != S_OK || lastResult != S_OK || interval.Disjoint || interval.Frequency == 0 || last < first)
            throw std::runtime_error("Radio GPU timestamp interval is incomplete or invalid.");
        double milliseconds = 1000.0 * static_cast<double>(last - first) / static_cast<double>(interval.Frequency);
        std::cout << "Owned " << Width * RenderScale << 'x' << Height * RenderScale
            << " MSAA" << Samples << " radio toon GPU time (ms): " << milliseconds
            << " (geometry + toon composite" << (Samples > 1 ? " + MSAA resolve" : "")
            << (RenderScale == 2 ? " + 2x2 GPU downsample" : "") << "; not whole-app FPS).\n";
    }

    void CameraToonRadioFixture::Save(ID3D11Texture2D* destination, const std::string& outputPath) {
        Context->OMSetRenderTargets(0, nullptr, nullptr);
        D3D11_TEXTURE2D_DESC descriptor {};
        destination->GetDesc(&descriptor);
        descriptor.BindFlags = 0;
        descriptor.Usage = D3D11_USAGE_STAGING;
        descriptor.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
        Check(Device->CreateTexture2D(&descriptor, nullptr, &staging), "Create owned radio readback");
        Context->CopyResource(staging.Get(), destination);
        D3D11_MAPPED_SUBRESOURCE mapped {};
        Check(Context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map owned radio pixels");
        std::vector<unsigned char> visible(static_cast<std::size_t>(Width) * Height * 4);
        for (UINT row = 0; row < Height; ++row) {
            const unsigned char* source = static_cast<const unsigned char*>(mapped.pData) + row * mapped.RowPitch;
            unsigned char* target = visible.data() + row * Width * 4;
            for (UINT column = 0; column < Width; ++column) {
                UINT pixel = column * 4;
                int background = (230 * (255 - source[pixel + 3]) + 127) / 255;
                target[pixel] = static_cast<unsigned char>(std::min(255, source[pixel + 2] + background));
                target[pixel + 1] = static_cast<unsigned char>(std::min(255, source[pixel + 1] + background));
                target[pixel + 2] = static_cast<unsigned char>(std::min(255, source[pixel] + background));
                target[pixel + 3] = 255;
            }
        }
        Context->Unmap(staging.Get(), 0);
        BITMAPFILEHEADER header {};
        header.bfType = 0x4d42;
        header.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
        header.bfSize = header.bfOffBits + static_cast<DWORD>(visible.size());
        BITMAPINFOHEADER info {};
        info.biSize = sizeof(info);
        info.biWidth = Width;
        info.biHeight = -static_cast<LONG>(Height);
        info.biPlanes = 1;
        info.biBitCount = 32;
        info.biCompression = BI_RGB;
        std::ofstream output(outputPath, std::ios::binary);
        if (!output) throw std::runtime_error("Cannot create radio fixture bitmap: " + outputPath);
        output.write(reinterpret_cast<const char*>(&header), sizeof(header));
        output.write(reinterpret_cast<const char*>(&info), sizeof(info));
        output.write(reinterpret_cast<const char*>(visible.data()), static_cast<std::streamsize>(visible.size()));
        output.close();
        if (!output) throw std::runtime_error("Cannot write radio fixture bitmap: " + outputPath);
    }

    void CameraToonRadioFixture::Run(const std::string& outputPath, float outsidePixels, float insidePixels, float offsetPixels) {
        Win32CameraToonPass pass(Device.Get(), outsidePixels * RenderScale, insidePixels * RenderScale, 3);
        auto readyDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
        while(!pass.TryPrepare()) {
            if(std::chrono::steady_clock::now()>=readyDeadline) throw std::runtime_error("Offscreen radio shaders did not finish loading.");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if(EffectsEnabled) {
            CameraToonEffectsConfiguration effects;
            effects.PearlEnabled=PearlEnabled; effects.PearlStrength=0.65f; effects.PearlHighlight=0.55f; effects.PearlWidth=0.45f;
            effects.ReflectionsEnabled=true; effects.ReflectionStrength=0.12f;
            effects.BloomEnabled=true; effects.BloomStrength=0.1f; effects.BloomThreshold=0.6f; effects.BloomRadius=6.0f*RenderScale;
            pass.ConfigureEffects(effects);
            std::vector<uint8_t> environment(128*64*4,255);
            for(UINT y=0;y<64;++y) for(UINT x=0;x<128;++x) {
                UINT offset=(y*128+x)*4;
                environment[offset]=static_cast<uint8_t>(32+x);
                environment[offset+1]=static_cast<uint8_t>(48+y*2);
                environment[offset+2]=static_cast<uint8_t>(96+x/2);
            }
            pass.SetEnvironment(Context.Get(),128,64,environment.data(),environment.size());
        }
        D3D11_TEXTURE2D_DESC descriptor {};
        descriptor.Width = Width * RenderScale;
        descriptor.Height = Height * RenderScale;
        descriptor.MipLevels = descriptor.ArraySize = 1;
        descriptor.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        descriptor.SampleDesc.Count = Samples;
        descriptor.BindFlags = D3D11_BIND_RENDER_TARGET | (Samples == 1 ? D3D11_BIND_SHADER_RESOURCE : 0);
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> destination;
        Check(Device->CreateTexture2D(&descriptor, nullptr, &texture), "Create owned radio destination");
        Check(Device->CreateRenderTargetView(texture.Get(), nullptr, &destination), "Create owned radio destination view");
        CreateResolveResources(texture.Get());
        DrawFrame(pass, destination.Get(), offsetPixels);
        Measure(pass, destination.Get(), offsetPixels);
        Save(Output.Get(), outputPath);
        std::cout << "Owned radio fixture saved: " << outputPath << " outside=" << outsidePixels
            << " inside=" << insidePixels << " offset=" << offsetPixels << " output pixels; diagnostic render scale=" << RenderScale
            << ", local samples=" << Samples << ".\n";
    }

    void CameraToonRadioFixture::Render(const std::string& geometryPath, const std::string& outputPath,
        float outsidePixels, float insidePixels, float offsetPixels, int renderScale, UINT samples, bool effectsEnabled, bool pearlEnabled) {
        if (geometryPath.empty() || outputPath.empty()) throw std::invalid_argument("Radio fixture requires explicit geometry and output paths.");
        if (!std::isfinite(offsetPixels)) throw std::invalid_argument("Radio fixture screen offset must be finite.");
        if (renderScale != 1 && renderScale != 2) throw std::invalid_argument("Radio fixture diagnostic render scale must be one or two.");
        if (samples != 1 && samples != 4) throw std::invalid_argument("Radio fixture local sample count must be one or four.");
        CameraToonRadioFixture fixture(geometryPath, renderScale, samples,effectsEnabled,pearlEnabled);
        fixture.Run(outputPath, outsidePixels, insidePixels, offsetPixels);
    }
}
