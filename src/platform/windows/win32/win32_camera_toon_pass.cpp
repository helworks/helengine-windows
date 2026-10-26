#include "platform/windows/win32/win32_camera_toon_pass.hpp"
#include "platform/windows/win32/win32_pearl_surface_shader.hpp"
#include "platform/windows/win32/win32_camera_toon_shader_source.hpp"

#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

namespace helengine::windows {
    namespace {
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
            /// Stores angular color strength, direct highlight strength and broadness.
            float Pearl[4];
        };

    }

    void Win32CameraToonPass::Check(HRESULT result, const char* operation) {
        if (FAILED(result)) throw std::runtime_error(std::string(operation) + " failed (HRESULT " + std::to_string(result) + ").");
    }

    Win32CameraToonPass::Win32CameraToonPass(ID3D11Device* device, float outsidePixels, float insidePixels, int bands) : Device(device) {
        if(device==nullptr) throw std::invalid_argument("Camera toon requires a graphics device.");
        Configure(outsidePixels,insidePixels,bands);
        ShaderCompilation=std::make_unique<DirectX11ShaderCompilation>(BuildCameraToonShaderRequests(std::string(PearlSurfaceShader)+CameraToonGeometryShader+CameraToonCompositeShader));
    }

    Win32CameraToonPass::~Win32CameraToonPass() = default;

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
        Check(Device->CreateVertexShader(geometryVS->GetBufferPointer(), geometryVS->GetBufferSize(), nullptr, &GeometryVertexShader), "Create toon geometry vertex shader");
        Check(Device->CreatePixelShader(geometryPS->GetBufferPointer(), geometryPS->GetBufferSize(), nullptr, &GeometryPixelShader), "Create toon geometry pixel shader");
        Check(Device->CreatePixelShader(pearlPS->GetBufferPointer(), pearlPS->GetBufferSize(), nullptr, &PearlGeometryPixelShader), "Create pearl geometry pixel shader");
        Check(Device->CreateVertexShader(compositeVS->GetBufferPointer(), compositeVS->GetBufferSize(), nullptr, &CompositeVertexShader), "Create toon composition vertex shader");
        Check(Device->CreatePixelShader(compositePS->GetBufferPointer(), compositePS->GetBufferSize(), nullptr, &CompositePixelShader), "Create toon composition pixel shader");
        Check(Device->CreatePixelShader(compositeMSPS->GetBufferPointer(), compositeMSPS->GetBufferSize(), nullptr, &CompositeMultisamplePixelShader), "Create multisampled toon composition pixel shader");
        Check(Device->CreatePixelShader(blurPS->GetBufferPointer(), blurPS->GetBufferSize(), nullptr, &BlurPixelShader), "Create selected-emission blur shader");
        D3D11_SAMPLER_DESC sampler {};
        sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD=D3D11_FLOAT32_MAX;
        Check(Device->CreateSamplerState(&sampler,&EffectSampler),"Create camera effect sampler");
        D3D11_INPUT_ELEMENT_DESC elements[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };
        Check(Device->CreateInputLayout(elements, 3, geometryVS->GetBufferPointer(), geometryVS->GetBufferSize(), &InputLayout), "Create toon mesh input layout");
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
        const float values[] {configuration.ReflectionStrength,configuration.BloomStrength,configuration.BloomThreshold,configuration.BloomRadius,configuration.ShadowStrength,configuration.ShadowSoftness,configuration.PearlStrength,configuration.PearlHighlight,configuration.PearlWidth};
        const float maxima[] {1,2,4,24,1,3,1,1,1};
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

    void Win32CameraToonPass::UploadParameters(ID3D11DeviceContext* context, float reflectivity, float bloomWeight, float blurX, float blurY) {
        ToonParameters parameters {
            {OutsidePixels,InsidePixels,static_cast<float>(Bands),static_cast<float>(PartIdentifier)},
            {static_cast<float>(Width),static_cast<float>(Height),ViewportX,ViewportY},
            {Effects.ReflectionsEnabled && EnvironmentView ? Effects.ReflectionStrength : 0.0f,Effects.BloomEnabled ? Effects.BloomStrength : 0.0f,Effects.BloomThreshold,Effects.BloomRadius},
            {reflectivity,bloomWeight,EnvironmentMaximumLod,Effects.ToonEnabled ? 1.0f : 0.0f},
            {Effects.ShadowsEnabled ? Effects.ShadowStrength : 0.0f,Effects.ShadowSoftness,0,0},
            {blurX,blurY,0,0},
            {Effects.PearlStrength,Effects.PearlHighlight,Effects.PearlWidth,0}
        };
        context->UpdateSubresource(Parameters.Get(),0,nullptr,&parameters,0,0);
    }

    void Win32CameraToonPass::ConfigureMaterialEffects(const void* material, float reflectivity, float bloomWeight, bool pearlSurface) {
        if(material==nullptr || !std::isfinite(reflectivity) || reflectivity<0 || reflectivity>1 || !std::isfinite(bloomWeight) || bloomWeight<0 || bloomWeight>1) throw std::invalid_argument("Camera material effects require an exact identity and finite zero-to-one weights.");
        MaterialEffects[material]={reflectivity,bloomWeight,pearlSurface ? 1.0f : 0.0f};
    }

    void Win32CameraToonPass::RemoveMaterialEffects(const void* material) {
        MaterialEffects.erase(material);
    }

    void Win32CameraToonPass::BindRegisteredGeometry(ID3D11DeviceContext* context, const void* material) {
        auto found=MaterialEffects.find(material);
        if(found==MaterialEffects.end()) BindGeometry(context);
        else BindGeometry(context,found->second[0],found->second[1],found->second[2]>0.5f);
    }

    void Win32CameraToonPass::Resize(UINT width, UINT height, UINT samples) {
        if (Width == width && Height == height && Samples == samples) return;
        if (width == 0 || height == 0 || (samples != 1 && samples != 4)) throw std::invalid_argument("Camera toon targets require positive dimensions and 1x or 4x samples.");
        // Construct replacements locally: allocation failures leave the previous valid camera resources intact.
        Microsoft::WRL::ComPtr<ID3D11Texture2D> color, parts, coverage, resolvedColor, resolvedCoverage, depth;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> colorTarget, partTarget, coverageTarget;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> colorView, partView, coverageView;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthTarget;
        D3D11_TEXTURE2D_DESC texture {};
        texture.Width = width; texture.Height = height; texture.MipLevels = 1; texture.ArraySize = 1;
        texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM; texture.SampleDesc.Count = samples;
        texture.BindFlags = D3D11_BIND_RENDER_TARGET | (samples == 1 ? D3D11_BIND_SHADER_RESOURCE : 0);
        Check(Device->CreateTexture2D(&texture, nullptr, &color), "Create toon color target");
        Check(Device->CreateRenderTargetView(color.Get(), nullptr, &colorTarget), "Create toon color RTV");
        texture.Format = DXGI_FORMAT_R32_UINT;
        texture.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        Check(Device->CreateTexture2D(&texture, nullptr, &parts), "Create toon integer part target");
        Check(Device->CreateRenderTargetView(parts.Get(), nullptr, &partTarget), "Create toon part RTV");
        Check(Device->CreateShaderResourceView(parts.Get(), nullptr, &partView), "Create unaveraged toon part SRV");
        texture.Format = DXGI_FORMAT_R32_FLOAT;
        texture.BindFlags = D3D11_BIND_RENDER_TARGET | (samples == 1 ? D3D11_BIND_SHADER_RESOURCE : 0);
        Check(Device->CreateTexture2D(&texture, nullptr, &coverage), "Create toon geometric coverage target");
        Check(Device->CreateRenderTargetView(coverage.Get(), nullptr, &coverageTarget), "Create toon coverage RTV");
        if (samples == 1) {
            resolvedColor = color; resolvedCoverage = coverage;
        } else {
            texture.SampleDesc.Count = 1; texture.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            Check(Device->CreateTexture2D(&texture, nullptr, &resolvedCoverage), "Create resolved toon coverage");
            texture.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
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
        texture.Width=width; texture.Height=height;
        texture.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; texture.SampleDesc.Count = samples; texture.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        Check(Device->CreateTexture2D(&texture, nullptr, &depth), "Create toon depth target");
        Check(Device->CreateDepthStencilView(depth.Get(), nullptr, &depthTarget), "Create toon depth DSV");
        Color = color; Parts = parts; Coverage = coverage;
        ResolvedColor = resolvedColor; ResolvedCoverage = resolvedCoverage;
        ColorTarget = colorTarget; PartTarget = partTarget; CoverageTarget = coverageTarget;
        ColorView = colorView; PartView = partView; CoverageView = coverageView; DepthTarget = depthTarget;
        EmittedLight=emitted; ResolvedEmittedLight=resolvedEmitted; EmittedLightTarget=emittedTarget; EmittedLightView=emittedView;
        BloomHorizontalTarget=horizontalTarget; BloomVerticalTarget=verticalTarget;
        BloomHorizontalView=horizontalView; BloomVerticalView=verticalView;
        Width = width; Height = height; Samples = samples;
    }

    void Win32CameraToonPass::Begin(ID3D11DeviceContext* context, const D3D11_VIEWPORT& viewport, UINT samples) {
        if(!PipelinePrepared) throw std::logic_error("Camera shaders are still loading; TryPrepare must succeed before Begin.");
        if (context == nullptr || !std::isfinite(viewport.Width) || !std::isfinite(viewport.Height)
            || viewport.Width <= 0.0f || viewport.Height <= 0.0f) throw std::invalid_argument("Camera toon requires a valid context and positive finite viewport.");
        Resize(static_cast<UINT>(std::ceil(viewport.Width)), static_cast<UINT>(std::ceil(viewport.Height)), samples);
        context->OMGetBlendState(PreviousBlend.ReleaseAndGetAddressOf(), PreviousBlendFactor, &PreviousSampleMask);
        context->PSGetConstantBuffers(8, 1, PreviousParameters.ReleaseAndGetAddressOf());
        context->PSGetShaderResources(8, 1, PreviousColorView.ReleaseAndGetAddressOf());
        context->PSGetShaderResources(9, 1, PreviousPartView.ReleaseAndGetAddressOf());
        context->PSGetShaderResources(10, 1, PreviousCoverageView.ReleaseAndGetAddressOf());
        for(UINT index=0;index<3;++index) context->PSGetShaderResources(11+index,1,PreviousEffectViews[index].ReleaseAndGetAddressOf());
        context->PSGetSamplers(8,1,PreviousEffectSampler.ReleaseAndGetAddressOf());
        ID3D11ShaderResourceView* emptyEffects[6] {}; context->PSSetShaderResources(8,6,emptyEffects);
        PartIdentifier = 0; ViewportX = viewport.TopLeftX; ViewportY = viewport.TopLeftY;
        context->OMGetDepthStencilState(PreviousDepth.ReleaseAndGetAddressOf(), &PreviousStencilReference);
        context->RSGetState(PreviousRasterizer.ReleaseAndGetAddressOf());
        context->VSGetShader(PreviousVertexShader.ReleaseAndGetAddressOf(), nullptr, nullptr);
        context->PSGetShader(PreviousPixelShader.ReleaseAndGetAddressOf(), nullptr, nullptr);
        context->IAGetInputLayout(PreviousLayout.ReleaseAndGetAddressOf());
        context->IAGetPrimitiveTopology(&PreviousTopology);
        UploadParameters(context);
        ID3D11RenderTargetView* targets[] = { ColorTarget.Get(), PartTarget.Get(), CoverageTarget.Get(),EmittedLightTarget.Get() };
        context->OMSetRenderTargets(4, targets, DepthTarget.Get());
        const float transparent[] = { 0, 0, 0, 0 };
        context->ClearRenderTargetView(ColorTarget.Get(), transparent); context->ClearRenderTargetView(PartTarget.Get(), transparent);
        context->ClearRenderTargetView(CoverageTarget.Get(), transparent);
        context->ClearRenderTargetView(EmittedLightTarget.Get(),transparent);
        context->ClearDepthStencilView(DepthTarget.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
        D3D11_VIEWPORT local = viewport; local.TopLeftX = 0.0f; local.TopLeftY = 0.0f;
        context->RSSetViewports(1, &local);
        context->OMSetBlendState(GeometryBlend.Get(), nullptr, 0xFFFFFFFFu);
    }

    void Win32CameraToonPass::BindGeometry(ID3D11DeviceContext* context, float reflectivity, float bloomWeight, bool pearlSurface) {
        if (context == nullptr) throw std::invalid_argument("Camera toon geometry requires a context.");
        if(!std::isfinite(reflectivity) || reflectivity<0 || reflectivity>1 || !std::isfinite(bloomWeight) || bloomWeight<0 || bloomWeight>1) throw std::invalid_argument("Material reflection and bloom weights must be finite from zero to one.");
        if (PartIdentifier >= 16777216u) throw std::overflow_error("Camera toon exhausted exactly representable part identifiers.");
        ++PartIdentifier; UploadParameters(context,reflectivity,bloomWeight);
        context->IASetInputLayout(InputLayout.Get());
        context->VSSetShader(GeometryVertexShader.Get(), nullptr, 0); context->PSSetShader(pearlSurface && Effects.PearlEnabled ? PearlGeometryPixelShader.Get() : GeometryPixelShader.Get(), nullptr, 0);
        ID3D11Buffer* parameters = Parameters.Get(); context->PSSetConstantBuffers(8, 1, &parameters);
        ID3D11ShaderResourceView* environment=EnvironmentView.Get(); context->PSSetShaderResources(12,1,&environment);
        ID3D11SamplerState* sampler=EffectSampler.Get(); context->PSSetSamplers(8,1,&sampler);
    }

    void Win32CameraToonPass::Composite(ID3D11DeviceContext* context, ID3D11RenderTargetView* destination, ID3D11DepthStencilView* destinationDepth, const D3D11_VIEWPORT& viewport) {
        if (context == nullptr || destination == nullptr) throw std::invalid_argument("Camera toon composition requires a context and destination.");
        context->OMSetRenderTargets(0, nullptr, nullptr);
        if (Samples > 1) {
            context->ResolveSubresource(ResolvedColor.Get(), 0, Color.Get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM);
            context->ResolveSubresource(ResolvedCoverage.Get(), 0, Coverage.Get(), 0, DXGI_FORMAT_R32_FLOAT);
            context->ResolveSubresource(ResolvedEmittedLight.Get(),0,EmittedLight.Get(),0,DXGI_FORMAT_R16G16B16A16_FLOAT);
        }
        context->OMSetBlendState(CompositeBlend.Get(), nullptr, 0xFFFFFFFFu); context->OMSetDepthStencilState(CompositeDepth.Get(), 0); context->RSSetState(CompositeRasterizer.Get());
        context->IASetInputLayout(nullptr); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(CompositeVertexShader.Get(), nullptr, 0);
        ID3D11Buffer* parameters = Parameters.Get(); context->PSSetConstantBuffers(8, 1, &parameters);
        ID3D11SamplerState* sampler=EffectSampler.Get(); context->PSSetSamplers(8,1,&sampler);
        if(Effects.BloomEnabled && Effects.BloomStrength>0) {
            context->OMSetBlendState(GeometryBlend.Get(),nullptr,0xFFFFFFFFu);
            D3D11_VIEWPORT reduced {0,0,static_cast<float>((Width+1)/2),static_cast<float>((Height+1)/2),0,1};
            context->RSSetViewports(1,&reduced); context->PSSetShader(BlurPixelShader.Get(),nullptr,0);
            ID3D11RenderTargetView* target=BloomHorizontalTarget.Get(); context->OMSetRenderTargets(1,&target,nullptr);
            ID3D11ShaderResourceView* source=EmittedLightView.Get(); context->PSSetShaderResources(11,1,&source);
            UploadParameters(context,0,0,Effects.BloomRadius/static_cast<float>(Width),0); context->Draw(3,0);
            source=nullptr; context->PSSetShaderResources(11,1,&source); context->OMSetRenderTargets(0,nullptr,nullptr);
            target=BloomVerticalTarget.Get(); context->OMSetRenderTargets(1,&target,nullptr);
            source=BloomHorizontalView.Get(); context->PSSetShaderResources(11,1,&source);
            UploadParameters(context,0,0,0,Effects.BloomRadius/static_cast<float>(Height)); context->Draw(3,0);
            source=nullptr; context->PSSetShaderResources(11,1,&source); context->OMSetRenderTargets(0,nullptr,nullptr);
        }
        UploadParameters(context);
        context->OMSetRenderTargets(1, &destination, nullptr); context->RSSetViewports(1, &viewport);
        context->OMSetBlendState(CompositeBlend.Get(),nullptr,0xFFFFFFFFu);
        context->PSSetShader(Samples == 4 ? CompositeMultisamplePixelShader.Get() : CompositePixelShader.Get(), nullptr, 0);
        ID3D11ShaderResourceView* views[] = { ColorView.Get(), PartView.Get(), CoverageView.Get() }; context->PSSetShaderResources(8, 3, views);
        ID3D11ShaderResourceView* glow=BloomVerticalView.Get(); context->PSSetShaderResources(13,1,&glow);
        context->Draw(3, 0);
        ID3D11ShaderResourceView* empty[6] {}; context->PSSetShaderResources(8, 6, empty);
        context->OMSetRenderTargets(1, &destination, destinationDepth);
        context->OMSetBlendState(PreviousBlend.Get(), PreviousBlendFactor, PreviousSampleMask); context->OMSetDepthStencilState(PreviousDepth.Get(), PreviousStencilReference); context->RSSetState(PreviousRasterizer.Get());
        context->VSSetShader(PreviousVertexShader.Get(), nullptr, 0); context->PSSetShader(PreviousPixelShader.Get(), nullptr, 0);
        context->IASetInputLayout(PreviousLayout.Get()); context->IASetPrimitiveTopology(PreviousTopology);
        ID3D11Buffer* previousParameters = PreviousParameters.Get(); context->PSSetConstantBuffers(8, 1, &previousParameters);
        ID3D11ShaderResourceView* previousViews[] = { PreviousColorView.Get(), PreviousPartView.Get(), PreviousCoverageView.Get() }; context->PSSetShaderResources(8, 3, previousViews);
        for(UINT index=0;index<3;++index) {
            ID3D11ShaderResourceView* previous=PreviousEffectViews[index].Get(); context->PSSetShaderResources(11+index,1,&previous); PreviousEffectViews[index].Reset();
        }
        sampler=PreviousEffectSampler.Get(); context->PSSetSamplers(8,1,&sampler); PreviousEffectSampler.Reset();
        PreviousDepth.Reset(); PreviousRasterizer.Reset(); PreviousVertexShader.Reset(); PreviousPixelShader.Reset(); PreviousLayout.Reset();
        PreviousBlend.Reset(); PreviousParameters.Reset(); PreviousColorView.Reset(); PreviousPartView.Reset(); PreviousCoverageView.Reset();
    }
}
