#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace helengine::windows {
    /// <summary>Owns one instance's immutable bind geometry and compute-deformed position/normal/UV vertex buffer.</summary>
    class Win32GpuSkin {
        /// <summary>Matches the engine's IEEE32 position, normal and UV vertex format.</summary>
        static constexpr UINT VertexStride = 32;
        /// <summary>Bounds constant-buffer skin palettes to the explicitly supported skeleton size.</summary>
        static constexpr UINT MaximumJoints = 64;
        /// <summary>Computes each output from bind geometry, preserving UVs and correcting scaled bone normals.</summary>
        static constexpr const char* ShaderSource = R"hlsl(
ByteAddressBuffer BindVertices : register(t0);
ByteAddressBuffer Influences : register(t1);
RWByteAddressBuffer SkinnedVertices : register(u0);
cbuffer SkinPalette : register(b0) {
    row_major float4x4 Bones[64];
    row_major float4x4 NormalBones[64];
    uint VertexCount;
    uint3 Padding;
};
[numthreads(64,1,1)]
void Main(uint3 dispatch : SV_DispatchThreadID) {
    uint vertex=dispatch.x;
    if(vertex>=VertexCount) return;
    uint offset=vertex*32;
    float3 bindPosition=asfloat(BindVertices.Load3(offset));
    float3 bindNormal=asfloat(BindVertices.Load3(offset+12));
    uint4 joints=Influences.Load4(offset);
    float4 weights=asfloat(Influences.Load4(offset+16));
    float3 position=0;
    float3 normal=0;
    [unroll] for(uint influence=0;influence<4;++influence) {
        position+=weights[influence]*mul(float4(bindPosition,1),Bones[joints[influence]]).xyz;
        normal+=weights[influence]*mul(float4(bindNormal,0),NormalBones[joints[influence]]).xyz;
    }
    float normalLength=length(normal);
    if(normalLength>0) normal/=normalLength;
    SkinnedVertices.Store3(offset,asuint(position));
    SkinnedVertices.Store3(offset+12,asuint(normal));
    SkinnedVertices.Store2(offset+24,BindVertices.Load2(offset+24));
}
)hlsl";
        /// <summary>Borrows the private runtime model's buffer slot until explicit restoration before model unload.</summary>
        Microsoft::WRL::ComPtr<ID3D11Buffer>* Target;
        /// <summary>Retains the original instance resource for explicit detach.</summary>
        Microsoft::WRL::ComPtr<ID3D11Buffer> Original;
        /// <summary>Owns a shader-readable copy that is never overwritten by animation.</summary>
        Microsoft::WRL::ComPtr<ID3D11Buffer> BindVertices;
        /// <summary>Owns four integer joints and four normalized float weights per vertex.</summary>
        Microsoft::WRL::ComPtr<ID3D11Buffer> Influences;
        /// <summary>Owns the UAV-capable vertex resource consumed by every existing rendering pass.</summary>
        Microsoft::WRL::ComPtr<ID3D11Buffer> Output;
        /// <summary>Owns the bone and normal palette constant buffer.</summary>
        Microsoft::WRL::ComPtr<ID3D11Buffer> Constants;
        /// <summary>Retains the creating device and its render-thread immediate context.</summary>
        Microsoft::WRL::ComPtr<ID3D11Device> Device;
        /// <summary>Issues one compute dispatch after each authored pose update.</summary>
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> Context;
        /// <summary>Reads the immutable raw bind vertices.</summary>
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> BindView;
        /// <summary>Reads the validated raw influences.</summary>
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> InfluenceView;
        /// <summary>Writes the private instance vertex resource.</summary>
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> OutputView;
        /// <summary>Retains the compiled shader for the complete skin lifetime.</summary>
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> Shader;
        /// <summary>Stores the number of authored vertices covered by each dispatch.</summary>
        UINT VertexCount;
        /// <summary>Stores the exact palette length accepted from the animation controller.</summary>
        UINT JointCount;

        /// <summary>Reports the actual Direct3D failure instead of producing partially initialized skin state.</summary>
        static void Check(HRESULT result, const char* operation) {
            if(FAILED(result)) throw std::runtime_error(std::string(operation)+" failed (HRESULT "+std::to_string(static_cast<unsigned long>(result))+")");
        }
        /// <summary>Creates an R32-typeless raw shader view of a byte-address buffer.</summary>
        void CreateRawView(ID3D11Buffer* buffer, UINT byteCount, ID3D11ShaderResourceView** view) {
            D3D11_SHADER_RESOURCE_VIEW_DESC description{};
            description.Format=DXGI_FORMAT_R32_TYPELESS; description.ViewDimension=D3D11_SRV_DIMENSION_BUFFEREX;
            description.BufferEx.NumElements=byteCount/4; description.BufferEx.Flags=D3D11_BUFFEREX_SRV_FLAG_RAW;
            Check(Device->CreateShaderResourceView(buffer,&description,view),"Skin raw input view");
        }
        /// <summary>Builds the inverse transpose of the affine linear part using double boundary arithmetic.</summary>
        static void WriteNormalMatrix(const float* source, float* target) {
            const double a=source[0],b=source[1],c=source[2],d=source[4],e=source[5],f=source[6],g=source[8],h=source[9],i=source[10];
            const double determinant=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
            if(!std::isfinite(determinant) || determinant==0) throw std::invalid_argument("Skin palette contains a singular bone matrix");
            const std::array<double,9> cofactors{e*i-f*h,f*g-d*i,d*h-e*g,c*h-b*i,a*i-c*g,b*g-a*h,b*f-c*e,c*d-a*f,a*e-b*d};
            for(UINT row=0;row<3;++row) for(UINT column=0;column<3;++column) {
                const double value=cofactors[row*3+column]/determinant;
                if(!std::isfinite(value) || std::abs(value)>std::numeric_limits<float>::max()) throw std::invalid_argument("Skin normal matrix exceeds float range");
                target[row*4+column]=static_cast<float>(value);
            }
            target[15]=1;
        }
    public:
        /// <summary>Validates four influences per vertex and atomically attaches private GPU resources to one model instance.</summary>
        Win32GpuSkin(Microsoft::WRL::ComPtr<ID3D11Buffer>& target, UINT vertexCount, const int32_t* joints, const float* weights, size_t influenceCount, UINT jointCount)
            :Target(std::addressof(target)),VertexCount(vertexCount),JointCount(jointCount) {
            if(!target.Get() || !joints || !weights || vertexCount==0 || jointCount==0 || jointCount>MaximumJoints || influenceCount!=static_cast<size_t>(vertexCount)*4)
                throw std::invalid_argument("GPU skin requires a private vertex buffer, four influences per vertex and 1..64 joints");
            if(vertexCount>std::numeric_limits<UINT>::max()/VertexStride || (vertexCount+63ull)/64>D3D11_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION)
                throw std::invalid_argument("GPU skin vertex count exceeds Direct3D dispatch bounds");
            const UINT bytes=vertexCount*VertexStride;
            D3D11_BUFFER_DESC originalDescription{}; target.Get()->GetDesc(&originalDescription);
            if(originalDescription.ByteWidth!=bytes || !(originalDescription.BindFlags&D3D11_BIND_VERTEX_BUFFER)) throw std::invalid_argument("GPU skin requires the engine's 32-byte position/normal/UV vertex format");
            std::vector<uint32_t> influences(static_cast<size_t>(vertexCount)*8);
            for(UINT vertex=0;vertex<vertexCount;++vertex) {
                double total=0;
                for(UINT index=0;index<4;++index) {
                    const size_t influence=static_cast<size_t>(vertex)*4+index;
                    if(joints[influence]<0 || static_cast<UINT>(joints[influence])>=jointCount || !std::isfinite(weights[influence]) || weights[influence]<0)
                        throw std::invalid_argument("GPU skin has a nonfinite/negative weight or out-of-range joint");
                    total+=weights[influence];
                }
                if(total<=0 || !std::isfinite(total)) throw std::invalid_argument("GPU skin requires a positive weight sum for every vertex");
                for(UINT index=0;index<4;++index) {
                    const size_t influence=static_cast<size_t>(vertex)*4+index;
                    influences[static_cast<size_t>(vertex)*8+index]=static_cast<uint32_t>(joints[influence]);
                    const float weight=static_cast<float>(weights[influence]/total);
                    std::memcpy(&influences[static_cast<size_t>(vertex)*8+4+index],&weight,sizeof(weight));
                }
            }
            Original=target; Original->GetDevice(&Device);
            if(Device->GetFeatureLevel()<D3D_FEATURE_LEVEL_11_0) throw std::runtime_error("GPU skin requires Direct3D feature level 11.0");
            Device->GetImmediateContext(&Context);
            D3D11_BUFFER_DESC description{}; description.ByteWidth=bytes; description.Usage=D3D11_USAGE_DEFAULT;
            description.BindFlags=D3D11_BIND_SHADER_RESOURCE; description.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
            Check(Device->CreateBuffer(&description,nullptr,&BindVertices),"Skin bind vertex allocation");
            Context->CopyResource(BindVertices.Get(),Original.Get()); CreateRawView(BindVertices.Get(),bytes,&BindView);
            description.Usage=D3D11_USAGE_IMMUTABLE;
            D3D11_SUBRESOURCE_DATA data{}; data.pSysMem=influences.data();
            Check(Device->CreateBuffer(&description,&data,&Influences),"Skin influences allocation"); CreateRawView(Influences.Get(),bytes,&InfluenceView);
            description.Usage=D3D11_USAGE_DEFAULT; description.BindFlags=D3D11_BIND_VERTEX_BUFFER|D3D11_BIND_UNORDERED_ACCESS;
            Check(Device->CreateBuffer(&description,nullptr,&Output),"Skin output allocation"); Context->CopyResource(Output.Get(),Original.Get());
            D3D11_UNORDERED_ACCESS_VIEW_DESC outputDescription{}; outputDescription.Format=DXGI_FORMAT_R32_TYPELESS;
            outputDescription.ViewDimension=D3D11_UAV_DIMENSION_BUFFER; outputDescription.Buffer.NumElements=bytes/4; outputDescription.Buffer.Flags=D3D11_BUFFER_UAV_FLAG_RAW;
            Check(Device->CreateUnorderedAccessView(Output.Get(),&outputDescription,&OutputView),"Skin output view");
            description.ByteWidth=MaximumJoints*16*sizeof(float)*2+16; description.BindFlags=D3D11_BIND_CONSTANT_BUFFER; description.MiscFlags=0;
            Check(Device->CreateBuffer(&description,nullptr,&Constants),"Skin palette allocation");
            Microsoft::WRL::ComPtr<ID3DBlob> code,error;
            const HRESULT compiled=D3DCompile(ShaderSource,std::strlen(ShaderSource),"Win32GpuSkin",nullptr,nullptr,"Main","cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
            if(FAILED(compiled)) throw std::runtime_error(error ? static_cast<const char*>(error->GetBufferPointer()) : "GPU skin shader compilation failed");
            Check(Device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&Shader),"Skin compute shader");
            *Target=Output;
        }
        /// <summary>Prevents duplicated ownership of the same borrowed instance buffer slot.</summary>
        Win32GpuSkin(const Win32GpuSkin&)=delete;
        /// <summary>Prevents assigning a second resource owner to an attached instance.</summary>
        Win32GpuSkin& operator=(const Win32GpuSkin&)=delete;
        /// <summary>Identifies the borrowed model slot for duplicate registration validation.</summary>
        const void* GetTarget() const { return Target; }
        /// <summary>Restores the original vertex resource before the caller unloads its private runtime model.</summary>
        void Restore() {
            if(!Target || Target->Get()!=Output.Get()) throw std::logic_error("GPU skin target was detached or replaced before destruction");
            Microsoft::WRL::ComPtr<ID3D11Buffer> previousVertex; UINT stride=0,offset=0;
            Context->IAGetVertexBuffers(0,1,&previousVertex,&stride,&offset);
            if(previousVertex.Get()==Output.Get()) { auto* original=Original.Get(); Context->IASetVertexBuffers(0,1,&original,&stride,&offset); }
            *Target=Original; Target=nullptr;
        }
        /// <summary>Uploads affine row-major row-vector transforms and dispatches once from the immutable bind pose.</summary>
        void Update(const float* palette, size_t paletteLength) {
            if(!Target || Target->Get()!=Output.Get()) throw std::logic_error("GPU skin target is no longer attached");
            if(!palette || paletteLength!=static_cast<size_t>(JointCount)*16) throw std::invalid_argument("GPU skin palette length must equal jointCount*16");
            std::array<float,MaximumJoints*16*2+4> constants{};
            for(UINT joint=0;joint<JointCount;++joint) {
                const float* matrix=palette+joint*16;
                for(UINT index=0;index<16;++index) if(!std::isfinite(matrix[index])) throw std::invalid_argument("GPU skin palette contains a nonfinite matrix component");
                if(matrix[3]!=0 || matrix[7]!=0 || matrix[11]!=0 || matrix[15]!=1) throw std::invalid_argument("GPU skin palette requires affine row-vector transforms");
                std::memcpy(constants.data()+joint*16,matrix,16*sizeof(float));
                WriteNormalMatrix(matrix,constants.data()+MaximumJoints*16+joint*16);
            }
            std::memcpy(constants.data()+MaximumJoints*16*2,&VertexCount,sizeof(VertexCount));
            Context->UpdateSubresource(Constants.Get(),0,nullptr,constants.data(),0,0);
            Microsoft::WRL::ComPtr<ID3D11Buffer> previousVertex; UINT stride=0,offset=0;
            Context->IAGetVertexBuffers(0,1,&previousVertex,&stride,&offset);
            const bool restoreVertex=previousVertex.Get()==Output.Get();
            if(restoreVertex) { ID3D11Buffer* empty=nullptr; UINT zero=0; Context->IASetVertexBuffers(0,1,&empty,&zero,&zero); }
            ID3D11ShaderResourceView* inputs[2]{BindView.Get(),InfluenceView.Get()}; auto* output=OutputView.Get(); auto* buffer=Constants.Get();
            Context->CSSetShader(Shader.Get(),nullptr,0); Context->CSSetShaderResources(0,2,inputs);
            Context->CSSetUnorderedAccessViews(0,1,&output,nullptr); Context->CSSetConstantBuffers(0,1,&buffer);
            Context->Dispatch((VertexCount+63)/64,1,1);
            ID3D11ShaderResourceView* emptyInputs[2]{}; ID3D11UnorderedAccessView* emptyOutput=nullptr; ID3D11Buffer* emptyBuffer=nullptr;
            Context->CSSetUnorderedAccessViews(0,1,&emptyOutput,nullptr); Context->CSSetShaderResources(0,2,emptyInputs);
            Context->CSSetConstantBuffers(0,1,&emptyBuffer); Context->CSSetShader(nullptr,nullptr,0);
            if(restoreVertex) { auto* previous=previousVertex.Get(); Context->IASetVertexBuffers(0,1,&previous,&stride,&offset); }
        }
    };
}


