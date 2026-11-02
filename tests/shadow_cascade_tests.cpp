#include "platform/windows/win32/win32_shadow_cascade_fitter.hpp"
#include "platform/windows/win32/win32_camera_toon_shader_source.hpp"
#include "platform/windows/win32/win32_pearl_surface_shader.hpp"
#include "platform/windows/win32/win32_shadow_shader_source.hpp"
#include "platform/windows/win32/win32_shadow_rasterizer_state.hpp"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>
using Microsoft::WRL::ComPtr;

/// <summary>Exercises fitted receiver coverage and the actual hard-shadow shader on owned WARP resources.</summary>
class ShadowCascadeTests {
    /// <summary>Supplies an independent high-precision numerical policy for the production fitter template.</summary>
    struct Math {
        /// <summary>Computes the reference square root.</summary>
        static double Sqrt(double value) {return std::sqrt(value);}
        /// <summary>Computes the reference absolute magnitude.</summary>
        static double Abs(double value) {return std::abs(value);}
        /// <summary>Computes the reference lower integer.</summary>
        static double Floor(double value) {return std::floor(value);}
        /// <summary>Computes the reference upper integer.</summary>
        static double Ceiling(double value) {return std::ceil(value);}
    };
    /// <summary>Uses the same fitting algorithm with independently checked numerical arithmetic.</summary>
    using Fitter=helengine::windows::Win32ShadowCascadeFitter<double,Math>;
    /// <summary>Stops immediately when a coverage or GPU contract fails.</summary>
    static void Require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
    /// <summary>Preserves the actual Direct3D failure and operation name.</summary>
    static void Check(HRESULT result,const char* message) {if(FAILED(result)) throw std::runtime_error(std::string(message)+": "+std::to_string(result));}
    /// <summary>Creates a world-aligned box using the production corner indexing.</summary>
    static Fitter::Geometry Bounds(Fitter::Point minimum,Fitter::Point maximum,unsigned layers=1) {
        Fitter::Geometry result{};result.Layers=layers;
        result.StructuralState={minimum[0],minimum[1],minimum[2],maximum[0],maximum[1],maximum[2],1,1,1,0,0,0};
        for(unsigned corner=0;corner<8;++corner) result.Corners[corner]={(corner&1)?maximum[0]:minimum[0],(corner&2)?maximum[1]:minimum[1],(corner&4)?maximum[2]:minimum[2]};
        return result;
    }
    /// <summary>Constructs a perspective camera with explicit original-output aspect and clipping distances.</summary>
    static Fitter::Camera Camera(double aspect=16.0/9.0,double farDepth=100,unsigned layers=1) {
        Fitter::Camera camera{};camera.Layers=layers;
        camera.Planes={Fitter::Plane{1,0,-aspect,0},Fitter::Plane{-1,0,-aspect,0},Fitter::Plane{0,1,-1,0},Fitter::Plane{0,-1,-1,0},Fitter::Plane{0,0,-1,-.1},Fitter::Plane{0,0,1,farDepth}};
        for(unsigned corner=0;corner<8;++corner) {
            const double depth=(corner&4)?farDepth:.1;
            camera.Corners[corner]={((corner&1)?1:-1)*depth*aspect,((corner&2)?1:-1)*depth,-depth};
        }
        return camera;
    }
    /// <summary>Checks every retained receiver against its fitted clip bounds, including depth.</summary>
    static void Coverage(const Fitter::Result& result) {
        for(const auto& cascade:result.Cascades) {
            for(double value:cascade.Matrix) Require(std::isfinite(value),"Nonfinite shadow matrix");
            for(const auto& point:cascade.Receivers) {
                const auto& m=cascade.Matrix;
                const double x=point[0]*m[0]+point[1]*m[4]+point[2]*m[8]+m[12];
                const double y=point[0]*m[1]+point[1]*m[5]+point[2]*m[9]+m[13];
                const double z=point[0]*m[2]+point[1]*m[6]+point[2]*m[10]+m[14];
                Require(std::abs(x)<=1.000001 && std::abs(y)<=1.000001 && z>=-.000001 && z<=1.000001,"Fitted cascade lost a receiver");
            }
        }
    }
    /// <summary>Checks room/cake depth, output union, rotations, texel snapping, clipping and caster depth.</summary>
    static void Numerical() {
        const Fitter::Plane depth{0,0,-1,0};
        std::vector<Fitter::Geometry> geometry{
            Bounds({-1.78,.98,-3.73},{1.78,1,-1.73}),Bounds({-1.78,-1,-3.73},{1.78,-.98,-1.73}),
            Bounds({-1.78,-1,-3.73},{-1.76,1,-1.73}),Bounds({1.76,-1,-3.73},{1.78,1,-1.73}),
            Bounds({-.25,-.25,-6.25},{.25,.25,-5.75},2),
            Bounds({-1.78,-1,-1.70},{1.78,1,-1.70})};
        const std::array<Fitter::Camera,2> cameras{Camera(16.0/9.0,100,1),Camera(16.0/9.0,100,2)};
        Fitter::Result previous{};
        auto result=Fitter::Fit(geometry,cameras,depth,.1,100,{.3,-1,.2},previous);Coverage(result);
        Require(result.Depths[1]>3.73 && result.Depths[2]>=6.25,"Shared slices omitted room depth or second-output cake");
        Require(result.Cascades[0].WorldTexel<32.0/1024/5,"Near cascade did not improve the old density by at least five times");
        Require(result.Cascades[1].Extent<2,"Far prop cascade wasted its scale on off-slice room walls");
        std::cout<<"density near="<<result.Cascades[0].WorldTexel<<" far="<<result.Cascades[1].WorldTexel<<" old="<<32.0/1024<<" improvement="<<32.0/result.Cascades[0].Extent<<"x splits="<<result.Depths[0]<<","<<result.Depths[1]<<","<<result.Depths[2]<<"\n";
        const auto initial=result;
        for(unsigned frame=0;frame<360;++frame) {
            const double angle=frame*3.141592653589793/180;
            auto rotated=geometry;
            for(auto& point:rotated[4].Corners) {
                const double x=point[0],z=point[2]+6;
                point[0]=x*std::cos(angle)+z*std::sin(angle);point[2]=-x*std::sin(angle)+z*std::cos(angle)-6;
            }
            result=Fitter::Fit(rotated,cameras,depth,.1,100,{.3,-1,.2},result);Coverage(result);
            Require(result.Cascades[0].Extent==initial.Cascades[0].Extent && result.Cascades[1].Extent==initial.Cascades[1].Extent,"Rotating cake changed cascade scale");
            Require(result.Depths==initial.Depths,"Rotating cake moved shared split depths");
        }
        for(const auto direction:{Fitter::Point{0,-1,0},Fitter::Point{0,-1,.000001},Fitter::Point{1,0,0},Fitter::Point{1,.000001,0}}) {
            auto fitted=Fitter::Fit(geometry,cameras,depth,.1,100,direction,{});Coverage(fitted);
            // Every enabled caster remains in both depth volumes, including the inward front closure.
            for(const auto& cascade:fitted.Cascades) for(const auto& bounds:geometry) for(const auto& point:bounds.Corners) {
                const auto& m=cascade.Matrix;const double z=point[0]*m[2]+point[1]*m[6]+point[2]*m[10]+m[14];
                Require(z>=0 && z<=1,"Off-slice upstream caster escaped shadow depth");
            }
        }
        for(double aspect:{.4,1.0,2.5}) for(double farDepth:{7.0,40.0}) {
            auto fitted=Fitter::Fit(geometry,{Camera(aspect,farDepth,1),Camera(aspect,farDepth,2)},depth,.1,farDepth,{.2,-1,.1},{});Coverage(fitted);
            Require(fitted.Depths[2]<=7,"Camera far clip wasted receiver depth coverage");
        }
        const auto& cascade=initial.Cascades[0];
        // The projected origin is constrained to integer texel increments.
        Require(std::abs(cascade.Matrix[12]*512-std::round(cascade.Matrix[12]*512))<1e-8,"Light-space X origin was not snapped to texels");
        Require(std::abs(cascade.Matrix[13]*512-std::round(cascade.Matrix[13]*512))<1e-8,"Light-space Y origin was not snapped to texels");
        auto outside=geometry;outside.push_back(Bounds({99,-1,-3},{100,1,-2},4));
        auto ignored=Fitter::Fit(outside,cameras,depth,.1,100,{.3,-1,.2},{});
        Require(ignored.Cascades[0].Extent==initial.Cascades[0].Extent,"Invisible output layers changed receiver scale");
        auto reduced=geometry;reduced.erase(reduced.begin()+4);
        const auto removed=Fitter::Fit(reduced,cameras,depth,.1,100,{.3,-1,.2},initial);Coverage(removed);
        const auto removedFresh=Fitter::Fit(reduced,cameras,depth,.1,100,{.3,-1,.2},{});
        Require(removed.Depths==removedFresh.Depths && removed.Depths[2]<initial.Depths[2],"Removed far geometry retained expanded depth slices");
        auto shrunk=geometry;
        for(auto& bounds:shrunk) {
            for(auto& point:bounds.Corners) point[0]*=.5;
            bounds.StructuralState[0]*=.5;bounds.StructuralState[3]*=.5;
        }
        auto roomShrink=Fitter::Fit(shrunk,cameras,depth,.1,100,{.3,-1,.2},initial);Coverage(roomShrink);
        Require(roomShrink.Cascades[0].Extent<initial.Cascades[0].Extent,"Shrinking structural room geometry retained old map scale");
        auto narrowCameras=std::array<Fitter::Camera,2>{Camera(.4,100,1),Camera(.4,100,2)};
        auto narrow=Fitter::Fit(geometry,narrowCameras,depth,.1,100,{.3,-1,.2},initial);Coverage(narrow);
        const auto freshNarrow=Fitter::Fit(geometry,narrowCameras,depth,.1,100,{.3,-1,.2},{});
        Require(narrow.Cascades[0].Extent==freshNarrow.Cascades[0].Extent,"Projection resize retained old stabilized extent");
        auto movedGeometry=geometry;
        for(auto& bounds:movedGeometry) for(auto& point:bounds.Corners) point[2]-=10;
        const Fitter::Plane movedDepth{0,0,-1,-10};
        auto movedCameras=cameras;
        for(auto& camera:movedCameras) {
            for(auto& corner:camera.Corners) corner[2]-=10;
            for(auto& plane:camera.Planes) plane[3]+=plane[2]*10;
        }
        auto moved=Fitter::Fit(movedGeometry,movedCameras,movedDepth,.1,100,{.3,-1,.2},initial);Coverage(moved);
        auto returned=Fitter::Fit(geometry,cameras,depth,.1,100,{.3,-1,.2},moved);Coverage(returned);
        Require(returned.Depths==initial.Depths && returned.Cascades[0].Extent==initial.Cascades[0].Extent,"Camera movement and return retained stale cascade coverage");
        const auto empty=Fitter::Fit({},cameras,depth,.1,100,{.3,-1,.2},initial);
        Require(!empty.HasReceivers,"Empty frame invented shadow receiver geometry");
        auto behind=geometry;
        for(auto& bounds:behind) for(auto& point:bounds.Corners) point[2]+=20;
        const auto hidden=Fitter::Fit(behind,cameras,depth,.1,100,{.3,-1,.2},initial);
        Require(!hidden.HasReceivers,"All-behind geometry invented shadow receivers");
        const auto afterEmpty=Fitter::Fit(geometry,cameras,depth,.1,100,{.3,-1,.2},empty);Coverage(afterEmpty);
        Require(afterEmpty.Depths==initial.Depths && afterEmpty.Cascades[0].Extent==initial.Cascades[0].Extent,"Returning from an empty frame retained stale stabilization");
        for(bool scaleDrift:{false,true}) {
            auto anchored=initial;anchored.Cascades[0].Extent=20;
            for(unsigned frame=1;frame<=40;++frame) {
                auto drifting=geometry;
                if(scaleDrift) {
                    drifting[0].StructuralState[6]=1+frame*.000001;
                    for(auto& point:drifting[0].Corners) point[0]*=drifting[0].StructuralState[6];
                } else {
                    drifting[0].StructuralState[9]=frame*.000001;
                    for(auto& point:drifting[0].Corners) point[0]+=drifting[0].StructuralState[9];
                }
                anchored=Fitter::Fit(drifting,cameras,depth,.1,100,{.3,-1,.2},anchored);Coverage(anchored);
                if(frame==5) Require(anchored.Cascades[0].Extent==20,"Sub-tolerance motion prematurely discarded stable scale");
            }
            Require(anchored.Cascades[0].Extent<20,"Accumulated tiny translation or scale drift never rebaselined stable coverage");
        }
        bool invalidCamera=false;
        try {Fitter::Fit({},cameras,depth,0,100,{.3,-1,.2},{});} catch(const std::invalid_argument&) {invalidCamera=true;}
        Require(invalidCamera,"Empty-frame handling masked invalid camera clipping planes");
        std::cout<<"PASS numerical: coverage, two-output union, 360 rotations, stable scale/splits, texel snapping, aspect/depth, vertical/horizon, upstream closure, camera/structural rebaseline and empty frames\n";
    }
    /// <summary>Compiles one test entry while preserving actual production HLSL errors.</summary>
    static ComPtr<ID3DBlob> Compile(const std::string& source,const char* entry,const char* profile) {
        ComPtr<ID3DBlob> code,error;
        const auto result=D3DCompile(source.data(),source.size(),"ShadowCascadeProduction",nullptr,nullptr,entry,profile,D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&error);
        if(FAILED(result)) throw std::runtime_error(error?static_cast<const char*>(error->GetBufferPointer()):"Shadow shader compilation failed");
        return code;
    }
    /// <summary>Uploads one explicitly sized constant buffer to the chosen pixel or vertex slot.</summary>
    static ComPtr<ID3D11Buffer> Buffer(ID3D11Device* device,ID3D11DeviceContext* context,const float* values,UINT bytes,UINT slot,bool vertex=false) {
        D3D11_BUFFER_DESC description{};description.ByteWidth=bytes;description.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA data{values,0,0};ComPtr<ID3D11Buffer> buffer;Check(device->CreateBuffer(&description,&data,&buffer),"Create shadow test constants");
        auto* binding=buffer.Get();if(vertex) context->VSSetConstantBuffers(slot,1,&binding);else context->PSSetConstantBuffers(slot,1,&binding);return buffer;
    }
    /// <summary>Rasterizes a closed half-ceiling and open half-window into both real 1024-square atlas tiles.</summary>
    static ComPtr<ID3D11ShaderResourceView> RasterAtlas(ID3D11Device* device,ID3D11DeviceContext* context) {
        D3D11_TEXTURE2D_DESC texture{};texture.Width=2048;texture.Height=1024;texture.MipLevels=texture.ArraySize=texture.SampleDesc.Count=1;texture.Format=DXGI_FORMAT_R32_TYPELESS;texture.BindFlags=D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> atlas;Check(device->CreateTexture2D(&texture,nullptr,&atlas),"Create cascade atlas");
        D3D11_DEPTH_STENCIL_VIEW_DESC depthDescription{};depthDescription.Format=DXGI_FORMAT_D32_FLOAT;depthDescription.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
        ComPtr<ID3D11DepthStencilView> depth;Check(device->CreateDepthStencilView(atlas.Get(),&depthDescription,&depth),"Create cascade depth view");
        context->OMSetRenderTargets(0,nullptr,depth.Get());context->ClearDepthStencilView(depth.Get(),D3D11_CLEAR_DEPTH,1,0);
        const std::string source="cbuffer TestDepth:register(b0){float4 Depth;} float4 VS(uint id:SV_VertexID):SV_POSITION {float2 p[6]={float2(-1,-1),float2(-1,1),float2(0,1),float2(-1,-1),float2(0,1),float2(0,-1)};return float4(p[id],Depth.x,1);}";
        auto code=Compile(source,"VS","vs_5_0");ComPtr<ID3D11VertexShader> vertex;Check(device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&vertex),"Create ceiling depth shader");
        D3D11_RASTERIZER_DESC rasterDescription{};rasterDescription.FillMode=D3D11_FILL_SOLID;rasterDescription.CullMode=D3D11_CULL_NONE;rasterDescription.DepthClipEnable=TRUE;
        ComPtr<ID3D11RasterizerState> raster;Check(device->CreateRasterizerState(&rasterDescription,&raster),"Create ceiling rasterizer");context->RSSetState(raster.Get());
        context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);context->VSSetShader(vertex.Get(),nullptr,0);context->PSSetShader(nullptr,nullptr,0);
        for(unsigned tile=0;tile<2;++tile) {
            const float values[4]={tile==0?.2f:.8f,0,0,0};auto constants=Buffer(device,context,values,16,0,true);
            D3D11_VIEWPORT viewport{static_cast<float>(tile*1024),0,1024,1024,0,1};context->RSSetViewports(1,&viewport);context->Draw(6,0);
        }
        context->OMSetRenderTargets(0,nullptr,nullptr);
        D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription{};viewDescription.Format=DXGI_FORMAT_R32_FLOAT;viewDescription.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;viewDescription.Texture2D.MipLevels=1;
        ComPtr<ID3D11ShaderResourceView> view;Check(device->CreateShaderResourceView(atlas.Get(),&viewDescription,&view),"Create cascade sampling view");return view;
    }
    /// <summary>Evaluates the actual production visibility function into owned floating-point pixels.</summary>
    static std::array<float,4> Shade(ID3D11Device* device,ID3D11DeviceContext* context,const std::string& expression) {
        const std::string source=std::string(helengine::windows::PearlSurfaceShader)+helengine::windows::CameraToonGeometryShader+"\nfloat4 TestVS(uint id:SV_VertexID):SV_POSITION {float2 p[3]={float2(-1,-1),float2(-1,3),float2(3,-1)};return float4(p[id],0,1); }\nfloat4 TestPS():SV_Target {return "+expression+";}";
        const auto vs=Compile(source,"TestVS","vs_5_0"),ps=Compile(source,"TestPS","ps_5_0");ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> pixel;
        Check(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex),"Create visibility VS");Check(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&pixel),"Create visibility PS");
        D3D11_TEXTURE2D_DESC description{};description.Width=description.Height=description.MipLevels=description.ArraySize=description.SampleDesc.Count=1;description.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;description.BindFlags=D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> target;Check(device->CreateTexture2D(&description,nullptr,&target),"Create visibility pixels");ComPtr<ID3D11RenderTargetView> view;Check(device->CreateRenderTargetView(target.Get(),nullptr,&view),"Create visibility RTV");
        auto* output=view.Get();context->OMSetRenderTargets(1,&output,nullptr);context->OMSetBlendState(nullptr,nullptr,0xffffffff);context->VSSetShader(vertex.Get(),nullptr,0);context->PSSetShader(pixel.Get(),nullptr,0);
        D3D11_VIEWPORT viewport{0,0,1,1,0,1};context->RSSetViewports(1,&viewport);context->Draw(3,0);context->OMSetRenderTargets(0,nullptr,nullptr);
        description.BindFlags=0;description.Usage=D3D11_USAGE_STAGING;description.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> readback;Check(device->CreateTexture2D(&description,nullptr,&readback),"Create visibility readback");context->CopyResource(readback.Get(),target.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};Check(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped),"Read visibility pixels");std::array<float,4> value{};std::memcpy(value.data(),mapped.pData,16);context->Unmap(readback.Get(),0);return value;
    }
    /// <summary>Checks real atlas sampling, hard ceiling/window edges, both cascades, splits and packed source slots.</summary>
    static void Gpu() {
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context),"Create WARP cascade device");
        auto atlas=RasterAtlas(device.Get(),context.Get());auto* binding=atlas.Get();context->PSSetShaderResources(1,1,&binding);
        std::array<float,76> transforms{};transforms[52]=1;auto transform=Buffer(device.Get(),context.Get(),transforms.data(),sizeof(transforms),0);
        std::array<float,40> toon{};toon[16]=1;auto style=Buffer(device.Get(),context.Get(),toon.data(),sizeof(toon),8);
        const float ink[16]={1};auto inkBuffer=Buffer(device.Get(),context.Get(),ink,sizeof(ink),13);
        for(unsigned packed=0;packed<4;++packed) {
            std::array<float,128> constants{};constants[0]=1;constants[1]=1.0f/2048;constants[2]=1.0f/1024;constants[3]=1;
            const unsigned slot=4+24*packed;constants[slot+2]=.5f;constants[slot+3]=1;constants[slot+4]=constants[slot+5]=constants[slot+6]=1;
            for(unsigned diagonal=0;diagonal<4;++diagonal) {constants[slot+8+diagonal*5]=1;constants[100+diagonal*5]=1;}
            constants[116]=constants[118]=.5f;constants[119]=1;constants[122]=1;constants[125]=.5f;constants[126]=1;constants[127]=2;
            auto shadows=Buffer(device.Get(),context.Get(),constants.data(),sizeof(constants),2);
            const auto index=std::to_string(packed);const auto args=",ShadowRect"+index+",ShadowSlot"+index+",ShadowMatrix"+index+")";
            auto values=Shade(device.Get(),context.Get(),"float4(ShadowVisibility(float3(-.5,0,.4)"+args+",ShadowVisibility(float3(.5,0,.4)"+args+",ShadowVisibility(float3(-.5,0,.9)"+args+",ShadowVisibility(float3(.5,0,.9)"+args+")");
            Require(values==std::array<float,4>{0,1,0,1},"Ceiling/window edge failed in a packed Sun/Moon slot or cascade");
            auto split=Shade(device.Get(),context.Get(),"float4(ShadowVisibility(float3(-.5,0,.49999)"+args+",ShadowVisibility(float3(-.5,0,.5)"+args+",ShadowVisibility(float3(-.5,0,.50001)"+args+",ShadowVisibility(float3(-1,0,.9)"+args+")");
            Require(split==std::array<float,4>{0,0,1,0},"Split selection or far-tile left boundary failed");
            auto other=Shade(device.Get(),context.Get(),"float4(ShadowVisibility(float3(-.5,0,.9),ShadowRect0,ShadowSlot0,ShadowMatrix0),ShadowVisibility(float3(-.5,0,.9),ShadowRect1,ShadowSlot1,ShadowMatrix1),ShadowVisibility(float3(-.5,0,.9),ShadowRect2,ShadowSlot2,ShadowMatrix2),ShadowVisibility(float3(-.5,0,.9),ShadowRect3,ShadowSlot3,ShadowMatrix3))");
            for(unsigned candidate=0;candidate<4;++candidate) Require(other[candidate]==(candidate==packed?0:1),"Cascade selection applied an unshadowed packed light's matrix");
            // Force the right border of the near tile onto an occluded texel; an unclamped integer load would read the far tile.
            constants[slot+8+3]=2; // Transposed row-vector translation shifts x=-1 onto near right edge.
            constants[125]=1; // Select the near tile at receiver depth .9; adjacent far depth .8 must remain occluded.
            context->UpdateSubresource(shadows.Get(),0,nullptr,constants.data(),0,0);
            auto border=Shade(device.Get(),context.Get(),"float4(ShadowVisibility(float3(-1,0,.9)"+args+",1,1,1)");
            Require(border[0]==1,"Near tile boundary read adjacent occluded far tile");
        }
        transforms[52]=0;context->UpdateSubresource(transform.Get(),0,nullptr,transforms.data(),0,0);
        auto excluded=Shade(device.Get(),context.Get(),"float4(ShadowVisibility(float3(-.5,0,.9),ShadowRect3,ShadowSlot3,ShadowMatrix3),1,1,1)");Require(excluded[0]==1,"Cascade shadows ignored material receiver eligibility");
        std::cout<<"PASS WARP: two rasterized 1024 tiles, ceiling/window hard edges, split boundaries, atlas borders, packed slots 0-3 and material eligibility\n";
    }
    /// <summary>Rejects false light along a closed angled caster junction, checks room self-shadowing and retains binary hard
    /// samples.</summary>
    static void Contact() {
        const auto legacy = helengine::windows::Win32ShadowRasterizerState::Describe(false),
                   inkState = helengine::windows::Win32ShadowRasterizerState::Describe(true);
        Require(legacy.DepthBias == 1000 && legacy.SlopeScaledDepthBias == 1 && legacy.CullMode == D3D11_CULL_NONE &&
                    legacy.FrontCounterClockwise && legacy.DepthClipEnable,
                "Legacy shadow rasterizer policy changed");
        Require(inkState.DepthBias == 0 && inkState.SlopeScaledDepthBias == 0,
                "Hard ink must compare precision bounds without displacing caster depth");
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context),
              "Device");
        for (double depthOffset : {0.0, 20.0}) {
            const double w = 16.0 / 9, d = 2, nearZ = -std::sqrt(3.0) - depthOffset, farZ = nearZ - d;
            std::vector<Fitter::Geometry> bounds{Bounds({-w, 1, farZ}, {w, 1, nearZ}),   Bounds({-w, -1, farZ}, {w, -1, nearZ}),
                                                 Bounds({-w, -1, farZ}, {-w, 1, nearZ}), Bounds({-w, -1, farZ}, {w, 1, farZ}),
                                                 Bounds({-w, -1, nearZ}, {w, 1, nearZ}), Bounds({-.1, -.1, -6.1}, {.1, .1, -5.9}, 2)};
            // Opaque rectangular window profiles are cubes, following the real normalized right-wall layout.
            for (unsigned part = 0; part < 6; ++part) {
                double zc, ys, zs, yc;
                if (part < 2) {
                    zc = part == 0 ? farZ + .008 : nearZ - .008;
                    zs = .016;
                    yc = 0;
                    ys = 2;
                } else if (part < 4) {
                    zc = (nearZ + farZ) / 2;
                    zs = 1.968;
                    yc = part == 2 ? .992 : -.992;
                    ys = .016;
                } else {
                    zc = farZ + .016 + (part - 3) * 1.968 / 3;
                    zs = .012;
                    yc = 0;
                    ys = 1.968;
                }
                bounds.push_back(Bounds({w - .036, yc - ys / 2, zc - zs / 2}, {w, yc + ys / 2, zc + zs / 2}));
            }
            for (double gap : {0.0, .0001, .001}) {
                const double peak = 1 - gap;
                for (bool contact : {false, true})
                    for (double lightY : {-.48, .48, .000001}) {
                        const auto light = Fitter::Normalize({-.84, lightY, -.25});
                        const auto fit =
                            Fitter::Fit(bounds, {Camera(16.0 / 9, 100, 1), Camera(16.0 / 9, 100, 2)}, {0, 0, -1, 0}, .1, 100, light, {});
                        Require(depthOffset == 0 ? -farZ <= fit.Depths[1] : -nearZ > fit.Depths[1],
                                "Receiver depth variation did not select the expected near/far cascade");
                        std::vector<float> vertices;
                        const unsigned faces[6][6] = {{0, 2, 6, 0, 6, 4}, {1, 5, 7, 1, 7, 3}, {0, 4, 5, 0, 5, 1},
                                                      {2, 3, 7, 2, 7, 6}, {0, 1, 3, 0, 3, 2}, {4, 6, 7, 4, 7, 5}};
                        for (unsigned box = 0; box < bounds.size(); ++box) {
                            // Real room surfaces are single quads; duplicate zero-thickness box sides are harmless in LESS_EQUAL.
                            for (const auto &face : faces)
                                for (unsigned corner : face)
                                    for (double c : bounds[box].Corners[corner])
                                        vertices.push_back(static_cast<float>(c));
                        }
                        if (contact) {
                            vertices.clear();
                            const Fitter::Point points[6] = {{-w, .5, nearZ}, {0, peak, nearZ}, {0, peak, farZ},
                                                             {-w, .5, farZ},  {w, .5, nearZ},   {w, .5, farZ}};
                            const unsigned roof[12] = {0, 1, 2, 0, 2, 3, 1, 4, 5, 1, 5, 2};
                            for (unsigned i : roof)
                                for (double component : points[i])
                                    vertices.push_back(static_cast<float>(component));
                        }
                        D3D11_BUFFER_DESC vb{};
                        vb.ByteWidth = static_cast<UINT>(vertices.size() * 4);
                        vb.BindFlags = D3D11_BIND_VERTEX_BUFFER;
                        D3D11_SUBRESOURCE_DATA vd{vertices.data()};
                        ComPtr<ID3D11Buffer> vertexBuffer;
                        Check(device->CreateBuffer(&vb, &vd, &vertexBuffer), "VB");
                        UINT stride = 12, offset = 0;
                        auto *vbBind = vertexBuffer.Get();
                        context->IASetVertexBuffers(0, 1, &vbBind, &stride, &offset);
                        auto depthCode = Compile(helengine::windows::ShadowVertexShaderSource, "VSMain", "vs_5_0");
                        ComPtr<ID3D11VertexShader> depthShader;
                        Check(device->CreateVertexShader(depthCode->GetBufferPointer(), depthCode->GetBufferSize(), nullptr, &depthShader),
                              "Depth VS");
                        D3D11_INPUT_ELEMENT_DESC input{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0};
                        ComPtr<ID3D11InputLayout> layout;
                        Check(device->CreateInputLayout(&input, 1, depthCode->GetBufferPointer(), depthCode->GetBufferSize(), &layout),
                              "Layout");
                        context->IASetInputLayout(layout.Get());
                        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                        context->VSSetShader(depthShader.Get(), nullptr, 0);
                        context->PSSetShader(nullptr, nullptr, 0);
                        D3D11_TEXTURE2D_DESC desc{};
                        desc.Width = 2048;
                        desc.Height = 1024;
                        desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
                        desc.Format = DXGI_FORMAT_R32_TYPELESS;
                        desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
                        ComPtr<ID3D11Texture2D> atlas;
                        Check(device->CreateTexture2D(&desc, nullptr, &atlas), "Atlas");
                        D3D11_DEPTH_STENCIL_VIEW_DESC dd{};
                        dd.Format = DXGI_FORMAT_D32_FLOAT;
                        dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
                        ComPtr<ID3D11DepthStencilView> depth;
                        Check(device->CreateDepthStencilView(atlas.Get(), &dd, &depth), "Depth");
                        context->OMSetRenderTargets(0, nullptr, depth.Get());
                        context->ClearDepthStencilView(depth.Get(), D3D11_CLEAR_DEPTH, 1, 0);
                        auto rd = helengine::windows::Win32ShadowRasterizerState::Describe(true);
                        ComPtr<ID3D11RasterizerState> raster;
                        Check(device->CreateRasterizerState(&rd, &raster), "Raster");
                        context->RSSetState(raster.Get());
                        std::array<float, 128> shadow{};
                        shadow[0] = shadow[3] = 1;
                        shadow[1] = 1.0f / 2048;
                        shadow[2] = 1.0f / 1024;
                        shadow[6] = .5f;
                        shadow[7] = 1;
                        shadow[8] = shadow[9] = shadow[10] = 1;
                        shadow[116] = shadow[118] = .5f;
                        shadow[119] = 1;
                        shadow[122] = -1;
                        shadow[124] = static_cast<float>(fit.Depths[0]);
                        shadow[125] = static_cast<float>(fit.Depths[1]);
                        shadow[126] = static_cast<float>(fit.Depths[2]);
                        shadow[127] = 2;
                        for (unsigned tile = 0; tile < 2; ++tile) {
                            std::array<float, 16> m{};
                            for (unsigned r = 0; r < 4; ++r)
                                for (unsigned c = 0; c < 4; ++c)
                                    m[r * 4 + c] = static_cast<float>(fit.Cascades[tile].Matrix[c * 4 + r]);
                            auto transform = Buffer(device.Get(), context.Get(), m.data(), 64, 0, true);
                            D3D11_VIEWPORT vp{static_cast<float>(tile * 1024), 0, 1024, 1024, 0, 1};
                            context->RSSetViewports(1, &vp);
                            context->Draw(static_cast<UINT>(vertices.size() / 3), 0);
                            std::copy(m.begin(), m.end(), shadow.begin() + (tile == 0 ? 12 : 100));
                        }
                        context->OMSetRenderTargets(0, nullptr, nullptr);
                        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
                        sd.Format = DXGI_FORMAT_R32_FLOAT;
                        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                        sd.Texture2D.MipLevels = 1;
                        ComPtr<ID3D11ShaderResourceView> srv;
                        Check(device->CreateShaderResourceView(atlas.Get(), &sd, &srv), "SRV");
                        auto *srvBind = srv.Get();
                        context->PSSetShaderResources(1, 1, &srvBind);
                        std::array<float, 76> transforms{};
                        transforms[52] = 1;
                        auto tb = Buffer(device.Get(), context.Get(), transforms.data(), sizeof(transforms), 0);
                        std::array<float, 40> toon{};
                        toon[16] = 1;
                        auto style = Buffer(device.Get(), context.Get(), toon.data(), sizeof(toon), 8);
                        const float ink[16] = {1};
                        auto ib = Buffer(device.Get(), context.Get(), ink, sizeof(ink), 13);
                        auto sb = Buffer(device.Get(), context.Get(), shadow.data(), sizeof(shadow), 2);
                        const auto degenerate = Shade(device.Get(), context.Get(),
                                                      "float4(HardShadowReceiverPlane(0,0,float4(0,0,.4,1),ShadowMatrix0,float4(0,0,.5,1),"
                                                      "float2(.2,.3),float2(.4,.6),float2(1.0/2048,1.0/1024)),"
                                                      "HardShadowReceiverPlane(float3(1,0,0),float3(1,1e-12,0),float4(0,0,.4,1),ShadowMatrix0,"
                                                      "float4(0,0,.5,1),float2(.2,.3),float2(.4,.6),float2(1.0/2048,1.0/1024)))");
                        Require(std::abs(degenerate[0] - .4f) < 1e-6 && std::abs(degenerate[2] - .4f) < 1e-6 && degenerate[1] > 0 &&
                                    degenerate[1] < .00001f && degenerate[3] > 0 && degenerate[3] < .00001f,
                                "Degenerate footprint lost finite depth or retained an excessive bias");
                        std::ostringstream expression;
                        expression << std::setprecision(17) << "float3 WorldPoint(float4 pixel){return float3((pixel.x/1024*2-1)*" << w << ",1,"
                                   << nearZ << "-pixel.y/512*2);}";
                        std::string shader =
                            std::string(helengine::windows::PearlSurfaceShader) + helengine::windows::CameraToonGeometryShader +
                            expression.str() +
                            "float4 ProbeVS(uint id:SV_VertexID):SV_POSITION{float2 "
                            "p[3]={float2(-1,-1),float2(-1,3),float2(3,-1)};return float4(p[id],0,1);}float4 ProbePS(float4 "
                            "p:SV_POSITION):SV_Target{return ShadowVisibility(WorldPoint(p),ShadowRect0,ShadowSlot0,ShadowMatrix0);}";

                        auto vs = Compile(shader, "ProbeVS", "vs_5_0"), ps = Compile(shader, "ProbePS", "ps_5_0");
                        ComPtr<ID3D11VertexShader> v;
                        ComPtr<ID3D11PixelShader> p;
                        Check(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &v), "VS");
                        Check(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &p), "PS");
                        context->IASetInputLayout(nullptr);
                        context->VSSetShader(v.Get(), nullptr, 0);
                        context->PSSetShader(p.Get(), nullptr, 0);
                        desc.Width = 1024;
                        desc.Height = 512;
                        desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
                        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
                        ComPtr<ID3D11Texture2D> target;
                        Check(device->CreateTexture2D(&desc, nullptr, &target), "Target");
                        ComPtr<ID3D11RenderTargetView> rtv;
                        Check(device->CreateRenderTargetView(target.Get(), nullptr, &rtv), "RTV");
                        auto *output = rtv.Get();
                        context->OMSetRenderTargets(1, &output, nullptr);
                        D3D11_VIEWPORT vp{0, 0, 1024, 512, 0, 1};
                        context->RSSetViewports(1, &vp);
                        context->Draw(3, 0);
                        context->OMSetRenderTargets(0, nullptr, nullptr);
                        desc.BindFlags = 0;
                        desc.Usage = D3D11_USAGE_STAGING;
                        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                        ComPtr<ID3D11Texture2D> read;
                        Check(device->CreateTexture2D(&desc, nullptr, &read), "Read");
                        context->CopyResource(read.Get(), target.Get());
                        D3D11_MAPPED_SUBRESOURCE mapped{};
                        Check(context->Map(read.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map");
                        std::vector<unsigned> mask(1024 * 512);
                        unsigned falseLit = 0, truthLit = 0, falseShadow = 0, interior = 0;
                        unsigned xmin = 1024, xmax = 0, ymin = 512, ymax = 0;
                        for (unsigned y = 0; y < 512; ++y)
                            for (unsigned x = 0; x < 1024; ++x) {
                                Fitter::Point point{((x + .5) / 1024 * 2 - 1) * w, 1, nearZ - (y + .5) / 512 * 2};
                                bool occluded = false;
                                if (contact) {
                                    for (double sign : {-1.0, 1.0}) {
                                        double k = -sign * (peak - .5) / w;
                                        double rayY = -light[1], rayX = -light[0], rayZ = -light[2];
                                        double t = (peak + k * point[0] - point[1]) / (rayY - k * rayX);
                                        double cx = point[0] + rayX * t, cz = point[2] + rayZ * t;
                                        if (t > 0 && cx * sign >= 0 && std::abs(cx) <= w && cz >= farZ && cz <= nearZ)
                                            occluded = true;
                                    }
                                } else {
                                    for (unsigned b = 1; b < bounds.size(); ++b) {
                                        double low = 1e-5, high = 1e10;
                                        for (unsigned axis = 0; axis < 3; ++axis) {
                                            double a = (bounds[b].Corners[0][axis] - point[axis]) / -light[axis],
                                                   z = (bounds[b].Corners[7][axis] - point[axis]) / -light[axis];
                                            if (a > z)
                                                std::swap(a, z);
                                            low = std::max(low, a);
                                            high = std::min(high, z);
                                        }
                                        if (low <= high) {
                                            occluded = true;
                                            break;
                                        }
                                    }
                                }
                                float value =
                                    reinterpret_cast<const float *>(static_cast<const char *>(mapped.pData) + y * mapped.RowPitch)[x * 4];
                                mask[y * 1024 + x] = (value > .5f ? 1 : 0) + (occluded ? 2 : 0);
                                Require(value == 0 || value == 1, "Hard shadow contact produced a filtered or nonfinite sample");
                                if (!occluded)
                                    ++truthLit;
                                if (occluded && value > .5f) {
                                    ++falseLit;
                                    xmin = std::min(xmin, x);
                                    xmax = std::max(xmax, x);
                                    ymin = std::min(ymin, y);
                                    ymax = std::max(ymax, y);
                                    if (x > 10 && x < 1014 && y > 10 && y < 502)
                                        ++interior;
                                }
                                if (!occluded && value < .5f)
                                    ++falseShadow;
                            }
                        context->Unmap(read.Get(), 0);
                        // This interior strip crosses the projected closed roof junction, far from every caster silhouette.
                        // Its two bracket pixels must both be physically occluded and GPU-shadowed before testing the strip.
                        if (contact && lightY == .48) {
                            for (unsigned y = 20; y < 492; ++y) {
                                Require(mask[y * 1024 + 505] == 2 && mask[y * 1024 + 518] == 2,
                                        "Closed junction regression lost its two shadowed brackets");
                            }
                        }
                        unsigned seamLeaks = 0;
                        for (unsigned y = 20; y < 492; ++y)
                            for (unsigned x = 509; x < 515; ++x)
                                if (mask[y * 1024 + x] == 3)
                                    ++seamLeaks;
                        std::cout << "depthOffset=" << depthOffset << " gap=" << gap << " seamLeaks=" << seamLeaks << " lightY=" << lightY
                                  << " contact=" << contact << " extent=" << fit.Cascades[0].Extent << " truthLit=" << truthLit
                                  << " falseLit=" << falseLit << " interiorFalseLit=" << interior << " falseShadow=" << falseShadow
                                  << " bbox=" << xmin << "," << ymin << ".." << xmax << "," << ymax << "\n";
                        if (contact && lightY == .48)
                            Require(seamLeaks == 0, "Closed angled caster junction leaked light between shadow regions");
                        if (!contact && lightY < 0)
                            Require(falseShadow == 0, "Room ceiling developed self-shadow acne");
                        ID3D11ShaderResourceView *nullView = nullptr;
                        context->PSSetShaderResources(1, 1, &nullView);
                    }
            }
        }
    }
public:
    /// <summary>Runs the smallest numerical and offscreen GPU checks for the new shadow contract.</summary>
    static void Run() {Numerical();Gpu();Contact();}
};
/// <summary>Returns the first real cascade regression failure without desktop capture or input.</summary>
int main() {try {ShadowCascadeTests::Run();return 0;} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}}
