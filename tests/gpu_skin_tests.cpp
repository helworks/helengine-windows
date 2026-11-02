#include "platform/windows/win32/win32_gpu_skin.hpp"
#include <d3d11sdklayers.h>
#include <iostream>
#include <limits>

/// <summary>Validates production compute skinning with offscreen WARP buffers and numeric readback.</summary>
class GpuSkinTests {
    /// <summary>Rejects failed contracts with a descriptive error.</summary>
    static void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
    /// <summary>Compares actual GPU results using a tolerance appropriate for float shader arithmetic.</summary>
    static void Near(float actual, float expected) { Require(std::abs(actual-expected)<0.0001f, "Unexpected GPU vertex component"); }
    /// <summary>Copies the actual published vertex buffer into CPU-readable owned staging memory.</summary>
    static std::array<float,16> Read(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Buffer* buffer) {
        D3D11_BUFFER_DESC description{}; buffer->GetDesc(&description);
        description.BindFlags=description.MiscFlags=description.StructureByteStride=0;
        description.Usage=D3D11_USAGE_STAGING; description.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        Microsoft::WRL::ComPtr<ID3D11Buffer> staging;
        Require(SUCCEEDED(device->CreateBuffer(&description,nullptr,&staging)),"Staging creation failed");
        context->CopyResource(staging.Get(),buffer);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Require(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"Readback failed");
        std::array<float,16> result{}; std::memcpy(result.data(),mapped.pData,sizeof(result));
        context->Unmap(staging.Get(),0); return result;
    }
public:
    /// <summary>Exercises identity, mixed weights, normal correction, repeated updates and resource ownership.</summary>
    static void Run() {
        using Microsoft::WRL::ComPtr;
        ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
        Require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_DEBUG,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)),"Debug WARP creation failed");
        ComPtr<ID3D11InfoQueue> messages; Require(SUCCEEDED(device.As(&messages)),"Debug queue missing");
        const float n=static_cast<float>(1/std::sqrt(2.0));
        const std::array<float,16> source{1,2,3,n,n,0,.25f,.75f, 2,4,6,0,0,1,.5f,.125f};
        D3D11_BUFFER_DESC desc{}; desc.ByteWidth=sizeof(source); desc.Usage=D3D11_USAGE_IMMUTABLE; desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA data{}; data.pSysMem=source.data(); ComPtr<ID3D11Buffer> vertex;
        Require(SUCCEEDED(device->CreateBuffer(&desc,&data,&vertex)),"Source allocation failed");
        const auto original=vertex;
        std::array<int32_t,8> joints{0,1,0,0,1,0,0,0}; std::array<float,8> weights{.25f,.75f,0,0,1,0,0,0};
        helengine::windows::Win32GpuSkin skin(vertex,2,joints.data(),weights.data(),8,2);
        Require(vertex.Get()!=original.Get(),"Instance did not receive private output");
        std::array<float,32> palette{}; palette[0]=palette[5]=palette[10]=palette[15]=palette[16]=palette[21]=palette[26]=palette[31]=1;
        skin.Update(palette.data(),32); auto actual=Read(device.Get(),context.Get(),vertex.Get());
        for(size_t i=0;i<source.size();++i) Near(actual[i],source[i]);
        palette[16]=2; palette[21]=3; palette[26]=4; palette[28]=10;
        skin.Update(palette.data(),32); actual=Read(device.Get(),context.Get(),vertex.Get());
        Near(actual[0],9.25f); Near(actual[1],5); Near(actual[2],9.75f);
        const double nx=.25+.75/2; const double ny=.25+.75/3; const double length=std::sqrt(nx*nx+ny*ny);
        Near(actual[3],static_cast<float>(nx/length)); Near(actual[4],static_cast<float>(ny/length));
        Near(actual[8],14); Near(actual[9],12); Near(actual[10],24); Near(actual[13],1);
        Near(actual[6],source[6]); Near(actual[7],source[7]);
        palette[16]=0; palette[17]=2; palette[20]=-3; palette[21]=0;
        skin.Update(palette.data(),32); actual=Read(device.Get(),context.Get(),vertex.Get());
        Near(actual[0],3.25f); Near(actual[1],2); Near(actual[2],9.75f); Near(actual[3],0); Near(actual[4],1);
        palette[16]=2; palette[17]=0; palette[20]=0; palette[21]=3;
        UINT stride=32,offset=0; auto* bound=vertex.Get(); context->IASetVertexBuffers(0,1,&bound,&stride,&offset);
        for(int repeat=0;repeat<3;++repeat) { skin.Update(palette.data(),32); actual=Read(device.Get(),context.Get(),vertex.Get()); Near(actual[0],9.25f); }
        palette[16]=palette[21]=palette[26]=1; palette[28]=0;
        skin.Update(palette.data(),32); actual=Read(device.Get(),context.Get(),vertex.Get());
        for(size_t i=0;i<source.size();++i) Near(actual[i],source[i]);
        bool rejected=false; palette[0]=std::numeric_limits<float>::quiet_NaN();
        try { skin.Update(palette.data(),32); } catch(const std::invalid_argument&) { rejected=true; } Require(rejected,"NaN palette accepted");
        palette[0]=0; rejected=false;
        try { skin.Update(palette.data(),32); } catch(const std::invalid_argument&) { rejected=true; } Require(rejected,"Singular normal matrix accepted");
        rejected=false; weights[0]=-1;
        try { helengine::windows::Win32GpuSkin invalid(vertex,2,joints.data(),weights.data(),8,2); } catch(const std::invalid_argument&) { rejected=true; } Require(rejected,"Negative weight accepted");
        weights[0]=.25f; rejected=false; joints[0]=2;
        try { helengine::windows::Win32GpuSkin invalid(vertex,2,joints.data(),weights.data(),8,2); } catch(const std::invalid_argument&) { rejected=true; } Require(rejected,"Out of range joint accepted");
        skin.Restore(); Require(vertex.Get()==original.Get(),"Original instance buffer not restored");
        ComPtr<ID3D11Buffer> restoredVertex; UINT restoredStride=0,restoredOffset=0;
        context->IAGetVertexBuffers(0,1,&restoredVertex,&restoredStride,&restoredOffset);
        Require(restoredVertex.Get()==original.Get() && restoredStride==32 && restoredOffset==0,"Destroy retained the compute output in input-assembler state");
        ComPtr<ID3D11ComputeShader> remainingShader; context->CSGetShader(&remainingShader,nullptr,nullptr);
        ComPtr<ID3D11ShaderResourceView> remainingInput; context->CSGetShaderResources(0,1,&remainingInput);
        ComPtr<ID3D11UnorderedAccessView> remainingOutput; context->CSGetUnorderedAccessViews(0,1,&remainingOutput);
        ComPtr<ID3D11Buffer> remainingConstants; context->CSGetConstantBuffers(0,1,&remainingConstants);
        Require(!remainingShader && !remainingInput && !remainingOutput && !remainingConstants,"Compute bindings survived the skin dispatch");
        for(UINT64 index=0;index<messages->GetNumStoredMessages();++index) {
            SIZE_T size=0; messages->GetMessage(index,nullptr,&size); std::vector<unsigned char> storage(size);
            auto* message=reinterpret_cast<D3D11_MESSAGE*>(storage.data()); messages->GetMessage(index,message,&size);
            if(message->Severity<=D3D11_MESSAGE_SEVERITY_WARNING) throw std::runtime_error(message->pDescription);
        }
        std::cout<<"PASS WARP GPU skin: identity, 2 bones, blended inverse-transpose normals, UVs, immutable bind pose, repeated dispatch, validation, resource restoration; no D3D11 warnings\n";
    }
};
/// <summary>Reports a failing GPU contract to the test runner.</summary>
int main() { try { GpuSkinTests::Run(); return 0; } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; } }



