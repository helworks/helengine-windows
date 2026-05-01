#include "platform/windows/win32/win32_render_bridge.hpp"

#include <d3dcompiler.h>

#include <DirectXMath.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <numbers>
#include <string>
#include <stdexcept>
#include <vector>

#include "MaterialLayoutBuilder.hpp"
#include "platform/windows/directx11/directx11_bootstrap.hpp"

namespace helengine::windows {
#if __has_include("RenderManager2D.hpp")
    namespace {
        /// Tracks whether one diagnostic render snapshot has already been written.
        bool HasWrittenRenderSnapshot = false;

        /// Caches uploaded textures so both the texture loader and material binder resolve the same GPU resources.
        std::unordered_map<std::string, std::unique_ptr<Win32TextureResource>> TextureResources;

        /// Packs one mesh vertex into the fixed Windows DirectX11 bridge layout.
        struct Win32VertexPositionNormalUV {
            DirectX::XMFLOAT3 Position;
            DirectX::XMFLOAT3 Normal;
            DirectX::XMFLOAT2 UV;
        };

        /// Stores the transform constants consumed by the fixed Windows DirectX11 bridge shader.
        struct Win32TransformConstants {
            DirectX::XMFLOAT4X4 World;
            DirectX::XMFLOAT4X4 WorldViewProjection;
            DirectX::XMFLOAT4X4 WorldNormal;
            DirectX::XMFLOAT4 CameraPosition;
            DirectX::XMFLOAT4 LightDirection;
            DirectX::XMFLOAT4 LightColor;
            DirectX::XMFLOAT4 AmbientColor;
            DirectX::XMFLOAT4 SpecularColor;
            DirectX::XMFLOAT4 MaterialParameters;
        };

        /// Vertex shader used by the first native 3D Windows pass.
        constexpr const char* VertexShaderSource = R"(
cbuffer TransformBuffer : register(b0) {
    float4x4 World;
    float4x4 WorldViewProjection;
    float4x4 WorldNormal;
    float4 CameraPosition;
    float4 LightDirection;
    float4 LightColor;
    float4 AmbientColor;
    float4 SpecularColor;
    float4 MaterialParameters;
};

struct VSInput {
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD0;
};

struct PSInput {
    float4 Position : SV_POSITION;
    float3 WorldPosition : TEXCOORD0;
    float3 WorldNormal : TEXCOORD1;
};

PSInput VSMain(VSInput input) {
    PSInput output;
    float4 worldPosition = mul(float4(input.Position, 1.0f), World);
    float4 worldNormal = mul(float4(input.Normal, 0.0f), WorldNormal);
    output.Position = mul(worldPosition, WorldViewProjection);
    output.WorldPosition = worldPosition.xyz;
    output.WorldNormal = normalize(worldNormal.xyz);
    return output;
}
)";

        /// Pixel shader used by the first native 3D Windows pass.
        constexpr const char* PixelShaderSource = R"(
cbuffer TransformBuffer : register(b0) {
    float4x4 World;
    float4x4 WorldViewProjection;
    float4x4 WorldNormal;
    float4 CameraPosition;
    float4 LightDirection;
    float4 LightColor;
    float4 AmbientColor;
    float4 SpecularColor;
    float4 MaterialParameters;
};

struct PSInput {
    float4 Position : SV_POSITION;
    float3 WorldPosition : TEXCOORD0;
    float3 WorldNormal : TEXCOORD1;
};

float4 PSMain(PSInput input) : SV_TARGET {
    float3 normal = normalize(input.WorldNormal);
    return float4(normal * 0.5f + 0.5f, 1.0f);
}
)";

        /// Vertex shader used by the fullscreen diagnostic pass.
        constexpr const char* DiagnosticVertexShaderSource = R"(
struct VSInput {
    float3 Position : POSITION;
};

struct VSOutput {
    float4 Position : SV_POSITION;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    output.Position = float4(input.Position, 1.0f);
    return output;
}
)";

        /// Pixel shader used by the fullscreen diagnostic pass.
        constexpr const char* DiagnosticPixelShaderSource = R"(
float4 PSMain() : SV_TARGET {
    return float4(0.05f, 0.7f, 0.95f, 1.0f);
}
)";

        /// Throws when one native DirectX operation fails.
        void ThrowIfFailed(HRESULT result, const char* message) {
            if (FAILED(result)) {
                throw std::runtime_error(message);
            }
        }

        /// Copies one HelEngine matrix into the native shader constant-buffer layout.
        DirectX::XMFLOAT4X4 StoreMatrix(const ::float4x4& value) {
            return DirectX::XMFLOAT4X4(
                value.M11, value.M12, value.M13, value.M14,
                value.M21, value.M22, value.M23, value.M24,
                value.M31, value.M32, value.M33, value.M34,
                value.M41, value.M42, value.M43, value.M44);
        }

        /// Appends one one-line message to the temporary Windows render snapshot log.
        void AppendRenderSnapshotLine(const std::string& line) {
            std::ofstream stream("C:\\dev\\helengine\\tmp\\win32-render-snapshot.log", std::ios::app);
            stream << line << '\n';
        }

        /// Creates a descriptor for one default mesh vertex input layout.
        std::vector<D3D11_INPUT_ELEMENT_DESC> BuildDefaultInputElements() {
            std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
            elements.push_back({ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 });
            elements.push_back({ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 });
            elements.push_back({ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 });
            return elements;
        }
    }

    /// Creates the native renderer bridge for one DirectX11 bootstrap.
    Win32RenderManager3D::Win32RenderManager3D(DirectX11Bootstrap& bootstrap)
        : Bootstrap(bootstrap)
        , CurrentViewProjection(::float4x4::get_Identity()) {
    }

    /// Builds a GPU-ready runtime model from raw mesh asset metadata.
    RuntimeModel* Win32RenderManager3D::BuildModelFromRaw(ModelAsset* data) {
        Win32RuntimeModel* runtimeModel = new Win32RuntimeModel();
        if (data != nullptr) {
            runtimeModel->set_Id(data->get_Id());

            const int32_t positionCount = data->Positions != nullptr ? data->Positions->Length : 0;
            const int32_t normalCount = data->Normals != nullptr ? data->Normals->Length : 0;
            const int32_t texCoordCount = data->TexCoords != nullptr ? data->TexCoords->Length : 0;
            const int32_t vertexCount = std::min(positionCount, std::min(normalCount, texCoordCount));
            if (vertexCount > 0) {
                std::vector<Win32VertexPositionNormalUV> vertices(static_cast<std::size_t>(vertexCount));
                for (int32_t index = 0; index < vertexCount; index++) {
                    const float3& position = (*data->Positions)[index];
                    const float3& normal = (*data->Normals)[index];
                    const float2& texCoord = (*data->TexCoords)[index];

                    vertices[static_cast<std::size_t>(index)] = Win32VertexPositionNormalUV {
                        DirectX::XMFLOAT3(position.X, position.Y, position.Z),
                        DirectX::XMFLOAT3(normal.X, normal.Y, normal.Z),
                        DirectX::XMFLOAT2(texCoord.X, texCoord.Y)
                    };
                }

                D3D11_BUFFER_DESC vertexBufferDescription {};
                vertexBufferDescription.ByteWidth = static_cast<UINT>(sizeof(Win32VertexPositionNormalUV) * vertices.size());
                vertexBufferDescription.Usage = D3D11_USAGE_DEFAULT;
                vertexBufferDescription.BindFlags = D3D11_BIND_VERTEX_BUFFER;

                D3D11_SUBRESOURCE_DATA vertexData {};
                vertexData.pSysMem = vertices.data();

                ThrowIfFailed(
                    Bootstrap.GetDevice()->CreateBuffer(&vertexBufferDescription, &vertexData, runtimeModel->VertexBuffer.GetAddressOf()),
                    "ID3D11Device::CreateBuffer failed for the Windows mesh vertex buffer.");
                runtimeModel->VertexCount = static_cast<UINT>(vertices.size());
            }

            if (data->Indices32 != nullptr && data->Indices32->Length > 0) {
                D3D11_BUFFER_DESC indexBufferDescription {};
                indexBufferDescription.ByteWidth = static_cast<UINT>(sizeof(uint32_t) * data->Indices32->Length);
                indexBufferDescription.Usage = D3D11_USAGE_DEFAULT;
                indexBufferDescription.BindFlags = D3D11_BIND_INDEX_BUFFER;

                D3D11_SUBRESOURCE_DATA indexData {};
                indexData.pSysMem = data->Indices32->Data;

                ThrowIfFailed(
                    Bootstrap.GetDevice()->CreateBuffer(&indexBufferDescription, &indexData, runtimeModel->IndexBuffer.GetAddressOf()),
                    "ID3D11Device::CreateBuffer failed for the Windows mesh 32-bit index buffer.");
                runtimeModel->IndexCount = static_cast<UINT>(data->Indices32->Length);
                runtimeModel->IndexFormat = DXGI_FORMAT_R32_UINT;
            } else if (data->Indices16 != nullptr && data->Indices16->Length > 0) {
                D3D11_BUFFER_DESC indexBufferDescription {};
                indexBufferDescription.ByteWidth = static_cast<UINT>(sizeof(uint16_t) * data->Indices16->Length);
                indexBufferDescription.Usage = D3D11_USAGE_DEFAULT;
                indexBufferDescription.BindFlags = D3D11_BIND_INDEX_BUFFER;

                D3D11_SUBRESOURCE_DATA indexData {};
                indexData.pSysMem = data->Indices16->Data;

                ThrowIfFailed(
                    Bootstrap.GetDevice()->CreateBuffer(&indexBufferDescription, &indexData, runtimeModel->IndexBuffer.GetAddressOf()),
                    "ID3D11Device::CreateBuffer failed for the Windows mesh 16-bit index buffer.");
                runtimeModel->IndexCount = static_cast<UINT>(data->Indices16->Length);
                runtimeModel->IndexFormat = DXGI_FORMAT_R16_UINT;
            }
        }

        return runtimeModel;
    }

    /// Builds a runtime material placeholder that keeps the packaged material identity.
    RuntimeMaterial* Win32RenderManager3D::BuildMaterialFromRaw(MaterialAsset* materialAsset, ShaderAsset* shaderAsset) {
        RuntimeMaterial* runtimeMaterial = new RuntimeMaterial();
        if (materialAsset == nullptr) {
            if (shaderAsset != nullptr) {
                runtimeMaterial->set_Id(shaderAsset->get_Id());
            }
            return runtimeMaterial;
        }

        if (shaderAsset == nullptr) {
            runtimeMaterial->set_Id(materialAsset->get_Id());
            return runtimeMaterial;
        }

        std::string materialId = materialAsset->get_Id();
        if (materialId.empty()) {
            materialId = shaderAsset->get_Id();
        }

        runtimeMaterial->set_Id(materialId);
        MaterialLayout* layout = MaterialLayoutBuilder::Build(materialAsset, shaderAsset);
        runtimeMaterial->SetLayout(layout);
        runtimeMaterial->SetRenderState(materialAsset->RenderState);
        runtimeMaterial->ApplyConstantBufferDefaults(materialAsset->ConstantBuffers);

        MaterialShaderResources[materialId] = std::make_unique<Win32ShaderResource>(BuildShaderResource(materialAsset, shaderAsset));
        return runtimeMaterial;
    }

    /// Draws every registered camera to the Windows back buffer in camera order.
    void Win32RenderManager3D::Draw() {
        RenderManager3D::Draw();
        EnsurePipelineState();
        if (!HasWrittenRenderSnapshot) {
            AppendRenderSnapshotLine("draw begin");
        }

        if (Core::get_Instance() == nullptr || Core::get_Instance()->get_ObjectManager() == nullptr) {
            ClearBackBuffer(0.0f, 0.0f, 0.0f, 1.0f);
            return;
        }

        List<ICamera*>* cameras = Core::get_Instance()->get_ObjectManager()->get_Cameras();
        if (cameras == nullptr || cameras->Count() == 0) {
            ClearBackBuffer(0.0f, 0.0f, 0.0f, 1.0f);
            return;
        }

        if (!HasWrittenRenderSnapshot) {
            AppendRenderSnapshotLine("camera count=" + std::to_string(cameras->Count()));
        }

        ID3D11DeviceContext* context = Bootstrap.GetDeviceContext();
        context->IASetInputLayout(InputLayout.Get());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11Buffer* transformBuffer = TransformBuffer.Get();
        context->VSSetConstantBuffers(0, 1, &transformBuffer);
        context->PSSetConstantBuffers(0, 1, &transformBuffer);
        context->RSSetState(RasterizerState.Get());
        context->OMSetDepthStencilState(DepthStencilState.Get(), 0);

        bool renderedAnyCamera = false;
        bool clearedColorBuffer = false;
        for (int32_t index = 0; index < cameras->Count(); index++) {
            ICamera* camera = (*cameras)[index];
            if (camera == nullptr || camera->get_Parent() == nullptr || !camera->get_Parent()->get_IsHierarchyEnabled()) {
                continue;
            }

            if (camera->get_RenderTarget() != nullptr) {
                continue;
            }

            RenderCamera(camera, !clearedColorBuffer);
            clearedColorBuffer = true;
            renderedAnyCamera = true;
        }

        if (!renderedAnyCamera) {
            ClearBackBuffer(0.0f, 0.0f, 0.0f, 1.0f);
        }
    }

    /// Draws one queued mesh for the currently active camera.
    void Win32RenderManager3D::Visit(IDrawable3D* drawable) {
        if (drawable == nullptr || drawable->get_Parent() == nullptr || !drawable->get_Parent()->get_IsHierarchyEnabled()) {
            return;
        }

        RuntimeModel* modelBase = drawable->get_Model();
        if (modelBase == nullptr) {
            return;
        }

        auto* model = static_cast<Win32RuntimeModel*>(modelBase);
        if (!model->VertexBuffer) {
            return;
        }

        RuntimeMaterial* runtimeMaterial = drawable->get_Material();
        RuntimeMaterial* rootMaterial = runtimeMaterial != nullptr ? runtimeMaterial->ResolveRootMaterial() : nullptr;
        Win32ShaderResource* shaderResource = nullptr;
        if (rootMaterial != nullptr) {
            std::string materialId = rootMaterial->get_Id();
            if (!materialId.empty()) {
                auto materialResource = MaterialShaderResources.find(materialId);
                if (materialResource != MaterialShaderResources.end()) {
                    shaderResource = materialResource->second.get();
                }
            }
        }

        ID3D11DeviceContext* context = Bootstrap.GetDeviceContext();
        const UINT stride = sizeof(Win32VertexPositionNormalUV);
        const UINT offset = 0;
        ID3D11Buffer* vertexBuffer = model->VertexBuffer.Get();
        context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);

        if (model->IndexBuffer && model->IndexCount > 0) {
            context->IASetIndexBuffer(model->IndexBuffer.Get(), model->IndexFormat, 0);
        }

        Entity* parent = drawable->get_Parent();
        float4 orientation = parent->get_Orientation();
        float3 scale = parent->get_Scale();
        float3 position = parent->get_Position();

        ::float4x4 rotation;
        float4x4::CreateFromQuaternion(orientation, rotation);

        ::float4x4 size;
        float4x4::CreateScale(scale.X, scale.Y, scale.Z, size);

        ::float4x4 rotationScale;
        float4x4::Multiply(rotation, size, rotationScale);

        ::float4x4 translation;
        float4x4::CreateTranslation(position, translation);

        ::float4x4 world;
        float4x4::Multiply(rotationScale, translation, world);

        ::float4x4 inverseScale;
        float4x4::CreateScale(
            scale.X != 0.0f ? 1.0f / scale.X : 0.0f,
            scale.Y != 0.0f ? 1.0f / scale.Y : 0.0f,
            scale.Z != 0.0f ? 1.0f / scale.Z : 0.0f,
            inverseScale);

        ::float4x4 normalMatrix;
        float4x4::Multiply(rotation, inverseScale, normalMatrix);

        ::float4x4 worldViewProjection;
        float4x4::Multiply(world, CurrentViewProjection, worldViewProjection);

        ::float4x4 transposedWorldViewProjection;
        float4x4::Transpose(worldViewProjection, transposedWorldViewProjection);

        ::float4x4 transposedWorld;
        float4x4::Transpose(world, transposedWorld);

        ::float4x4 transposedWorldNormal;
        float4x4::Transpose(normalMatrix, transposedWorldNormal);

        if (!HasWrittenRenderSnapshot) {
            AppendRenderSnapshotLine(
                "drawable position=" + std::to_string(position.X) + "," + std::to_string(position.Y) + "," + std::to_string(position.Z) +
                " scale=" + std::to_string(scale.X) + "," + std::to_string(scale.Y) + "," + std::to_string(scale.Z) +
                " orientation=" + std::to_string(orientation.X) + "," + std::to_string(orientation.Y) + "," + std::to_string(orientation.Z) + "," + std::to_string(orientation.W));
            AppendRenderSnapshotLine(
                "wvp rows="
                "[" + std::to_string(worldViewProjection.M11) + "," + std::to_string(worldViewProjection.M12) + "," + std::to_string(worldViewProjection.M13) + "," + std::to_string(worldViewProjection.M14) + "]"
                "[" + std::to_string(worldViewProjection.M21) + "," + std::to_string(worldViewProjection.M22) + "," + std::to_string(worldViewProjection.M23) + "," + std::to_string(worldViewProjection.M24) + "]"
                "[" + std::to_string(worldViewProjection.M31) + "," + std::to_string(worldViewProjection.M32) + "," + std::to_string(worldViewProjection.M33) + "," + std::to_string(worldViewProjection.M34) + "]"
                "[" + std::to_string(worldViewProjection.M41) + "," + std::to_string(worldViewProjection.M42) + "," + std::to_string(worldViewProjection.M43) + "," + std::to_string(worldViewProjection.M44) + "]");
            HasWrittenRenderSnapshot = true;
        }

        if (shaderResource != nullptr && rootMaterial != nullptr) {
            ApplyMaterial(rootMaterial);
        } else {
            context->IASetInputLayout(InputLayout.Get());
            context->VSSetShader(VertexShader.Get(), nullptr, 0);
            context->PSSetShader(PixelShader.Get(), nullptr, 0);
            ID3D11Buffer* transformBuffer = TransformBuffer.Get();
            context->VSSetConstantBuffers(0, 1, &transformBuffer);
            context->PSSetConstantBuffers(0, 1, &transformBuffer);
        }

        Win32TransformConstants constants {};
        constants.World = StoreMatrix(transposedWorld);
        constants.WorldViewProjection = StoreMatrix(transposedWorldViewProjection);
        constants.WorldNormal = StoreMatrix(transposedWorldNormal);
        constants.CameraPosition = DirectX::XMFLOAT4(0.0f, 1.818842f, 6.267936f, 0.0f);
        constants.LightDirection = DirectX::XMFLOAT4(-0.35f, -0.65f, -0.55f, 0.0f);
        constants.LightColor = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
        constants.AmbientColor = DirectX::XMFLOAT4(0.16f, 0.18f, 0.22f, 1.0f);
        constants.SpecularColor = DirectX::XMFLOAT4(0.9f, 0.9f, 0.95f, 1.0f);
        constants.MaterialParameters = DirectX::XMFLOAT4(24.0f, 0.55f, 0.0f, 0.0f);
        context->UpdateSubresource(TransformBuffer.Get(), 0, nullptr, &constants, 0, 0);

        if (model->IndexBuffer && model->IndexCount > 0) {
            context->DrawIndexed(model->IndexCount, 0, 0);
        } else {
            context->Draw(model->VertexCount, 0);
        }
    }

    /// Creates the shaders, input layout, and fixed pipeline state on first use.
    void Win32RenderManager3D::EnsurePipelineState() {
        if (VertexShader && PixelShader && InputLayout && TransformBuffer && RasterizerState && DepthStencilState) {
            EnsureTextureSamplerState();
            return;
        }

        if (!HasWrittenRenderSnapshot) {
            AppendRenderSnapshotLine("ensure pipeline state begin");
        }

        Microsoft::WRL::ComPtr<ID3DBlob> vertexShaderBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> pixelShaderBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;

        HRESULT vertexShaderResult = D3DCompile(VertexShaderSource, strlen(VertexShaderSource), nullptr, nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, vertexShaderBlob.GetAddressOf(), errorBlob.GetAddressOf());
        if (FAILED(vertexShaderResult) && errorBlob) {
            AppendRenderSnapshotLine(std::string("vertex shader compile failed: ") + static_cast<const char*>(errorBlob->GetBufferPointer()));
        }
        ThrowIfFailed(
            vertexShaderResult,
            errorBlob ? static_cast<const char*>(errorBlob->GetBufferPointer()) : "D3DCompile failed for the Windows bridge vertex shader.");
        if (!HasWrittenRenderSnapshot) {
            AppendRenderSnapshotLine("vertex shader compiled");
        }
        errorBlob.Reset();
        HRESULT pixelShaderResult = D3DCompile(PixelShaderSource, strlen(PixelShaderSource), nullptr, nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, pixelShaderBlob.GetAddressOf(), errorBlob.GetAddressOf());
        if (FAILED(pixelShaderResult) && errorBlob) {
            AppendRenderSnapshotLine(std::string("pixel shader compile failed: ") + static_cast<const char*>(errorBlob->GetBufferPointer()));
        }
        ThrowIfFailed(
            pixelShaderResult,
            errorBlob ? static_cast<const char*>(errorBlob->GetBufferPointer()) : "D3DCompile failed for the Windows bridge pixel shader.");
        if (!HasWrittenRenderSnapshot) {
            AppendRenderSnapshotLine("pixel shader compiled");
        }

        ThrowIfFailed(
            Bootstrap.GetDevice()->CreateVertexShader(vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize(), nullptr, VertexShader.GetAddressOf()),
            "ID3D11Device::CreateVertexShader failed for the Windows bridge.");
        ThrowIfFailed(
            Bootstrap.GetDevice()->CreatePixelShader(pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize(), nullptr, PixelShader.GetAddressOf()),
            "ID3D11Device::CreatePixelShader failed for the Windows bridge.");
        if (!HasWrittenRenderSnapshot) {
            AppendRenderSnapshotLine("pipeline shaders created");
        }

        static const D3D11_INPUT_ELEMENT_DESC InputElements[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };

        ThrowIfFailed(
            Bootstrap.GetDevice()->CreateInputLayout(
                InputElements,
                static_cast<UINT>(std::size(InputElements)),
                vertexShaderBlob->GetBufferPointer(),
                vertexShaderBlob->GetBufferSize(),
                InputLayout.GetAddressOf()),
            "ID3D11Device::CreateInputLayout failed for the Windows bridge.");
        if (!HasWrittenRenderSnapshot) {
            AppendRenderSnapshotLine("input layout created");
        }

        D3D11_BUFFER_DESC constantBufferDescription {};
        constantBufferDescription.ByteWidth = sizeof(Win32TransformConstants);
        constantBufferDescription.Usage = D3D11_USAGE_DEFAULT;
        constantBufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

        ThrowIfFailed(
            Bootstrap.GetDevice()->CreateBuffer(&constantBufferDescription, nullptr, TransformBuffer.GetAddressOf()),
            "ID3D11Device::CreateBuffer failed for the Windows bridge transform buffer.");
        if (!HasWrittenRenderSnapshot) {
            AppendRenderSnapshotLine("transform buffer created");
        }

        EnsureTextureSamplerState();

        D3D11_RASTERIZER_DESC rasterizerDescription {};
        rasterizerDescription.FillMode = D3D11_FILL_SOLID;
        rasterizerDescription.CullMode = D3D11_CULL_NONE;
        rasterizerDescription.DepthClipEnable = TRUE;

        ThrowIfFailed(
            Bootstrap.GetDevice()->CreateRasterizerState(&rasterizerDescription, RasterizerState.GetAddressOf()),
            "ID3D11Device::CreateRasterizerState failed for the Windows bridge.");

        D3D11_DEPTH_STENCIL_DESC depthStencilDescription {};
        depthStencilDescription.DepthEnable = TRUE;
        depthStencilDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        depthStencilDescription.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        depthStencilDescription.StencilEnable = TRUE;
        depthStencilDescription.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
        depthStencilDescription.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
        depthStencilDescription.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
        depthStencilDescription.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
        depthStencilDescription.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
        depthStencilDescription.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
        depthStencilDescription.BackFace = depthStencilDescription.FrontFace;

        ThrowIfFailed(
            Bootstrap.GetDevice()->CreateDepthStencilState(&depthStencilDescription, DepthStencilState.GetAddressOf()),
            "ID3D11Device::CreateDepthStencilState failed for the Windows bridge.");
        if (!HasWrittenRenderSnapshot) {
            AppendRenderSnapshotLine("depth stencil state created");
        }

        Microsoft::WRL::ComPtr<ID3DBlob> diagnosticVertexShaderBlob;
        Microsoft::WRL::ComPtr<ID3DBlob> diagnosticPixelShaderBlob;
        errorBlob.Reset();
        ThrowIfFailed(
            D3DCompile(DiagnosticVertexShaderSource, strlen(DiagnosticVertexShaderSource), nullptr, nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, diagnosticVertexShaderBlob.GetAddressOf(), errorBlob.GetAddressOf()),
            errorBlob ? static_cast<const char*>(errorBlob->GetBufferPointer()) : "D3DCompile failed for the Windows bridge diagnostic vertex shader.");
        errorBlob.Reset();
        ThrowIfFailed(
            D3DCompile(DiagnosticPixelShaderSource, strlen(DiagnosticPixelShaderSource), nullptr, nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, diagnosticPixelShaderBlob.GetAddressOf(), errorBlob.GetAddressOf()),
            errorBlob ? static_cast<const char*>(errorBlob->GetBufferPointer()) : "D3DCompile failed for the Windows bridge diagnostic pixel shader.");

        ThrowIfFailed(
            Bootstrap.GetDevice()->CreateVertexShader(diagnosticVertexShaderBlob->GetBufferPointer(), diagnosticVertexShaderBlob->GetBufferSize(), nullptr, DiagnosticVertexShader.GetAddressOf()),
            "ID3D11Device::CreateVertexShader failed for the Windows bridge diagnostic pass.");
        ThrowIfFailed(
            Bootstrap.GetDevice()->CreatePixelShader(diagnosticPixelShaderBlob->GetBufferPointer(), diagnosticPixelShaderBlob->GetBufferSize(), nullptr, DiagnosticPixelShader.GetAddressOf()),
            "ID3D11Device::CreatePixelShader failed for the Windows bridge diagnostic pass.");

        static const D3D11_INPUT_ELEMENT_DESC DiagnosticInputElements[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };

        ThrowIfFailed(
            Bootstrap.GetDevice()->CreateInputLayout(
                DiagnosticInputElements,
                static_cast<UINT>(std::size(DiagnosticInputElements)),
                diagnosticVertexShaderBlob->GetBufferPointer(),
                diagnosticVertexShaderBlob->GetBufferSize(),
                DiagnosticInputLayout.GetAddressOf()),
            "ID3D11Device::CreateInputLayout failed for the Windows bridge diagnostic pass.");
        if (!HasWrittenRenderSnapshot) {
            AppendRenderSnapshotLine("diagnostic pipeline created");
        }
    }

    /// Creates the default sampler used by material texture bindings.
    void Win32RenderManager3D::EnsureTextureSamplerState() {
        if (TextureSamplerState) {
            return;
        }

        D3D11_SAMPLER_DESC samplerDescription {};
        samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.ComparisonFunc = D3D11_COMPARISON_NEVER;
        samplerDescription.MinLOD = 0.0f;
        samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;

        ThrowIfFailed(
            Bootstrap.GetDevice()->CreateSamplerState(&samplerDescription, TextureSamplerState.GetAddressOf()),
            "ID3D11Device::CreateSamplerState failed for the Windows bridge texture sampler.");
    }

    /// Creates the small diagnostic triangle buffer used to prove the native pipeline can draw to the back buffer.
    void Win32RenderManager3D::EnsureDebugTriangleBuffer() {
        if (DebugTriangleBuffer) {
            return;
        }

        const Win32VertexPositionNormalUV vertices[] = {
            { DirectX::XMFLOAT3(-1.0f, -1.0f, 0.0f), DirectX::XMFLOAT3(0.0f, 0.0f, -1.0f), DirectX::XMFLOAT2(0.0f, 0.0f) },
            { DirectX::XMFLOAT3(-1.0f, 3.0f, 0.0f), DirectX::XMFLOAT3(0.0f, 0.0f, -1.0f), DirectX::XMFLOAT2(0.0f, 1.0f) },
            { DirectX::XMFLOAT3(3.0f, -1.0f, 0.0f), DirectX::XMFLOAT3(0.0f, 0.0f, -1.0f), DirectX::XMFLOAT2(1.0f, 0.0f) }
        };

        D3D11_BUFFER_DESC vertexBufferDescription {};
        vertexBufferDescription.ByteWidth = static_cast<UINT>(sizeof(vertices));
        vertexBufferDescription.Usage = D3D11_USAGE_DEFAULT;
        vertexBufferDescription.BindFlags = D3D11_BIND_VERTEX_BUFFER;

        D3D11_SUBRESOURCE_DATA vertexData {};
        vertexData.pSysMem = vertices;

        ThrowIfFailed(
            Bootstrap.GetDevice()->CreateBuffer(&vertexBufferDescription, &vertexData, DebugTriangleBuffer.GetAddressOf()),
            "ID3D11Device::CreateBuffer failed for the Windows bridge debug triangle.");
    }

    /// Clears and renders one camera directly into the main back buffer.
    void Win32RenderManager3D::RenderCamera(ICamera* camera, bool clearColorBuffer) {
        ID3D11DeviceContext* context = Bootstrap.GetDeviceContext();
        ID3D11RenderTargetView* renderTargetView = Bootstrap.GetRenderTargetView();
        ID3D11DepthStencilView* depthStencilView = Bootstrap.GetDepthStencilView();

        context->OMSetRenderTargets(1, &renderTargetView, depthStencilView);

        CameraClearSettings clearSettings = camera->get_ClearSettings();
        if (clearColorBuffer) {
            float4 clearColor = clearSettings.get_ClearColorEnabled() ? clearSettings.get_ClearColor() : float4(0.0f, 0.0f, 0.0f, 1.0f);
            const float clearColorValues[] = { clearColor.X, clearColor.Y, clearColor.Z, clearColor.W };
            context->ClearRenderTargetView(renderTargetView, clearColorValues);
        }

        UINT clearFlags = 0;
        if (clearSettings.get_ClearDepthEnabled()) {
            clearFlags |= D3D11_CLEAR_DEPTH;
        }
        if (clearSettings.get_ClearStencilEnabled()) {
            clearFlags |= D3D11_CLEAR_STENCIL;
        }
        if (clearFlags != 0 && depthStencilView != nullptr) {
            context->ClearDepthStencilView(depthStencilView, clearFlags, clearSettings.get_ClearDepth(), clearSettings.get_ClearStencil());
        }

        const D3D11_VIEWPORT viewport = ResolveViewport(camera);
        context->RSSetViewports(1, &viewport);

        Entity* cameraParent = camera->get_Parent();
        float3 cameraPosition = cameraParent->get_Position();
        float4 cameraOrientation = cameraParent->get_Orientation();
        float3 cameraForward = float4::RotateVector(float3(0.0f, 0.0f, -1.0f), cameraOrientation);
        float3 cameraUp = float4::RotateVector(float3(0.0f, 1.0f, 0.0f), cameraOrientation);
        float3 cameraTarget = cameraPosition + cameraForward;

        ::float4x4 view;
        float4x4::CreateLookAt(cameraPosition, cameraTarget, cameraUp, view);
        const float aspectRatio = viewport.Height > 0.0f ? viewport.Width / viewport.Height : 1.0f;
        constexpr float CameraFieldOfViewRadians = 0.78539816339f;
        ::float4x4 projection;
        float4x4::CreatePerspectiveFieldOfView(CameraFieldOfViewRadians, aspectRatio, 0.1f, 100.0f, projection);
        float4x4::Multiply(view, projection, CurrentViewProjection);

        if (!HasWrittenRenderSnapshot) {
            AppendRenderSnapshotLine(
                "camera position=" + std::to_string(cameraPosition.X) + "," + std::to_string(cameraPosition.Y) + "," + std::to_string(cameraPosition.Z) +
                " forward=" + std::to_string(cameraForward.X) + "," + std::to_string(cameraForward.Y) + "," + std::to_string(cameraForward.Z) +
                " up=" + std::to_string(cameraUp.X) + "," + std::to_string(cameraUp.Y) + "," + std::to_string(cameraUp.Z));
            AppendRenderSnapshotLine(
                "viewport=" + std::to_string(viewport.TopLeftX) + "," + std::to_string(viewport.TopLeftY) + "," + std::to_string(viewport.Width) + "," + std::to_string(viewport.Height));
            AppendRenderSnapshotLine(
                "viewProjection rows="
                "[" + std::to_string(CurrentViewProjection.M11) + "," + std::to_string(CurrentViewProjection.M12) + "," + std::to_string(CurrentViewProjection.M13) + "," + std::to_string(CurrentViewProjection.M14) + "]"
                "[" + std::to_string(CurrentViewProjection.M21) + "," + std::to_string(CurrentViewProjection.M22) + "," + std::to_string(CurrentViewProjection.M23) + "," + std::to_string(CurrentViewProjection.M24) + "]"
                "[" + std::to_string(CurrentViewProjection.M31) + "," + std::to_string(CurrentViewProjection.M32) + "," + std::to_string(CurrentViewProjection.M33) + "," + std::to_string(CurrentViewProjection.M34) + "]"
                "[" + std::to_string(CurrentViewProjection.M41) + "," + std::to_string(CurrentViewProjection.M42) + "," + std::to_string(CurrentViewProjection.M43) + "," + std::to_string(CurrentViewProjection.M44) + "]");
        }

        IRenderQueue3D* renderQueue = camera->get_RenderQueue3D();
        if (renderQueue != nullptr) {
            renderQueue->VisitOrdered(this);
        }
    }

    /// Draws one clip-space triangle with an identity transform to validate the native draw pipeline.
    void Win32RenderManager3D::DrawDebugTriangle() {
        EnsureDebugTriangleBuffer();

        ID3D11DeviceContext* context = Bootstrap.GetDeviceContext();
        const UINT stride = sizeof(Win32VertexPositionNormalUV);
        const UINT offset = 0;
        ID3D11Buffer* vertexBuffer = DebugTriangleBuffer.Get();
        context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);

        ::float4x4 identity = float4x4::get_Identity();
        ::float4x4 transposedIdentity;
        float4x4::Transpose(identity, transposedIdentity);

        Win32TransformConstants constants {};
        constants.WorldViewProjection = StoreMatrix(transposedIdentity);
        context->UpdateSubresource(TransformBuffer.Get(), 0, nullptr, &constants, 0, 0);

        context->IASetInputLayout(InputLayout.Get());
        context->VSSetShader(VertexShader.Get(), nullptr, 0);
        context->PSSetShader(PixelShader.Get(), nullptr, 0);
        context->Draw(3, 0);
    }

    /// Builds a shader resource from one packaged shader asset and material program selection.
    Win32ShaderResource Win32RenderManager3D::BuildShaderResource(MaterialAsset* materialAsset, ShaderAsset* shaderAsset) {
        if (materialAsset == nullptr) {
            throw new ArgumentNullException("materialAsset");
        }

        if (shaderAsset == nullptr) {
            throw new ArgumentNullException("shaderAsset");
        }

        if (String::IsNullOrWhiteSpace(materialAsset->VertexProgram)) {
            throw new InvalidOperationException("Material assets must define a vertex program name.");
        }

        if (String::IsNullOrWhiteSpace(materialAsset->PixelProgram)) {
            throw new InvalidOperationException("Material assets must define a pixel program name.");
        }

        if (String::IsNullOrWhiteSpace(materialAsset->Variant)) {
            throw new InvalidOperationException("Material assets must define a shader variant.");
        }

        ShaderBinaryAsset* vertexBinary = GetShaderBinary(shaderAsset, materialAsset->VertexProgram, ShaderStage::Vertex, materialAsset->Variant);
        ShaderBinaryAsset* pixelBinary = GetShaderBinary(shaderAsset, materialAsset->PixelProgram, ShaderStage::Pixel, materialAsset->Variant);
        if (vertexBinary == nullptr || pixelBinary == nullptr) {
            throw new InvalidOperationException("Material shader binaries could not be resolved.");
        }

        std::vector<std::string> semanticStorage;
        std::vector<D3D11_INPUT_ELEMENT_DESC> inputElements = BuildInputElements(shaderAsset, materialAsset->VertexProgram, materialAsset->Variant, semanticStorage);
        if (inputElements.empty()) {
            inputElements = BuildDefaultInputElements();
        }

        Win32ShaderResource shaderResource {};
        ThrowIfFailed(
            Bootstrap.GetDevice()->CreateVertexShader(vertexBinary->Bytecode->Data, static_cast<SIZE_T>(vertexBinary->Bytecode->Length), nullptr, shaderResource.VertexShader.GetAddressOf()),
            "ID3D11Device::CreateVertexShader failed for a packaged material shader.");
        ThrowIfFailed(
            Bootstrap.GetDevice()->CreatePixelShader(pixelBinary->Bytecode->Data, static_cast<SIZE_T>(pixelBinary->Bytecode->Length), nullptr, shaderResource.PixelShader.GetAddressOf()),
            "ID3D11Device::CreatePixelShader failed for a packaged material shader.");
        ThrowIfFailed(
            Bootstrap.GetDevice()->CreateInputLayout(
                inputElements.data(),
                static_cast<UINT>(inputElements.size()),
                vertexBinary->Bytecode->Data,
                static_cast<SIZE_T>(vertexBinary->Bytecode->Length),
                shaderResource.InputLayout.GetAddressOf()),
            "ID3D11Device::CreateInputLayout failed for a packaged material shader.");
        return shaderResource;
    }

    /// Builds Direct3D input elements from the vertex signature exposed by a shader program.
    std::vector<D3D11_INPUT_ELEMENT_DESC> Win32RenderManager3D::BuildInputElements(
        ShaderAsset* shaderAsset,
        std::string vertexProgram,
        std::string variant,
        std::vector<std::string>& semanticStorage) {
        if (shaderAsset == nullptr) {
            throw new ArgumentNullException("shaderAsset");
        }

        if (String::IsNullOrWhiteSpace(vertexProgram)) {
            throw new InvalidOperationException("Vertex program name must be provided.");
        }

        if (String::IsNullOrWhiteSpace(variant)) {
            throw new InvalidOperationException("Shader variant name must be provided.");
        }

        (void)variant;

        ShaderProgramAsset* vertexProgramAsset = nullptr;
        if (shaderAsset->Programs != nullptr) {
            for (int32_t programIndex = 0; programIndex < shaderAsset->Programs->Length; programIndex++) {
                ShaderProgramAsset* candidate = (*shaderAsset->Programs)[programIndex];
                if (candidate == nullptr) {
                    continue;
                }

                if (candidate->Stage != ShaderStage::Vertex) {
                    continue;
                }

                if (!String::Equals(candidate->Name, vertexProgram, StringComparison::Ordinal)) {
                    continue;
                }

                vertexProgramAsset = candidate;
                break;
            }
        }

        if (vertexProgramAsset == nullptr || vertexProgramAsset->Inputs == nullptr || vertexProgramAsset->Inputs->Length == 0) {
            return BuildDefaultInputElements();
        }

        std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
        elements.reserve(static_cast<std::size_t>(vertexProgramAsset->Inputs->Length));
        semanticStorage.reserve(static_cast<std::size_t>(vertexProgramAsset->Inputs->Length));

        UINT byteOffset = 0;
        for (int32_t inputIndex = 0; inputIndex < vertexProgramAsset->Inputs->Length; inputIndex++) {
            ShaderVertexElementAsset* element = (*vertexProgramAsset->Inputs)[inputIndex];
            if (element == nullptr) {
                throw new InvalidOperationException("Shader program input elements contain a null entry.");
            }

            semanticStorage.push_back(element->Semantic);
            D3D11_INPUT_ELEMENT_DESC inputElement {};
            inputElement.SemanticName = semanticStorage.back().c_str();
            inputElement.SemanticIndex = static_cast<UINT>(element->Index);
            inputElement.Format = ResolveVertexElementFormat(element->Format);
            inputElement.InputSlot = 0;
            inputElement.AlignedByteOffset = byteOffset;
            inputElement.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
            inputElement.InstanceDataStepRate = 0;
            elements.push_back(inputElement);

            switch (inputElement.Format) {
                case DXGI_FORMAT_R32_FLOAT:
                    byteOffset += 4;
                    break;
                case DXGI_FORMAT_R32G32_FLOAT:
                    byteOffset += 8;
                    break;
                case DXGI_FORMAT_R32G32B32_FLOAT:
                    byteOffset += 12;
                    break;
                case DXGI_FORMAT_R32G32B32A32_FLOAT:
                    byteOffset += 16;
                    break;
                case DXGI_FORMAT_R8G8B8A8_UNORM:
                    byteOffset += 4;
                    break;
                default:
                    throw new InvalidOperationException("Unsupported vertex element format.");
            }
        }

        return elements;
    }

    /// Resolves one shader binary for the requested shader program, stage, and variant.
    ShaderBinaryAsset* Win32RenderManager3D::GetShaderBinary(ShaderAsset* shaderAsset, std::string programName, ShaderStage stage, std::string variant) {
        if (shaderAsset == nullptr) {
            throw new ArgumentNullException("shaderAsset");
        }

        if (shaderAsset->Binaries == nullptr) {
            throw new InvalidOperationException("Shader assets must include compiled binaries.");
        }

        for (int32_t binaryIndex = 0; binaryIndex < shaderAsset->Binaries->Length; binaryIndex++) {
            ShaderBinaryAsset* binary = (*shaderAsset->Binaries)[binaryIndex];
            if (binary == nullptr) {
                continue;
            }

            if (!String::Equals(binary->TargetName, shaderAsset->TargetName, StringComparison::OrdinalIgnoreCase)) {
                continue;
            }

            if (!String::Equals(binary->ProgramName, programName, StringComparison::Ordinal)) {
                continue;
            }

            if (binary->Stage != stage) {
                continue;
            }

            if (!String::Equals(binary->Variant, variant, StringComparison::Ordinal)) {
                continue;
            }

            if (binary->Bytecode == nullptr || binary->Bytecode->Length == 0) {
                throw new InvalidOperationException("Shader binary does not include bytecode.");
            }

            return binary;
        }

        throw new InvalidOperationException("Shader binary was not found for the requested program.");
    }

    /// Resolves the DirectX input format for one shader vertex element format string.
    DXGI_FORMAT Win32RenderManager3D::ResolveVertexElementFormat(std::string format) const {
        if (String::Equals(format, "float", StringComparison::OrdinalIgnoreCase) ||
            String::Equals(format, "float1", StringComparison::OrdinalIgnoreCase)) {
            return DXGI_FORMAT_R32_FLOAT;
        }

        if (String::Equals(format, "float2", StringComparison::OrdinalIgnoreCase)) {
            return DXGI_FORMAT_R32G32_FLOAT;
        }

        if (String::Equals(format, "float3", StringComparison::OrdinalIgnoreCase)) {
            return DXGI_FORMAT_R32G32B32_FLOAT;
        }

        if (String::Equals(format, "float4", StringComparison::OrdinalIgnoreCase)) {
            return DXGI_FORMAT_R32G32B32A32_FLOAT;
        }

        if (String::Equals(format, "uint4", StringComparison::OrdinalIgnoreCase)) {
            return DXGI_FORMAT_R32G32B32A32_UINT;
        }

        if (String::Equals(format, "int4", StringComparison::OrdinalIgnoreCase)) {
            return DXGI_FORMAT_R32G32B32A32_SINT;
        }

        throw new InvalidOperationException(std::string("Unsupported shader vertex element format: ") + format);
    }

    /// Applies the shader and resource bindings for one runtime material.
    void Win32RenderManager3D::ApplyMaterial(RuntimeMaterial* material) {
        if (material == nullptr) {
            throw new ArgumentNullException("material");
        }

        RuntimeMaterial* rootMaterial = material->ResolveRootMaterial();
        if (rootMaterial == nullptr) {
            throw new InvalidOperationException("Runtime materials must resolve to a root material.");
        }

        std::string materialId = rootMaterial->get_Id();
        if (materialId.empty()) {
            throw new InvalidOperationException("Runtime materials must have an identifier before rendering.");
        }

        auto shaderResourceIt = MaterialShaderResources.find(materialId);
        if (shaderResourceIt == MaterialShaderResources.end() || shaderResourceIt->second == nullptr) {
            throw new InvalidOperationException(std::string("No shader resource was cached for runtime material '") + materialId + std::string("'."));
        }

        ID3D11DeviceContext* context = Bootstrap.GetDeviceContext();
        Win32ShaderResource* shaderResource = shaderResourceIt->second.get();
        context->IASetInputLayout(shaderResource->InputLayout.Get());
        context->VSSetShader(shaderResource->VertexShader.Get(), nullptr, 0);
        context->PSSetShader(shaderResource->PixelShader.Get(), nullptr, 0);

        ID3D11Buffer* transformBuffer = TransformBuffer.Get();
        context->VSSetConstantBuffers(0, 1, &transformBuffer);
        context->PSSetConstantBuffers(0, 1, &transformBuffer);

        MaterialLayout* layout = material->get_Layout();
        MaterialPropertyBlock* properties = material->get_Properties();
        if (layout != nullptr && properties != nullptr) {
            Array<MaterialLayoutBinding*>* textureBindings = layout->get_TextureBindings();
            if (textureBindings != nullptr) {
                for (int32_t bindingIndex = 0; bindingIndex < textureBindings->Length; bindingIndex++) {
                    MaterialLayoutBinding* binding = (*textureBindings)[bindingIndex];
                    if (binding == nullptr) {
                        continue;
                    }

                    BindMaterialTexture(material, bindingIndex, binding->get_Slot());
                }
            }
        }
    }

    /// Binds one runtime material texture to the pixel shader if the texture has been uploaded.
    void Win32RenderManager3D::BindMaterialTexture(RuntimeMaterial* material, int32_t bindingIndex, int32_t slot) {
        if (material == nullptr) {
            throw new ArgumentNullException("material");
        }

        if (bindingIndex < 0) {
            throw new ArgumentOutOfRangeException("bindingIndex", "Binding index cannot be negative.");
        }

        if (slot < 0) {
            throw new ArgumentOutOfRangeException("slot", "Binding slot cannot be negative.");
        }

        MaterialPropertyBlock* properties = material->get_Properties();
        if (properties == nullptr) {
            return;
        }

        RuntimeTexture* texture = properties->GetTexture(bindingIndex);
        ID3D11ShaderResourceView* resourceView = ResolveTextureResourceView(texture);
        ID3D11DeviceContext* context = Bootstrap.GetDeviceContext();
        context->PSSetShaderResources(static_cast<UINT>(slot), 1, &resourceView);
        if (resourceView != nullptr) {
            ID3D11SamplerState* samplerState = TextureSamplerState.Get();
            context->PSSetSamplers(static_cast<UINT>(slot), 1, &samplerState);
        }
    }

    /// Resolves an uploaded shader resource view for one runtime texture.
    ID3D11ShaderResourceView* Win32RenderManager3D::ResolveTextureResourceView(RuntimeTexture* texture) const {
        if (texture == nullptr) {
            return nullptr;
        }

        std::string textureId = texture->get_Id();
        if (textureId.empty()) {
            return nullptr;
        }

        auto resource = TextureResources.find(textureId);
        if (resource == TextureResources.end() || resource->second == nullptr) {
            return nullptr;
        }

        return resource->second->ShaderResourceView.Get();
    }

    /// Clears the back buffer to a solid fallback color when nothing else renders.
    void Win32RenderManager3D::ClearBackBuffer(float red, float green, float blue, float alpha) {
        ID3D11RenderTargetView* renderTargetView = Bootstrap.GetRenderTargetView();
        ID3D11DepthStencilView* depthStencilView = Bootstrap.GetDepthStencilView();
        ID3D11DeviceContext* context = Bootstrap.GetDeviceContext();

        const float clearColor[] = { red, green, blue, alpha };
        context->OMSetRenderTargets(1, &renderTargetView, depthStencilView);
        context->ClearRenderTargetView(renderTargetView, clearColor);
        if (depthStencilView != nullptr) {
            context->ClearDepthStencilView(depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
        }

        D3D11_VIEWPORT viewport {};
        viewport.TopLeftX = 0.0f;
        viewport.TopLeftY = 0.0f;
        viewport.Width = static_cast<float>(Bootstrap.GetWidth());
        viewport.Height = static_cast<float>(Bootstrap.GetHeight());
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        context->RSSetViewports(1, &viewport);
    }

    /// Creates the native 2D bridge for one DirectX11 bootstrap.
    Win32RenderManager2D::Win32RenderManager2D(DirectX11Bootstrap& bootstrap)
        : Bootstrap(bootstrap) {
    }

    /// Resolves a camera viewport against the current swap-chain size.
    D3D11_VIEWPORT Win32RenderManager3D::ResolveViewport(ICamera* camera) const {
        const float4 rawViewport = camera->get_Viewport();
        float offsetX = rawViewport.X;
        float offsetY = rawViewport.Y;
        float width = rawViewport.Z;
        float height = rawViewport.W;

        if (width <= 1.0f && height <= 1.0f && width > 0.0f && height > 0.0f) {
            offsetX *= static_cast<float>(Bootstrap.GetWidth());
            offsetY *= static_cast<float>(Bootstrap.GetHeight());
            width *= static_cast<float>(Bootstrap.GetWidth());
            height *= static_cast<float>(Bootstrap.GetHeight());
        }

        if (width <= 0.0f || height <= 0.0f) {
            offsetX = 0.0f;
            offsetY = 0.0f;
            width = static_cast<float>(Bootstrap.GetWidth());
            height = static_cast<float>(Bootstrap.GetHeight());
        }

        D3D11_VIEWPORT viewport {};
        viewport.TopLeftX = offsetX;
        viewport.TopLeftY = offsetY;
        viewport.Width = width;
        viewport.Height = height;
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;
        return viewport;
    }

    /// Builds a placeholder runtime texture from raw asset metadata.
    RuntimeTexture* Win32RenderManager2D::BuildTextureFromRaw(TextureAsset* data) {
        RuntimeTexture* runtimeTexture = new RuntimeTexture();
        if (data != nullptr) {
            runtimeTexture->set_Id(data->get_Id());
            runtimeTexture->set_Width(data->Width);
            runtimeTexture->set_Height(data->Height);

            if (data->Colors == nullptr || data->Colors->Length == 0) {
                throw new InvalidOperationException("Texture assets must include embedded color data.");
            }

            if (data->Width == 0 || data->Height == 0) {
                throw new InvalidOperationException("Texture assets must define a non-zero width and height.");
            }

            D3D11_TEXTURE2D_DESC textureDescription {};
            textureDescription.Width = static_cast<UINT>(data->Width);
            textureDescription.Height = static_cast<UINT>(data->Height);
            textureDescription.MipLevels = 1;
            textureDescription.ArraySize = 1;
            textureDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            textureDescription.SampleDesc.Count = 1;
            textureDescription.Usage = D3D11_USAGE_DEFAULT;
            textureDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            D3D11_SUBRESOURCE_DATA textureData {};
            textureData.pSysMem = data->Colors->Data;
            textureData.SysMemPitch = static_cast<UINT>(static_cast<UINT64>(data->Width) * 4ULL);

            Win32TextureResource textureResource;
            ThrowIfFailed(
                Bootstrap.GetDevice()->CreateTexture2D(&textureDescription, &textureData, textureResource.Texture.GetAddressOf()),
                "ID3D11Device::CreateTexture2D failed for a packaged texture asset.");

            D3D11_SHADER_RESOURCE_VIEW_DESC resourceViewDescription {};
            resourceViewDescription.Format = textureDescription.Format;
            resourceViewDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            resourceViewDescription.Texture2D.MostDetailedMip = 0;
            resourceViewDescription.Texture2D.MipLevels = 1;
            ThrowIfFailed(
                Bootstrap.GetDevice()->CreateShaderResourceView(textureResource.Texture.Get(), &resourceViewDescription, textureResource.ShaderResourceView.GetAddressOf()),
                "ID3D11Device::CreateShaderResourceView failed for a packaged texture asset.");

            TextureResources[data->get_Id()] = std::make_unique<Win32TextureResource>(std::move(textureResource));
        }

        return runtimeTexture;
    }

    /// Accepts a sprite draw request without issuing backend rendering yet.
    void Win32RenderManager2D::DrawSprite(ISpriteDrawable2D* sprite) {
    }

    /// Accepts a text draw request without issuing backend rendering yet.
    void Win32RenderManager2D::DrawText(ITextDrawable2D* text) {
    }

    /// Accepts a rounded-rectangle draw request without issuing backend rendering yet.
    void Win32RenderManager2D::DrawRoundedRect(IRoundedRectDrawable2D* shape) {
    }
#endif
}
