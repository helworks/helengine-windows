#include "platform/windows/directx11/directx11_constant_data_pool.hpp"
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <cstring>
#include <iostream>
using namespace helengine::windows;
using Microsoft::WRL::ComPtr;
/// <summary>Exercises immutable D3D11 content sharing and actual readback on the Windows WARP device without opening a window.</summary>
class MaterialCacheGpuTests {
    /// <summary>Converts failed Direct3D calls to a test failure with the operation context.</summary>
    static void Check(HRESULT result,const char* operation){if(FAILED(result))throw std::runtime_error(operation);}
    /// <summary>Creates the same immutable constant-buffer contract consumed by the Windows material bridge.</summary>
    static ComPtr<ID3D11Buffer> Create(ID3D11Device* device,std::span<const uint8_t> bytes){
        D3D11_BUFFER_DESC description{};description.ByteWidth=static_cast<UINT>(bytes.size());description.Usage=D3D11_USAGE_IMMUTABLE;description.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA initial{};initial.pSysMem=bytes.data();ComPtr<ID3D11Buffer> buffer;
        Check(device->CreateBuffer(&description,&initial,buffer.GetAddressOf()),"Create immutable constant buffer");return buffer;
    }
    /// <summary>Compares actual GPU contents using a staging copy; no screenshots or image files are produced.</summary>
    static void Verify(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Buffer* source,std::span<const uint8_t> bytes){
        D3D11_BUFFER_DESC description{};source->GetDesc(&description);description.Usage=D3D11_USAGE_STAGING;description.BindFlags=0;description.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Buffer> staging;Check(device->CreateBuffer(&description,nullptr,staging.GetAddressOf()),"Create staging buffer");context->CopyResource(staging.Get(),source);
        D3D11_MAPPED_SUBRESOURCE mapped{};Check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Map staging buffer");
        const bool equal=std::memcmp(mapped.pData,bytes.data(),bytes.size())==0;context->Unmap(staging.Get(),0);
        if(!equal)throw std::runtime_error("Cached constant buffer readback is stale");
    }
public:
    /// <summary>Checks identical-resource reuse and changed contents on a real Windows Direct3D implementation.</summary>
    static void Run(){
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
        Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,device.GetAddressOf(),nullptr,context.GetAddressOf()),"Create WARP device");
        DirectX11ConstantDataPool<ComPtr<ID3D11Buffer>> pool;std::array<uint8_t,16> bytes{};size_t creates=0;
        auto first=pool.Get(bytes,[&]{++creates;return Create(device.Get(),bytes);});
        auto same=pool.Get(bytes,[&]{++creates;return Create(device.Get(),bytes);});
        if(first!=same || creates!=1)throw std::runtime_error("Unchanged material reuploaded constants");
        Verify(device.Get(),context.Get(),first->Get(),bytes);
        bytes[4]=71;auto changed=pool.Get(bytes,[&]{++creates;return Create(device.Get(),bytes);});
        Verify(device.Get(),context.Get(),changed->Get(),bytes);
        bytes[4]=0;Verify(device.Get(),context.Get(),first->Get(),bytes);
        first.reset();same.reset();if(pool.GetCount()!=1)throw std::runtime_error("Unused constant generation retained");
        changed.reset();if(pool.GetCount()!=0)throw std::runtime_error("Last material owner retained GPU cache content");
    }
};
/// <summary>Runs only on Windows and reports actual D3D11 execution rather than a simulated Linux pass.</summary>
int main(){try{MaterialCacheGpuTests::Run();std::cout<<"PASS native DirectX11 WARP immutable constants sharing and readback\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
