#include "platform/windows/win32/win32_camera_toon_pass.hpp"
#include "platform/windows/win32/win32_pearl_surface_shader.hpp"
#include "platform/windows/win32/win32_camera_toon_shader_source.hpp"
#ifdef HELENGINE_VOXEL_GI
#include "platform/windows/win32/win32_scene_gi_shader.hpp"
#endif

#include <d3dcompiler.h>
#include "platform/windows/directx11/directx11_aces_sdr_lut.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <mutex>

namespace helengine::windows {
    namespace {
        /// Protects scratch ownership when independent devices prepare or release cameras concurrently.
        std::mutex SharedCameraTargetsMutex;
        /// Retains no GPU memory by itself; cameras own live scratch sets and expired device entries are pruned.
        std::unordered_map<ID3D11Device*,std::weak_ptr<Win32CameraToonTargets>> SharedCameraTargets;

        /// Keeps all GPU-facing data explicitly IEEE32 and aligned to Direct3D constant-buffer registers.
        struct ToonParameters {
            /// Stores outside radius, inside radius, band count and the exactly representable camera-local part identifier.
            float Style[4];
            /// Stores local texture width and height followed by reserved padding.
            float Size[4];
            /// Stores reflection strength, bloom strength, emission threshold and physical blur radius.
            float Effects[4];
            /// Stores exact material weights, maximum environment LOD and the quantization toggle.
            float Material[4];
            /// Stores independent shadow strength and bounded PCF softness.
            float Shadow[4];
            /// Stores horizontal and vertical blur steps in normalized texture coordinates.
            float Blur[4];
            /// Stores pearl angular strength, highlight strength and broadness, followed by independent material metallic weight.
            float Pearl[4];
            /// Stores the tone-mapping switch and linear exposure multiplier.
            float Display[4];
            /// Stores world radius, coplanar bias, pixel radius and diffuse indirect occlusion strength.
            float Occlusion[4];
            /// Stores current toon eligibility, contour width scale and the largest registered scale.
            float SurfaceStyle[4];
            /// Stores the sharp face-normal cosine threshold and the independent crease-contour switch.
            float Crease[4];
            /// Stores perspective near/far planes for the depth-outline reconstruction.
            float DepthProjection[4];
            /// Stores optional selection RGB/radius and its bounded composite rectangle.
            float SelectionColor[4];
            float SelectionBounds[4];
        };

    }

    void Win32CameraToonPass::Check(HRESULT result, const char* operation) {
        if (FAILED(result)) throw std::runtime_error(std::string(operation) + " failed (HRESULT " + std::to_string(result) + ").");
    }

    Win32CameraToonPass::Win32CameraToonPass(ID3D11Device* device, float outsidePixels, float insidePixels, int bands) : Device(device) {
        if(device==nullptr) throw std::invalid_argument("Camera toon requires a graphics device.");
        Configure(outsidePixels,insidePixels,bands);
        std::string source=std::string(PearlSurfaceShader);
#ifdef HELENGINE_VOXEL_GI
        source+="#define HELENGINE_VOXEL_GI 1\n";
        source+=SceneGiShader;
#endif
#ifdef HELENGINE_LIGHTMAP
        source+="#define HELENGINE_LIGHTMAP 1\n";
#endif
        source+=CameraToonGeometryShader;
        source+=CameraToonCompositeShader;
        ShaderCompilation=std::make_unique<DirectX11ShaderCompilation>(BuildCameraToonShaderRequests(source));
    }

    Win32CameraToonPass::~Win32CameraToonPass() {
        ReleaseTargets();
    }

    bool Win32CameraToonPass::TryPrepare() {
        if(PipelineFailure) std::rethrow_exception(PipelineFailure);
        if(PipelinePrepared) return true;
        try {
            DirectX11ShaderCompilation::Bytecodes bytecodes;
            if(!ShaderCompilation->TryGetBytecodes(bytecodes)) return false;
            InitializePipeline(bytecodes);
            PipelinePrepared=true;
            ShaderCompilation.reset();
            return true;
        } catch(...) { PipelineFailure=std::current_exception(); throw; }
    }

    void Win32CameraToonPass::InitializePipeline(const DirectX11ShaderCompilation::Bytecodes& bytecodes) {
        const auto& geometryVS=bytecodes[0]; const auto& geometryPS=bytecodes[1]; const auto& pearlPS=bytecodes[2];
        const auto& compositeVS=bytecodes[3]; const auto& compositePS=bytecodes[4];
        const auto& compositeMSPS=bytecodes[5]; const auto& blurPS=bytecodes[6];
        const auto& tilePS=bytecodes[7]; const auto& tileMSPS=bytecodes[8];
        Check(Device->CreateVertexShader(geometryVS->GetBufferPointer(), geometryVS->GetBufferSize(), nullptr, &GeometryVertexShader), "Create toon geometry vertex shader");
        Check(Device->CreatePixelShader(geometryPS->GetBufferPointer(), geometryPS->GetBufferSize(), nullptr, &GeometryPixelShader), "Create toon geometry pixel shader");
        Check(Device->CreatePixelShader(pearlPS->GetBufferPointer(), pearlPS->GetBufferSize(), nullptr, &PearlGeometryPixelShader), "Create pearl geometry pixel shader");
        Check(Device->CreateVertexShader(compositeVS->GetBufferPointer(), compositeVS->GetBufferSize(), nullptr, &CompositeVertexShader), "Create toon composition vertex shader");
        Check(Device->CreatePixelShader(compositePS->GetBufferPointer(), compositePS->GetBufferSize(), nullptr, &CompositePixelShader), "Create toon composition pixel shader");
        Check(Device->CreatePixelShader(compositeMSPS->GetBufferPointer(), compositeMSPS->GetBufferSize(), nullptr, &CompositeMultisamplePixelShader), "Create multisampled toon composition pixel shader");
        Check(Device->CreatePixelShader(blurPS->GetBufferPointer(), blurPS->GetBufferSize(), nullptr, &BlurPixelShader), "Create selected-emission blur shader");
        Check(Device->CreatePixelShader(tilePS->GetBufferPointer(),tilePS->GetBufferSize(),nullptr,&OutlineTilePixelShader),"Create contour tile shader");
        Check(Device->CreatePixelShader(tileMSPS->GetBufferPointer(),tileMSPS->GetBufferSize(),nullptr,&OutlineTileMultisamplePixelShader),"Create multisampled contour tile shader");
#ifdef HELENGINE_LIGHTMAP
        Check(Device->CreatePixelShader(bytecodes[9]->GetBufferPointer(),bytecodes[9]->GetBufferSize(),nullptr,&LightmapPixelShader),"Create baked diffuse pixel shader");
        Check(Device->CreatePixelShader(bytecodes[10]->GetBufferPointer(),bytecodes[10]->GetBufferSize(),nullptr,&LightmapPearlPixelShader),"Create baked pearl pixel shader");
#endif
        Check(Device->CreatePixelShader(bytecodes[bytecodes.size()-7]->GetBufferPointer(),bytecodes[bytecodes.size()-7]->GetBufferSize(),nullptr,&OutlineOnlyPixelShader),"Create outline-only geometry shader");
        const auto transparentStart=bytecodes.size()-6;
        Check(Device->CreatePixelShader(bytecodes[transparentStart]->GetBufferPointer(),bytecodes[transparentStart]->GetBufferSize(),nullptr,&AuxiliaryOutlinePixelShader),"Create auxiliary outline shader");
        Check(Device->CreatePixelShader(bytecodes[transparentStart+1]->GetBufferPointer(),bytecodes[transparentStart+1]->GetBufferSize(),nullptr,&AuxiliaryOutlineMultisamplePixelShader),"Create multisample auxiliary outline shader");
        Check(Device->CreatePixelShader(bytecodes[transparentStart+2]->GetBufferPointer(),bytecodes[transparentStart+2]->GetBufferSize(),nullptr,&AuxiliaryTilePixelShader),"Create transparent tile shader");
        Check(Device->CreatePixelShader(bytecodes[transparentStart+3]->GetBufferPointer(),bytecodes[transparentStart+3]->GetBufferSize(),nullptr,&AuxiliaryTileMultisamplePixelShader),"Create multisample transparent tile shader");
        const auto& gtao=bytecodes[bytecodes.size()-2]; const auto& gtaoFilter=bytecodes.back();
        Check(Device->CreatePixelShader(gtao->GetBufferPointer(),gtao->GetBufferSize(),nullptr,&GtaoPixelShader),"Create GTAO horizon shader");
        Check(Device->CreatePixelShader(gtaoFilter->GetBufferPointer(),gtaoFilter->GetBufferSize(),nullptr,&GtaoFilterPixelShader),"Create GTAO spatial filter");
        D3D11_SAMPLER_DESC sampler {};
        sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD=D3D11_FLOAT32_MAX;
        Check(Device->CreateSamplerState(&sampler,&EffectSampler),"Create camera effect sampler");
        AcesSdrView=DirectX11AcesSdrLut::Create(Device.Get());
        D3D11_INPUT_ELEMENT_DESC elements[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 }

        };
        Check(Device->CreateInputLayout(elements, static_cast<UINT>(std::size(elements)), geometryVS->GetBufferPointer(), geometryVS->GetBufferSize(), &InputLayout), "Create toon mesh input layout");
#ifdef HELENGINE_LIGHTMAP
        const auto& lightmapVS=bytecodes[11];
        Check(Device->CreateVertexShader(lightmapVS->GetBufferPointer(),lightmapVS->GetBufferSize(),nullptr,&LightmapVertexShader),"Create baked geometry vertex shader");
        D3D11_INPUT_ELEMENT_DESC bakedElements[]={elements[0],elements[1],elements[2],{"TEXCOORD",1,DXGI_FORMAT_R32G32_FLOAT,1,0,D3D11_INPUT_PER_VERTEX_DATA,0}};
        Check(Device->CreateInputLayout(bakedElements,4,lightmapVS->GetBufferPointer(),lightmapVS->GetBufferSize(),&LightmapInputLayout),"Create baked geometry input layout");
#endif
        D3D11_BUFFER_DESC buffer {};
        buffer.ByteWidth = sizeof(ToonParameters); buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        Check(Device->CreateBuffer(&buffer, nullptr, &Parameters), "Create toon parameters");
        D3D11_BLEND_DESC blend {};
        blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        Check(Device->CreateBlendState(&blend, &GeometryBlend), "Create opaque toon geometry blend");
        blend.RenderTarget[0].BlendEnable = TRUE;
        blend.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE; blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        Check(Device->CreateBlendState(&blend, &CompositeBlend), "Create toon composition blend");
        D3D11_DEPTH_STENCIL_DESC depth {};
        depth.DepthEnable = FALSE;
        Check(Device->CreateDepthStencilState(&depth, &CompositeDepth), "Create toon composition depth state");
        D3D11_RASTERIZER_DESC raster {};
        raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = D3D11_CULL_NONE; raster.DepthClipEnable = TRUE; raster.MultisampleEnable = TRUE;
        Check(Device->CreateRasterizerState(&raster, &CompositeRasterizer), "Create toon composition rasterizer");
    }

    void Win32CameraToonPass::Configure(float outsidePixels, float insidePixels, int bands) {
        if (!std::isfinite(outsidePixels) || outsidePixels < 0.0f || outsidePixels > 16.0f
            || !std::isfinite(insidePixels) || insidePixels < 0.0f || insidePixels > 16.0f || bands < 2 || bands > 16) {
            throw std::invalid_argument("Camera toon requires finite outline widths from 0 to 16 pixels and 2 to 16 bands.");
        }
        OutsidePixels = outsidePixels; InsidePixels = insidePixels; Bands = bands;
    }

    void Win32CameraToonPass::ConfigureEffects(const CameraToonEffectsConfiguration& configuration) {
        const float values[] {configuration.ReflectionStrength,configuration.BloomStrength,configuration.BloomThreshold,configuration.BloomRadius,configuration.ShadowStrength,configuration.ShadowSoftness,configuration.PearlStrength,configuration.PearlHighlight,configuration.PearlWidth,configuration.Exposure,configuration.AmbientOcclusionStrength,configuration.AmbientOcclusionRadius,configuration.AmbientOcclusionBias,configuration.AmbientOcclusionPixelRadius,configuration.CreaseAngleDegrees,configuration.SdrExposure};
        const float maxima[] {1,2,4,24,1,3,1,1,1,16,1,16,1,64,180,16};
        for(std::size_t index=0;index<std::size(values);++index) {
            if(!std::isfinite(values[index]) || values[index]<0 || values[index]>maxima[index]) throw std::invalid_argument("Camera effects require finite values within documented bounds.");
        }
        Effects=configuration;
    }

    void Win32CameraToonPass::SetEnvironment(ID3D11DeviceContext* context, uint32_t width, uint32_t height, const uint8_t* rgbaBytes, std::size_t byteCount) {
        if(context==nullptr || rgbaBytes==nullptr || width==0 || height==0 || width>4096 || height>4096
            || byteCount!=static_cast<std::size_t>(width)*height*4) throw std::invalid_argument("Camera environment requires a context and exact positive RGBA dimensions up to 4096 pixels.");
        D3D11_TEXTURE2D_DESC descriptor {};
        descriptor.Width=width; descriptor.Height=height; descriptor.MipLevels=0; descriptor.ArraySize=1;
        descriptor.Format=DXGI_FORMAT_R8G8B8A8_UNORM; descriptor.SampleDesc.Count=1;
        descriptor.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE; descriptor.MiscFlags=D3D11_RESOURCE_MISC_GENERATE_MIPS;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
        Check(Device->CreateTexture2D(&descriptor,nullptr,&texture),"Create camera environment texture");
        Check(Device->CreateShaderResourceView(texture.Get(),nullptr,&view),"Create camera environment view");
        context->UpdateSubresource(texture.Get(),0,nullptr,rgbaBytes,width*4,0);
        context->GenerateMips(view.Get());
        texture->GetDesc(&descriptor); EnvironmentMaximumLod=static_cast<float>(descriptor.MipLevels-1);
        EnvironmentView=view;
    }

    void Win32CameraToonPass::UploadParameters(ID3D11DeviceContext* context, float reflectivity, float bloomWeight, float blurX, float blurY, bool rawColor) {
        ToonParameters parameters {
            {OutsidePixels,InsidePixels,static_cast<float>(Bands),static_cast<float>(PartIdentifier)},
            {static_cast<float>(Width),static_cast<float>(Height),ViewportX,ViewportY},
            {Effects.ReflectionsEnabled && EnvironmentView ? Effects.ReflectionStrength : 0.0f,Effects.BloomEnabled ? Effects.BloomStrength : 0.0f,Effects.BloomThreshold,Effects.BloomRadius},
            {reflectivity,bloomWeight,EnvironmentMaximumLod,Effects.ToonEnabled || MaterialToonEnabled ? 1.0f : 0.0f},
            {Effects.ShadowsEnabled ? Effects.ShadowStrength : 0.0f,Effects.ShadowSoftness,0,0},
            {blurX,blurY,0,0},
            {Effects.PearlStrength,Effects.PearlHighlight,Effects.PearlWidth,MaterialMetallic},
            {Effects.ToneMappingEnabled ? 1.0f : 0.0f,Effects.OutputExposure(HdrOutput),HdrOutput ? 1.0f : 0.0f,0},
            {Effects.AmbientOcclusionRadius,Effects.AmbientOcclusionBias,Effects.AmbientOcclusionPixelRadius,Effects.AmbientOcclusionEnabled && !rawColor ? Effects.AmbientOcclusionStrength : 0.0f},
            {Effects.ToonEnabled || MaterialToonEnabled ? 1.0f : 0.0f,MaterialOutlineScale,MaximumOutlineScale,OutlinedGeometrySubmitted ? 1.0f : 0.0f},
            {static_cast<float>(std::cos(Effects.CreaseAngleDegrees*0.017453292519943295)),Effects.CreaseAngleDegrees<180.0f ? 1.0f : 0.0f,0,0},
            {DepthNear,DepthFar,0,0},
            {SelectionColor[0],SelectionColor[1],SelectionColor[2],SelectionColor[3]},
            {SelectionBounds[0],SelectionBounds[1],SelectionBounds[2],SelectionBounds[3]}
        };
        context->UpdateSubresource(Parameters.Get(),0,nullptr,&parameters,0,0);
    }

    void Win32CameraToonPass::ConfigureSelection(float red,float green,float blue,float radius,float left,float top,float right,float bottom) {
        if(!std::isfinite(red)||!std::isfinite(green)||!std::isfinite(blue)||red<0||red>1||green<0||green>1||blue<0||blue>1
            ||!std::isfinite(radius)||radius<1||radius>64||!std::isfinite(left)||!std::isfinite(top)||!std::isfinite(right)||!std::isfinite(bottom)
            ||right<=left||bottom<=top) throw std::invalid_argument("Selection requires finite RGB, radius 1..64 and ordered pixel bounds.");
        SelectionColor[0]=red;SelectionColor[1]=green;SelectionColor[2]=blue;SelectionColor[3]=radius;
        SelectionBounds[0]=left;SelectionBounds[1]=top;SelectionBounds[2]=right;SelectionBounds[3]=bottom;
        CameraToonEffectsConfiguration effects{};
        effects.BloomEnabled=false;effects.ReflectionsEnabled=false;effects.ShadowsEnabled=false;
        effects.AmbientOcclusionEnabled=false;effects.PearlEnabled=false;effects.ToonEnabled=false;
        ConfigureEffects(effects);
    }

    void Win32CameraToonPass::ConfigureMaterialEffects(const void* material, float reflectivity, float bloomWeight, bool pearlSurface, bool toonSurface, float outlineScale, float metallic, bool outlineOnly) {
        if(material==nullptr || !std::isfinite(reflectivity) || reflectivity<0 || reflectivity>1 || !std::isfinite(bloomWeight) || bloomWeight<0 || bloomWeight>1) throw std::invalid_argument("Camera material effects require an exact identity and finite zero-to-one weights.");
        if(!std::isfinite(outlineScale) || outlineScale<0 || outlineScale>4) throw std::invalid_argument("Material outline scale must be finite from zero to four.");
        if(!std::isfinite(metallic) || metallic<0 || metallic>1) throw std::invalid_argument("Material metallic weight must be finite from zero to one.");
        MaterialEffects[material]={reflectivity,bloomWeight,pearlSurface ? 1.0f : 0.0f,toonSurface ? 1.0f : 0.0f,outlineScale,metallic,outlineOnly ? 1.0f : 0.0f};
        MaximumOutlineScale=1.0f;
        for(const auto& entry:MaterialEffects) MaximumOutlineScale=std::max(MaximumOutlineScale,entry.second[4]);
    }

    float Win32CameraToonPass::GetMaterialMetallic(const void* material) const {
        const auto found=MaterialEffects.find(material);return found==MaterialEffects.end()?0.0f:found->second[5];
    }

    bool Win32CameraToonPass::WritesAuxiliaryContours(const void* material) const {
        const auto found=MaterialEffects.find(material);
        return found!=MaterialEffects.end() && found->second[6]>0.5f && found->second[4]>0 && (Effects.ToonEnabled || found->second[3]>0.5f);
    }

    void Win32CameraToonPass::RemoveMaterialEffects(const void* material) {
        MaterialEffects.erase(material);
        MaximumOutlineScale=1.0f;
        for(const auto& entry:MaterialEffects) MaximumOutlineScale=std::max(MaximumOutlineScale,entry.second[4]);
    }

    /// <summary>Reads exact material eligibility while suppressing recursive reflections, contours, bloom and camera-state writes.</summary>
    void Win32CameraToonPass::BindCaptureGeometry(ID3D11DeviceContext* context,const void* material) {
        const auto found=MaterialEffects.find(material);
        ToonParameters parameters{{0,0,3,0},{},{},{0,0,0,1},{Effects.ShadowsEnabled?Effects.ShadowStrength:0.0f,0,0,0},{},{0,0,0,found==MaterialEffects.end()?0.0f:found->second[5]}};
        context->UpdateSubresource(Parameters.Get(),0,nullptr,&parameters,0,0);
        context->IASetInputLayout(InputLayout.Get());context->VSSetShader(GeometryVertexShader.Get(),nullptr,0);context->PSSetShader(found!=MaterialEffects.end() && found->second[6]>0.5f ? OutlineOnlyPixelShader.Get() : GeometryPixelShader.Get(),nullptr,0);
        auto* buffer=Parameters.Get();context->PSSetConstantBuffers(8,1,&buffer);
    }

    void Win32CameraToonPass::BindRegisteredGeometry(ID3D11DeviceContext* context, const void* material,bool selectiveOutlines,bool transparent,const void* owner) {
        auto found=MaterialEffects.find(material);
        if(found==MaterialEffects.end()) BindGeometry(context,0,0,false,false,selectiveOutlines?0.0f:1.0f,0.0f,false,owner);
        else BindGeometry(context,found->second[0],found->second[1],found->second[2]>0.5f,found->second[3]>0.5f,found->second[4],found->second[5],found->second[6]>0.5f,owner);
        if(MaterialOutlineOnly && (Effects.ToonEnabled || MaterialToonEnabled) && MaterialOutlineScale>0) AuxiliaryOutlinesSubmitted=true;
    }

#ifdef HELENGINE_LIGHTMAP
    void Win32CameraToonPass::BindLightmapGeometry(ID3D11DeviceContext* context,const void* material) {
        const auto found=MaterialEffects.find(material);const bool pearl=found!=MaterialEffects.end() && found->second[2]>0.5f && Effects.PearlEnabled;
        context->IASetInputLayout(LightmapInputLayout.Get());
        context->VSSetShader(LightmapVertexShader.Get(),nullptr,0);
        context->PSSetShader(MaterialOutlineOnly ? OutlineOnlyPixelShader.Get() : pearl?LightmapPearlPixelShader.Get():LightmapPixelShader.Get(),nullptr,0);
    }
#endif

    void Win32CameraToonPass::UseTargets(const std::shared_ptr<Win32CameraToonTargets>& targets) {
        SharedTargets=targets;
        Color=targets->Color;
        Parts=targets->Parts;
        Coverage=targets->Coverage;
        ResolvedColor=targets->ResolvedColor;
        ResolvedCoverage=targets->ResolvedCoverage;
        ColorTarget=targets->ColorTarget;
        PartTarget=targets->PartTarget;
        AuxiliaryParts=targets->AuxiliaryParts; AuxiliaryPartTarget=targets->AuxiliaryPartTarget; AuxiliaryPartView=targets->AuxiliaryPartView;
        CoverageTarget=targets->CoverageTarget;
        ColorView=targets->ColorView;
        PartView=targets->PartView;
        CoverageView=targets->CoverageView;
        DepthTarget=targets->DepthTarget; DepthView=targets->DepthView;
        EmittedLight=targets->EmittedLight;
        ResolvedEmittedLight=targets->ResolvedEmittedLight;
        EmittedLightTarget=targets->EmittedLightTarget;
        EmittedLightView=targets->EmittedLightView;
        BloomHorizontalTarget=targets->BloomHorizontalTarget;
        BloomVerticalTarget=targets->BloomVerticalTarget;
        BloomHorizontalView=targets->BloomHorizontalView;
        BloomVerticalView=targets->BloomVerticalView;
        DiffuseIndirect=targets->DiffuseIndirect;
        ResolvedDiffuseIndirect=targets->ResolvedDiffuseIndirect;
        DiffuseIndirectTarget=targets->DiffuseIndirectTarget;
        DiffuseIndirectView=targets->DiffuseIndirectView;
        AmbientVisibilityTarget=targets->AmbientVisibilityTarget;
        AmbientVisibilityView=targets->AmbientVisibilityView;
        FilteredVisibilityTarget=targets->FilteredVisibilityTarget;
        FilteredVisibilityView=targets->FilteredVisibilityView;
        GeometryPosition=targets->GeometryPosition;
        GeometryNormal=targets->GeometryNormal;
        MultisampleGeometryPosition=targets->MultisampleGeometryPosition;
        MultisampleGeometryNormal=targets->MultisampleGeometryNormal;
        GeometryPositionTarget=targets->GeometryPositionTarget;
        GeometryNormalTarget=targets->GeometryNormalTarget;
        GeometryPositionView=targets->GeometryPositionView;
        GeometryNormalView=targets->GeometryNormalView;
        OutlineTileTarget=targets->OutlineTileTarget;
        OutlineTileView=targets->OutlineTileView;
        Width=targets->Width;
        Height=targets->Height;
        Samples=targets->Samples;
        SharedTargets->Busy=true; TargetsAcquired=true;
    }

    void Win32CameraToonPass::ReleaseTargets() {
        if(!TargetsAcquired) return;
        std::lock_guard<std::mutex> lock(SharedCameraTargetsMutex);
        SharedTargets->Busy=false; TargetsAcquired=false;
    }

    void Win32CameraToonPass::Resize(UINT width, UINT height, UINT samples) {
        std::lock_guard<std::mutex> lock(SharedCameraTargetsMutex);
        if(SharedTargets && Width==width && Height==height && Samples==samples && (!SharedTargets->Busy || TargetsAcquired)) {
            SharedTargets->Busy=true; TargetsAcquired=true; return;
        }
        if (width == 0 || height == 0 || (samples != 1 && samples != 4)) throw std::invalid_argument("Camera toon targets require positive dimensions and 1x or 4x samples.");
        for(auto entry=SharedCameraTargets.begin();entry!=SharedCameraTargets.end();) {
            if(entry->second.expired()) entry=SharedCameraTargets.erase(entry);
            else ++entry;
        }
        auto found=SharedCameraTargets.find(Device.Get());
        if(found!=SharedCameraTargets.end()) {
            auto targets=found->second.lock();
            if(targets && !targets->Busy && targets->Width==width && targets->Height==height && targets->Samples==samples) {
                UseTargets(targets); return;
            }
        }
        // Construct replacements locally: allocation failures leave the previous valid camera resources intact.
        Microsoft::WRL::ComPtr<ID3D11Texture2D> color, parts, coverage, resolvedColor, resolvedCoverage, depth;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> colorTarget, partTarget, coverageTarget;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> colorView, partView, coverageView;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthTarget;
        D3D11_TEXTURE2D_DESC texture {};
        texture.Width = width; texture.Height = height; texture.MipLevels = 1; texture.ArraySize = 1;
        texture.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; texture.SampleDesc.Count = samples;
        texture.BindFlags = D3D11_BIND_RENDER_TARGET | (samples == 1 ? D3D11_BIND_SHADER_RESOURCE : 0);
        Check(Device->CreateTexture2D(&texture, nullptr, &color), "Create toon color target");
        Check(Device->CreateRenderTargetView(color.Get(), nullptr, &colorTarget), "Create toon color RTV");
        texture.Format = DXGI_FORMAT_R32_UINT;
        texture.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        Check(Device->CreateTexture2D(&texture, nullptr, &parts), "Create toon integer part target");
        Check(Device->CreateRenderTargetView(parts.Get(), nullptr, &partTarget), "Create toon part RTV");
        Check(Device->CreateShaderResourceView(parts.Get(), nullptr, &partView), "Create unaveraged toon part SRV");
        Microsoft::WRL::ComPtr<ID3D11Texture2D> auxiliaryParts;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> auxiliaryTarget;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> auxiliaryView;
        texture.Format=DXGI_FORMAT_R32G32_UINT;
        Check(Device->CreateTexture2D(&texture,nullptr,&auxiliaryParts),"Create auxiliary contour mask");
        Check(Device->CreateRenderTargetView(auxiliaryParts.Get(),nullptr,&auxiliaryTarget),"Create auxiliary contour RTV");
        Check(Device->CreateShaderResourceView(auxiliaryParts.Get(),nullptr,&auxiliaryView),"Create auxiliary contour SRV");
        texture.Format = DXGI_FORMAT_R32_FLOAT;
        texture.BindFlags = D3D11_BIND_RENDER_TARGET | (samples == 1 ? D3D11_BIND_SHADER_RESOURCE : 0);
        Check(Device->CreateTexture2D(&texture, nullptr, &coverage), "Create toon geometric coverage target");
        Check(Device->CreateRenderTargetView(coverage.Get(), nullptr, &coverageTarget), "Create toon coverage RTV");
        if (samples == 1) {
            resolvedColor = color; resolvedCoverage = coverage;
        } else {
            texture.SampleDesc.Count = 1; texture.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            Check(Device->CreateTexture2D(&texture, nullptr, &resolvedCoverage), "Create resolved toon coverage");
            texture.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            Check(Device->CreateTexture2D(&texture, nullptr, &resolvedColor), "Create resolved toon color");
        }
        Check(Device->CreateShaderResourceView(resolvedColor.Get(), nullptr, &colorView), "Create toon color SRV");
        Check(Device->CreateShaderResourceView(resolvedCoverage.Get(), nullptr, &coverageView), "Create toon coverage SRV");
        Microsoft::WRL::ComPtr<ID3D11Texture2D> emitted,resolvedEmitted,horizontal,vertical;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> emittedTarget,horizontalTarget,verticalTarget;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> emittedView,horizontalView,verticalView;
        texture.Format=DXGI_FORMAT_R16G16B16A16_FLOAT; texture.SampleDesc.Count=samples;
        texture.BindFlags=D3D11_BIND_RENDER_TARGET|(samples==1 ? D3D11_BIND_SHADER_RESOURCE : 0);
        Check(Device->CreateTexture2D(&texture,nullptr,&emitted),"Create selected HDR emission target");
        Check(Device->CreateRenderTargetView(emitted.Get(),nullptr,&emittedTarget),"Create selected emission RTV");
        if(samples==1) resolvedEmitted=emitted;
        else {
            texture.SampleDesc.Count=1; texture.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            Check(Device->CreateTexture2D(&texture,nullptr,&resolvedEmitted),"Create resolved selected emission");
        }
        Check(Device->CreateShaderResourceView(resolvedEmitted.Get(),nullptr,&emittedView),"Create selected emission SRV");
        texture.Width=(width+1)/2; texture.Height=(height+1)/2; texture.SampleDesc.Count=1;
        texture.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        Check(Device->CreateTexture2D(&texture,nullptr,&horizontal),"Create reduced horizontal glow");
        Check(Device->CreateTexture2D(&texture,nullptr,&vertical),"Create reduced vertical glow");
        Check(Device->CreateRenderTargetView(horizontal.Get(),nullptr,&horizontalTarget),"Create horizontal glow RTV");
        Check(Device->CreateRenderTargetView(vertical.Get(),nullptr,&verticalTarget),"Create vertical glow RTV");
        Check(Device->CreateShaderResourceView(horizontal.Get(),nullptr,&horizontalView),"Create horizontal glow SRV");
        Check(Device->CreateShaderResourceView(vertical.Get(),nullptr,&verticalView),"Create vertical glow SRV");
        texture.Width=width; texture.Height=height; texture.SampleDesc.Count=samples;
        texture.BindFlags=D3D11_BIND_RENDER_TARGET|(samples==1 ? D3D11_BIND_SHADER_RESOURCE : 0);
        Microsoft::WRL::ComPtr<ID3D11Texture2D> position,normal,resolvedPosition,resolvedNormal;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> positionTarget,normalTarget;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> positionView,normalView;
        Check(Device->CreateTexture2D(&texture,nullptr,&position),"Create camera world-position target");
        Check(Device->CreateTexture2D(&texture,nullptr,&normal),"Create camera world-normal target");
        Check(Device->CreateRenderTargetView(position.Get(),nullptr,&positionTarget),"Create camera position RTV");
        Check(Device->CreateRenderTargetView(normal.Get(),nullptr,&normalTarget),"Create camera normal RTV");
        if(samples==1) { resolvedPosition=position; resolvedNormal=normal; }
        else {
            texture.SampleDesc.Count=1; texture.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            Check(Device->CreateTexture2D(&texture,nullptr,&resolvedPosition),"Create resolved camera position");
            Check(Device->CreateTexture2D(&texture,nullptr,&resolvedNormal),"Create resolved camera normal");
        }
        Check(Device->CreateShaderResourceView(resolvedPosition.Get(),nullptr,&positionView),"Create camera position SRV");
        Check(Device->CreateShaderResourceView(resolvedNormal.Get(),nullptr,&normalView),"Create camera normal SRV");
        Microsoft::WRL::ComPtr<ID3D11Texture2D> indirect,resolvedIndirect,visibility,filteredVisibility;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> indirectTarget,visibilityTarget,filteredVisibilityTarget;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> indirectView,visibilityView,filteredVisibilityView;
        texture.SampleDesc.Count=samples;texture.BindFlags=D3D11_BIND_RENDER_TARGET|(samples==1 ? D3D11_BIND_SHADER_RESOURCE:0);
        Check(Device->CreateTexture2D(&texture,nullptr,&indirect),"Create diffuse indirect target");
        Check(Device->CreateRenderTargetView(indirect.Get(),nullptr,&indirectTarget),"Create diffuse indirect RTV");
        if(samples==1) resolvedIndirect=indirect;
        else {
            texture.SampleDesc.Count=1;texture.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            Check(Device->CreateTexture2D(&texture,nullptr,&resolvedIndirect),"Create resolved diffuse indirect");
        }
        Check(Device->CreateShaderResourceView(resolvedIndirect.Get(),nullptr,&indirectView),"Create diffuse indirect SRV");
        texture.Format=DXGI_FORMAT_R8_UNORM;texture.SampleDesc.Count=1;texture.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        Check(Device->CreateTexture2D(&texture,nullptr,&visibility),"Create GTAO visibility");
        Check(Device->CreateTexture2D(&texture,nullptr,&filteredVisibility),"Create filtered GTAO visibility");
        Check(Device->CreateRenderTargetView(visibility.Get(),nullptr,&visibilityTarget),"Create GTAO visibility RTV");
        Check(Device->CreateRenderTargetView(filteredVisibility.Get(),nullptr,&filteredVisibilityTarget),"Create filtered GTAO RTV");
        Check(Device->CreateShaderResourceView(visibility.Get(),nullptr,&visibilityView),"Create GTAO visibility SRV");
        Check(Device->CreateShaderResourceView(filteredVisibility.Get(),nullptr,&filteredVisibilityView),"Create filtered GTAO SRV");
        Microsoft::WRL::ComPtr<ID3D11Texture2D> outlineTiles;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> outlineTileTarget;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> outlineTileView;
        texture.Width=(width+31)/32; texture.Height=(height+31)/32;
        texture.Format=DXGI_FORMAT_R8_UNORM; texture.SampleDesc.Count=1;
        texture.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        Check(Device->CreateTexture2D(&texture,nullptr,&outlineTiles),"Create contour tile mask");
        Check(Device->CreateRenderTargetView(outlineTiles.Get(),nullptr,&outlineTileTarget),"Create contour tile RTV");
        Check(Device->CreateShaderResourceView(outlineTiles.Get(),nullptr,&outlineTileView),"Create contour tile SRV");
        texture.Width=width; texture.Height=height;
        texture.Format = DXGI_FORMAT_R24G8_TYPELESS; texture.SampleDesc.Count = samples;
        texture.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
        Check(Device->CreateTexture2D(&texture, nullptr, &depth), "Create toon depth target");
        D3D11_DEPTH_STENCIL_VIEW_DESC depthDescription{};depthDescription.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;
        depthDescription.ViewDimension=samples==4 ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
        Check(Device->CreateDepthStencilView(depth.Get(), &depthDescription, &depthTarget), "Create toon depth DSV");
        D3D11_SHADER_RESOURCE_VIEW_DESC depthResource{};depthResource.Format=DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        depthResource.ViewDimension=samples==4 ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D;
        if(samples==1) depthResource.Texture2D.MipLevels=1;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> depthView;
        Check(Device->CreateShaderResourceView(depth.Get(),&depthResource,&depthView),"Create contour occlusion depth SRV");
        DepthView=depthView;
        Color = color; Parts = parts; Coverage = coverage;
        ResolvedColor = resolvedColor; ResolvedCoverage = resolvedCoverage;
        AuxiliaryParts=auxiliaryParts; AuxiliaryPartTarget=auxiliaryTarget; AuxiliaryPartView=auxiliaryView;
        ColorTarget = colorTarget; PartTarget = partTarget; CoverageTarget = coverageTarget;
        ColorView = colorView; PartView = partView; CoverageView = coverageView; DepthTarget = depthTarget;
        EmittedLight=emitted; ResolvedEmittedLight=resolvedEmitted; EmittedLightTarget=emittedTarget; EmittedLightView=emittedView;
        BloomHorizontalTarget=horizontalTarget; BloomVerticalTarget=verticalTarget;
        BloomHorizontalView=horizontalView; BloomVerticalView=verticalView;
        MultisampleGeometryPosition=position; MultisampleGeometryNormal=normal;
        GeometryPosition=resolvedPosition; GeometryNormal=resolvedNormal;
        GeometryPositionTarget=positionTarget; GeometryNormalTarget=normalTarget;
        GeometryPositionView=positionView; GeometryNormalView=normalView;
        DiffuseIndirect=indirect;ResolvedDiffuseIndirect=resolvedIndirect;DiffuseIndirectTarget=indirectTarget;DiffuseIndirectView=indirectView;
        AmbientVisibilityTarget=visibilityTarget;AmbientVisibilityView=visibilityView;
        FilteredVisibilityTarget=filteredVisibilityTarget;FilteredVisibilityView=filteredVisibilityView;
        OutlineTileTarget=outlineTileTarget; OutlineTileView=outlineTileView;
        Width = width; Height = height; Samples = samples;
        auto targets=std::make_shared<Win32CameraToonTargets>();
        targets->Color=Color;
        targets->Parts=Parts;
        targets->Coverage=Coverage;
        targets->ResolvedColor=ResolvedColor;
        targets->ResolvedCoverage=ResolvedCoverage;
        targets->ColorTarget=ColorTarget;
        targets->PartTarget=PartTarget;
        targets->AuxiliaryParts=AuxiliaryParts; targets->AuxiliaryPartTarget=AuxiliaryPartTarget; targets->AuxiliaryPartView=AuxiliaryPartView;
        targets->CoverageTarget=CoverageTarget;
        targets->ColorView=ColorView;
        targets->PartView=PartView;
        targets->CoverageView=CoverageView;
        targets->DepthTarget=DepthTarget; targets->DepthView=DepthView;
        targets->EmittedLight=EmittedLight;
        targets->ResolvedEmittedLight=ResolvedEmittedLight;
        targets->EmittedLightTarget=EmittedLightTarget;
        targets->EmittedLightView=EmittedLightView;
        targets->BloomHorizontalTarget=BloomHorizontalTarget;
        targets->BloomVerticalTarget=BloomVerticalTarget;
        targets->BloomHorizontalView=BloomHorizontalView;
        targets->BloomVerticalView=BloomVerticalView;
        targets->DiffuseIndirect=DiffuseIndirect;
        targets->ResolvedDiffuseIndirect=ResolvedDiffuseIndirect;
        targets->DiffuseIndirectTarget=DiffuseIndirectTarget;
        targets->DiffuseIndirectView=DiffuseIndirectView;
        targets->AmbientVisibilityTarget=AmbientVisibilityTarget;
        targets->AmbientVisibilityView=AmbientVisibilityView;
        targets->FilteredVisibilityTarget=FilteredVisibilityTarget;
        targets->FilteredVisibilityView=FilteredVisibilityView;
        targets->GeometryPosition=GeometryPosition;
        targets->GeometryNormal=GeometryNormal;
        targets->MultisampleGeometryPosition=MultisampleGeometryPosition;
        targets->MultisampleGeometryNormal=MultisampleGeometryNormal;
        targets->GeometryPositionTarget=GeometryPositionTarget;
        targets->GeometryNormalTarget=GeometryNormalTarget;
        targets->GeometryPositionView=GeometryPositionView;
        targets->GeometryNormalView=GeometryNormalView;
        targets->OutlineTileTarget=OutlineTileTarget;
        targets->OutlineTileView=OutlineTileView;
        targets->Width=Width;
        targets->Height=Height;
        targets->Samples=Samples;
        SharedTargets=targets; SharedTargets->Busy=true; TargetsAcquired=true;
        SharedCameraTargets[Device.Get()]=targets;
    }

    void Win32CameraToonPass::Begin(ID3D11DeviceContext* context, const D3D11_VIEWPORT& viewport, UINT samples, float depthNear, float depthFar) {
        if(!std::isfinite(depthNear) || !std::isfinite(depthFar) || depthNear<0 || depthFar<=depthNear) throw std::invalid_argument("Outline depth requires valid camera clip planes.");
        DepthNear=depthNear;DepthFar=depthFar;
        if(!PipelinePrepared) throw std::logic_error("Camera shaders are still loading; TryPrepare must succeed before Begin.");
        if (context == nullptr || !std::isfinite(viewport.Width) || !std::isfinite(viewport.Height)
            || viewport.Width <= 0.0f || viewport.Height <= 0.0f) throw std::invalid_argument("Camera toon requires a valid context and positive finite viewport.");
        Resize(static_cast<UINT>(std::ceil(viewport.Width)), static_cast<UINT>(std::ceil(viewport.Height)), samples);
        context->OMGetBlendState(PreviousBlend.ReleaseAndGetAddressOf(), PreviousBlendFactor, &PreviousSampleMask);
        context->PSGetConstantBuffers(8, 1, PreviousParameters.ReleaseAndGetAddressOf());
        context->PSGetShaderResources(8, 1, PreviousColorView.ReleaseAndGetAddressOf());
        context->PSGetShaderResources(9, 1, PreviousPartView.ReleaseAndGetAddressOf());
        context->PSGetShaderResources(30,1,PreviousContourDepthView.ReleaseAndGetAddressOf());
        ID3D11ShaderResourceView* emptyDepth=nullptr;context->PSSetShaderResources(30,1,&emptyDepth);
        context->PSGetShaderResources(28,1,PreviousAuxiliaryPartView.ReleaseAndGetAddressOf());
        ID3D11ShaderResourceView* emptyAuxiliary=nullptr; context->PSSetShaderResources(28,1,&emptyAuxiliary);
        context->PSGetShaderResources(10, 1, PreviousCoverageView.ReleaseAndGetAddressOf());
        for(UINT index=0;index<6;++index) context->PSGetShaderResources(11+index,1,PreviousEffectViews[index].ReleaseAndGetAddressOf());
        for(UINT index=0;index<2;++index) context->PSGetShaderResources(26+index,1,PreviousAoViews[index].ReleaseAndGetAddressOf());
        ID3D11ShaderResourceView* emptyAo[2] {};context->PSSetShaderResources(26,2,emptyAo);
        context->PSGetSamplers(8,1,PreviousEffectSampler.ReleaseAndGetAddressOf());
        ID3D11ShaderResourceView* emptyEffects[9] {}; context->PSSetShaderResources(8,9,emptyEffects);
        PartIdentifier = 0; NextMeshIdentifier=0; MeshIdentifiers.clear(); AuxiliaryOutlinesSubmitted=false; OutlinedGeometrySubmitted=false; MaterialToonEnabled=false; MaterialOutlineOnly=false; MaterialOutlineScale=1.0f; MaterialMetallic=0.0f; ViewportX = viewport.TopLeftX; ViewportY = viewport.TopLeftY;
        context->OMGetDepthStencilState(PreviousDepth.ReleaseAndGetAddressOf(), &PreviousStencilReference);
        context->RSGetState(PreviousRasterizer.ReleaseAndGetAddressOf());
        context->VSGetShader(PreviousVertexShader.ReleaseAndGetAddressOf(), nullptr, nullptr);
        context->PSGetShader(PreviousPixelShader.ReleaseAndGetAddressOf(), nullptr, nullptr);
        context->IAGetInputLayout(PreviousLayout.ReleaseAndGetAddressOf());
        context->IAGetPrimitiveTopology(&PreviousTopology);
        UploadParameters(context);
        ID3D11RenderTargetView* targets[] = { ColorTarget.Get(), PartTarget.Get(), CoverageTarget.Get(),EmittedLightTarget.Get(),GeometryPositionTarget.Get(),GeometryNormalTarget.Get(),DiffuseIndirectTarget.Get(),AuxiliaryPartTarget.Get() };
        context->OMSetRenderTargets(8, targets, DepthTarget.Get());
        const float transparent[] = { 0, 0, 0, 0 };
        context->ClearRenderTargetView(AuxiliaryPartTarget.Get(), transparent);
        context->ClearRenderTargetView(ColorTarget.Get(), transparent); context->ClearRenderTargetView(PartTarget.Get(), transparent);
        context->ClearRenderTargetView(CoverageTarget.Get(), transparent);
        context->ClearRenderTargetView(EmittedLightTarget.Get(),transparent);
        context->ClearRenderTargetView(GeometryPositionTarget.Get(),transparent);
        context->ClearRenderTargetView(GeometryNormalTarget.Get(),transparent);
        context->ClearRenderTargetView(DiffuseIndirectTarget.Get(),transparent);
        context->ClearDepthStencilView(DepthTarget.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
        D3D11_VIEWPORT local = viewport; local.TopLeftX = 0.0f; local.TopLeftY = 0.0f;
        context->RSSetViewports(1, &local);
        context->OMSetBlendState(GeometryBlend.Get(), nullptr, 0xFFFFFFFFu);
    }

    void Win32CameraToonPass::BindGeometry(ID3D11DeviceContext* context, float reflectivity, float bloomWeight, bool pearlSurface, bool toonSurface, float outlineScale, float metallic, bool outlineOnly, const void* owner) {
        if (context == nullptr) throw std::invalid_argument("Camera toon geometry requires a context.");
        if(!std::isfinite(reflectivity) || reflectivity<0 || reflectivity>1 || !std::isfinite(bloomWeight) || bloomWeight<0 || bloomWeight>1) throw std::invalid_argument("Material reflection and bloom weights must be finite from zero to one.");
        if (NextMeshIdentifier >= 8388607u) throw std::overflow_error("Camera toon exhausted exactly representable part identifiers.");
        if(!std::isfinite(outlineScale) || outlineScale<0 || outlineScale>4) throw std::invalid_argument("Material outline scale must be finite from zero to four.");
        if(!std::isfinite(metallic) || metallic<0 || metallic>1) throw std::invalid_argument("Material metallic weight must be finite from zero to one.");
        MaterialToonEnabled=toonSurface; MaterialOutlineScale=outlineScale; MaterialMetallic=metallic; MaterialOutlineOnly=outlineOnly;
        if((Effects.ToonEnabled || toonSurface) && outlineScale>0) OutlinedGeometrySubmitted=true;
        if(owner) {
            auto found=MeshIdentifiers.find(owner);
            if(found==MeshIdentifiers.end()) found=MeshIdentifiers.emplace(owner,++NextMeshIdentifier).first;
            PartIdentifier=found->second;
        } else PartIdentifier=++NextMeshIdentifier;
        UploadParameters(context,reflectivity,bloomWeight);
        context->IASetInputLayout(InputLayout.Get());
        context->VSSetShader(GeometryVertexShader.Get(), nullptr, 0); context->PSSetShader(outlineOnly ? OutlineOnlyPixelShader.Get() : pearlSurface && Effects.PearlEnabled ? PearlGeometryPixelShader.Get() : GeometryPixelShader.Get(), nullptr, 0);
        ID3D11Buffer* parameters = Parameters.Get(); context->PSSetConstantBuffers(8, 1, &parameters);
        ID3D11ShaderResourceView* environment=EnvironmentView.Get(); context->PSSetShaderResources(12,1,&environment);
        ID3D11SamplerState* sampler=EffectSampler.Get(); context->PSSetSamplers(8,1,&sampler);
    }

    void Win32CameraToonPass::Composite(ID3D11DeviceContext* context, ID3D11RenderTargetView* destination, ID3D11DepthStencilView* destinationDepth, const D3D11_VIEWPORT& viewport, bool rawColor) {
        if (context == nullptr || destination == nullptr) throw std::invalid_argument("Camera toon composition requires a context and destination.");
        D3D11_RENDER_TARGET_VIEW_DESC outputDescription{};destination->GetDesc(&outputDescription);
        HdrOutput=!rawColor && outputDescription.Format==DXGI_FORMAT_R16G16B16A16_FLOAT;
        context->OMSetRenderTargets(0, nullptr, nullptr);
        if (Samples > 1) {
            context->ResolveSubresource(ResolvedColor.Get(), 0, Color.Get(), 0, DXGI_FORMAT_R16G16B16A16_FLOAT);
            context->ResolveSubresource(ResolvedCoverage.Get(), 0, Coverage.Get(), 0, DXGI_FORMAT_R32_FLOAT);
            context->ResolveSubresource(ResolvedEmittedLight.Get(),0,EmittedLight.Get(),0,DXGI_FORMAT_R16G16B16A16_FLOAT);
            context->ResolveSubresource(GeometryPosition.Get(),0,MultisampleGeometryPosition.Get(),0,DXGI_FORMAT_R16G16B16A16_FLOAT);
            context->ResolveSubresource(ResolvedDiffuseIndirect.Get(),0,DiffuseIndirect.Get(),0,DXGI_FORMAT_R16G16B16A16_FLOAT);
            context->ResolveSubresource(GeometryNormal.Get(),0,MultisampleGeometryNormal.Get(),0,DXGI_FORMAT_R16G16B16A16_FLOAT);
        }
        context->OMSetBlendState(CompositeBlend.Get(), nullptr, 0xFFFFFFFFu); context->OMSetDepthStencilState(CompositeDepth.Get(), 0); context->RSSetState(CompositeRasterizer.Get());
        context->IASetInputLayout(nullptr); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(CompositeVertexShader.Get(), nullptr, 0);
        ID3D11Buffer* parameters = Parameters.Get(); context->PSSetConstantBuffers(8, 1, &parameters);
        ID3D11SamplerState* sampler=EffectSampler.Get(); context->PSSetSamplers(8,1,&sampler);
        if(!rawColor && Effects.AmbientOcclusionEnabled && Effects.AmbientOcclusionStrength>0 && Effects.AmbientOcclusionRadius>0 && Effects.AmbientOcclusionPixelRadius>0) {
            context->OMSetBlendState(GeometryBlend.Get(),nullptr,0xFFFFFFFFu);
            D3D11_VIEWPORT local {0,0,static_cast<float>(Width),static_cast<float>(Height),0,1};context->RSSetViewports(1,&local);
            ID3D11ShaderResourceView* geometry[]={GeometryPositionView.Get(),GeometryNormalView.Get()};context->PSSetShaderResources(14,2,geometry);
            ID3D11RenderTargetView* target=AmbientVisibilityTarget.Get();context->OMSetRenderTargets(1,&target,nullptr);
            context->PSSetShader(GtaoPixelShader.Get(),nullptr,0);UploadParameters(context);context->Draw(3,0);
            context->OMSetRenderTargets(0,nullptr,nullptr);
            ID3D11ShaderResourceView* source=AmbientVisibilityView.Get();context->PSSetShaderResources(27,1,&source);
            target=FilteredVisibilityTarget.Get();context->OMSetRenderTargets(1,&target,nullptr);
            context->PSSetShader(GtaoFilterPixelShader.Get(),nullptr,0);context->Draw(3,0);
            source=nullptr;context->PSSetShaderResources(27,1,&source);context->OMSetRenderTargets(0,nullptr,nullptr);
        }
        if(!rawColor && OutlinedGeometrySubmitted && (OutsidePixels>0 || InsidePixels>0)) {
            // Every source pixel is visited at most once here, replacing full-screen neighborhood searches.
            context->OMSetBlendState(GeometryBlend.Get(),nullptr,0xFFFFFFFFu);
            D3D11_VIEWPORT tiles {0,0,static_cast<float>((Width+31)/32),static_cast<float>((Height+31)/32),0,1};
            context->RSSetViewports(1,&tiles);
            ID3D11RenderTargetView* target=OutlineTileTarget.Get(); context->OMSetRenderTargets(1,&target,nullptr);
            ID3D11ShaderResourceView* parts=PartView.Get(); context->PSSetShaderResources(9,1,&parts);
            ID3D11ShaderResourceView* coverage=CoverageView.Get(); context->PSSetShaderResources(10,1,&coverage);
            ID3D11ShaderResourceView* normals=GeometryNormalView.Get(); context->PSSetShaderResources(15,1,&normals);
            context->PSSetShader(Samples==4 ? OutlineTileMultisamplePixelShader.Get() : OutlineTilePixelShader.Get(),nullptr,0);
            UploadParameters(context); context->Draw(3,0);
            parts=nullptr; context->PSSetShaderResources(9,1,&parts); context->OMSetRenderTargets(0,nullptr,nullptr);
        }
        if(!rawColor && ((Effects.BloomEnabled && Effects.BloomStrength>0) || SelectionColor[3]>0)) {
            const float blurRadius=SelectionColor[3]>0 ? SelectionColor[3]*3 : Effects.BloomRadius;
            context->OMSetBlendState(GeometryBlend.Get(),nullptr,0xFFFFFFFFu);
            D3D11_VIEWPORT reduced {0,0,static_cast<float>((Width+1)/2),static_cast<float>((Height+1)/2),0,1};
            context->RSSetViewports(1,&reduced); context->PSSetShader(BlurPixelShader.Get(),nullptr,0);
            ID3D11RenderTargetView* target=BloomHorizontalTarget.Get(); context->OMSetRenderTargets(1,&target,nullptr);
            ID3D11ShaderResourceView* source=SelectionColor[3]>0 ? CoverageView.Get() : EmittedLightView.Get(); context->PSSetShaderResources(11,1,&source);
            UploadParameters(context,0,0,blurRadius/static_cast<float>(Width),0); context->Draw(3,0);
            source=nullptr; context->PSSetShaderResources(11,1,&source); context->OMSetRenderTargets(0,nullptr,nullptr);
            target=BloomVerticalTarget.Get(); context->OMSetRenderTargets(1,&target,nullptr);
            source=BloomHorizontalView.Get(); context->PSSetShaderResources(11,1,&source);
            UploadParameters(context,0,0,0,blurRadius/static_cast<float>(Height)); context->Draw(3,0);
            source=nullptr; context->PSSetShaderResources(11,1,&source); context->OMSetRenderTargets(0,nullptr,nullptr);
        }
        // Preserve these dedicated bindings locally; geometry and UI never consume the display LUT.
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> previousAces;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> previousAcesSampler;
        context->PSGetShaderResources(29,1,&previousAces);
        context->PSGetSamplers(11,1,&previousAcesSampler);
        ID3D11ShaderResourceView* aces=AcesSdrView.Get();
        context->PSSetShaderResources(29,1,&aces);
        context->PSSetSamplers(11,1,&sampler);
        UploadParameters(context,0,0,0,0,rawColor);
        context->OMSetRenderTargets(1, &destination, nullptr); context->RSSetViewports(1, &viewport);
        context->OMSetBlendState(CompositeBlend.Get(),nullptr,0xFFFFFFFFu);
        context->PSSetShader(Samples == 4 ? CompositeMultisamplePixelShader.Get() : CompositePixelShader.Get(), nullptr, 0);
        ID3D11ShaderResourceView* views[] = { ColorView.Get(), PartView.Get(), CoverageView.Get() }; context->PSSetShaderResources(8, 3, views);
        ID3D11ShaderResourceView* glow=BloomVerticalView.Get(); context->PSSetShaderResources(13,1,&glow);
        ID3D11ShaderResourceView* geometryViews[]={GeometryPositionView.Get(),GeometryNormalView.Get()}; context->PSSetShaderResources(14,2,geometryViews);
        ID3D11ShaderResourceView* outlineTiles=OutlineTileView.Get(); context->PSSetShaderResources(16,1,&outlineTiles);
        ID3D11ShaderResourceView* aoViews[]={DiffuseIndirectView.Get(),FilteredVisibilityView.Get()};context->PSSetShaderResources(26,2,aoViews);
        ID3D11ShaderResourceView* proxyMask=AuxiliaryPartView.Get();context->PSSetShaderResources(28,1,&proxyMask);
        ID3D11ShaderResourceView* proxyDepth=DepthView.Get();context->PSSetShaderResources(30,1,&proxyDepth);
        context->Draw(3, 0);
        if(!rawColor && AuxiliaryOutlinesSubmitted && (OutsidePixels>0 || InsidePixels>0)) {
            // Reuse the occupancy target after opaque composition has consumed it.
            ID3D11ShaderResourceView* unbound=nullptr; context->PSSetShaderResources(16,1,&unbound);
            ID3D11ShaderResourceView* contourDepth=DepthView.Get();context->PSSetShaderResources(30,1,&contourDepth);
            ID3D11ShaderResourceView* transparent=AuxiliaryPartView.Get(); context->PSSetShaderResources(28,1,&transparent);
            context->OMSetBlendState(GeometryBlend.Get(),nullptr,0xFFFFFFFFu);
            D3D11_VIEWPORT tiles {0,0,static_cast<float>((Width+31)/32),static_cast<float>((Height+31)/32),0,1};
            context->RSSetViewports(1,&tiles);
            ID3D11RenderTargetView* tileTarget=OutlineTileTarget.Get(); context->OMSetRenderTargets(1,&tileTarget,nullptr);
            context->PSSetShader(Samples==4 ? AuxiliaryTileMultisamplePixelShader.Get() : AuxiliaryTilePixelShader.Get(),nullptr,0);
            context->Draw(3,0);
            context->OMSetRenderTargets(1,&destination,nullptr); context->RSSetViewports(1,&viewport);
            context->PSSetShaderResources(16,1,&outlineTiles);
            context->OMSetBlendState(CompositeBlend.Get(),nullptr,0xFFFFFFFFu);
            context->PSSetShader(Samples==4 ? AuxiliaryOutlineMultisamplePixelShader.Get() : AuxiliaryOutlinePixelShader.Get(),nullptr,0);
            context->Draw(3,0);
        }
        aces=previousAces.Get(); context->PSSetShaderResources(29,1,&aces);
        ID3D11SamplerState* restoredAcesSampler=previousAcesSampler.Get();
        context->PSSetSamplers(11,1,&restoredAcesSampler);
        ID3D11ShaderResourceView* previousContourDepth=PreviousContourDepthView.Get();
        context->PSSetShaderResources(30,1,&previousContourDepth);PreviousContourDepthView.Reset();
        ID3D11ShaderResourceView* previousAuxiliary=PreviousAuxiliaryPartView.Get();
        context->PSSetShaderResources(28,1,&previousAuxiliary); PreviousAuxiliaryPartView.Reset();
        ID3D11ShaderResourceView* emptyAo[2] {};context->PSSetShaderResources(26,2,emptyAo);
        ID3D11ShaderResourceView* empty[9] {}; context->PSSetShaderResources(8, 9, empty);
        context->OMSetRenderTargets(1, &destination, destinationDepth);
        context->OMSetBlendState(PreviousBlend.Get(), PreviousBlendFactor, PreviousSampleMask); context->OMSetDepthStencilState(PreviousDepth.Get(), PreviousStencilReference); context->RSSetState(PreviousRasterizer.Get());
        context->VSSetShader(PreviousVertexShader.Get(), nullptr, 0); context->PSSetShader(PreviousPixelShader.Get(), nullptr, 0);
        context->IASetInputLayout(PreviousLayout.Get()); context->IASetPrimitiveTopology(PreviousTopology);
        ID3D11Buffer* previousParameters = PreviousParameters.Get(); context->PSSetConstantBuffers(8, 1, &previousParameters);
        ID3D11ShaderResourceView* previousViews[] = { PreviousColorView.Get(), PreviousPartView.Get(), PreviousCoverageView.Get() }; context->PSSetShaderResources(8, 3, previousViews);
        for(UINT index=0;index<6;++index) {
            ID3D11ShaderResourceView* previous=PreviousEffectViews[index].Get(); context->PSSetShaderResources(11+index,1,&previous); PreviousEffectViews[index].Reset();
        }
        for(UINT index=0;index<2;++index) {
            ID3D11ShaderResourceView* previous=PreviousAoViews[index].Get();context->PSSetShaderResources(26+index,1,&previous);PreviousAoViews[index].Reset();
        }
        sampler=PreviousEffectSampler.Get(); context->PSSetSamplers(8,1,&sampler); PreviousEffectSampler.Reset();
        PreviousDepth.Reset(); PreviousRasterizer.Reset(); PreviousVertexShader.Reset(); PreviousPixelShader.Reset(); PreviousLayout.Reset();
        PreviousBlend.Reset(); PreviousParameters.Reset(); PreviousColorView.Reset(); PreviousPartView.Reset(); PreviousCoverageView.Reset();
        ReleaseTargets();
    }
}
