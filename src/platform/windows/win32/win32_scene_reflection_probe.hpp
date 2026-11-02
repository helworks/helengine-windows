#pragma once
#include "hellm/ReflectionProbe.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
namespace helengine::windows {
    /// <summary>Uploads immutable linear scene radiance once and shares its cubemap and GGX integration across scene cameras.</summary>
    class Win32SceneReflectionProbe {
        /// <summary>Retains all six faces and the complete prefiltered perceptual-roughness mip chain.</summary>
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> Cube;
        /// <summary>Retains the split-sum Fresnel scale and bias, independently integrated from the scene radiance.</summary>
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> Brdf;
        /// <summary>Stores enabled/mip state, capture position and the authored box in four IEEE32 vectors.</summary>
        Microsoft::WRL::ComPtr<ID3D11Buffer> Parameters;
        /// <summary>Filters faces and mip levels linearly without wrapping lookup coordinates.</summary>
        Microsoft::WRL::ComPtr<ID3D11SamplerState> Sampler;
        /// <summary>Propagates the original device failure before publishing incomplete reflection resources.</summary>
        static void Check(HRESULT result) {
            if(FAILED(result)) throw std::runtime_error("Reflection probe GPU allocation failed: "+std::to_string(result));
        }
        /// <summary>Reverses sample bits for the deterministic base-two radical inverse used by GGX quadrature.</summary>
        static double RadicalInverse(uint32_t bits) {
            bits=(bits<<16)|(bits>>16);
            bits=((bits&0x55555555u)<<1)|((bits&0xAAAAAAAAu)>>1);
            bits=((bits&0x33333333u)<<2)|((bits&0xCCCCCCCCu)>>2);
            bits=((bits&0x0F0F0F0Fu)<<4)|((bits&0xF0F0F0F0u)>>4);
            bits=((bits&0x00FF00FFu)<<8)|((bits&0xFF00FF00u)>>8);
            return static_cast<double>(bits)*2.3283064365386963e-10;
        }
        /// <summary>Integrates single-scatter GGX with exact Smith masking and Schlick Fresnel at 256 Hammersley samples per cell.</summary>
        static std::vector<float> IntegrateBrdf() {
            constexpr uint32_t size=64,samples=256;
            std::vector<float> pixels(size*size*2);
            for(uint32_t y=0;y<size;++y) for(uint32_t x=0;x<size;++x) {
                const double nv=(x+.5)/size,roughness=std::max(.045,(y+.5)/size);
                const double alpha=roughness*roughness,a2=alpha*alpha,vx=std::sqrt(1-nv*nv);
                const double gv=2*nv/(nv+std::sqrt(a2+(1-a2)*nv*nv));
                double scale=0,bias=0;
                for(uint32_t sample=0;sample<samples;++sample) {
                    const double phi=6.28318530717958647692*sample/samples,xi=RadicalInverse(sample);
                    const double hz=std::sqrt((1-xi)/(1+(a2-1)*xi)),st=std::sqrt(std::max(0.0,1-hz*hz));
                    const double hx=std::cos(phi)*st,vh=std::max(0.0,vx*hx+nv*hz),nl=2*vh*hz-nv;
                    if(nl<=0 || vh<=0) continue;
                    const double gl=2*nl/(nl+std::sqrt(a2+(1-a2)*nl*nl));
                    const double visibility=gl*gv*vh/(hz*nv),fresnel=std::pow(1-vh,5);
                    scale+=(1-fresnel)*visibility;bias+=fresnel*visibility;
                }
                scale/=samples;bias/=samples;
                // Finite quadrature must not create energy in the white furnace at grazing angles.
                const double normalization=std::max(1.0,scale+bias);
                pixels[(y*size+x)*2]=static_cast<float>(scale/normalization);
                pixels[(y*size+x)*2+1]=static_cast<float>(bias/normalization);
            }
            return pixels;
        }
    public:
        /// <summary>Validates complete HDR faces and box metadata before creating immutable camera-independent resources.</summary>
        Win32SceneReflectionProbe(ID3D11Device* device,const hellm::ReflectionProbe& probe) {
            if(!device) throw std::invalid_argument("Reflection probe requires an initialized GPU device.");
            if(probe.FaceSize==0) {
                if(!probe.Mips.empty()) throw std::invalid_argument("Disabled reflection probe contains mip data.");
                return;
            }
            if((probe.FaceSize&(probe.FaceSize-1)) || probe.FaceSize>D3D11_REQ_TEXTURECUBE_DIMENSION) throw std::invalid_argument("Reflection cube edge must be a supported power of two.");
            const float position[3]={probe.Position.X,probe.Position.Y,probe.Position.Z};
            const float minimum[3]={probe.BoundsMin.X,probe.BoundsMin.Y,probe.BoundsMin.Z},maximum[3]={probe.BoundsMax.X,probe.BoundsMax.Y,probe.BoundsMax.Z};
            for(int axis=0;axis<3;++axis) if(!std::isfinite(position[axis]) || !std::isfinite(minimum[axis]) || !std::isfinite(maximum[axis]) || minimum[axis]>=maximum[axis] || position[axis]<=minimum[axis] || position[axis]>=maximum[axis]) throw std::invalid_argument("Reflection capture must lie strictly inside a finite authored box.");
            UINT count=1;for(UINT size=probe.FaceSize;size>1;size>>=1) ++count;
            if(probe.Mips.size()!=count) throw std::invalid_argument("Reflection cube requires the complete prefiltered mip chain.");
            std::vector<D3D11_SUBRESOURCE_DATA> data(6*count);
            for(UINT mip=0;mip<count;++mip) {
                const UINT size=std::max(1u,probe.FaceSize>>mip);const auto& pixels=probe.Mips[mip];
                if(pixels.size()!=static_cast<std::size_t>(size)*size*24) throw std::invalid_argument("Reflection cube face data is incomplete.");
                for(float channel:pixels) if(!std::isfinite(channel) || channel<0) throw std::invalid_argument("Reflection radiance must be finite and nonnegative.");
                for(UINT face=0;face<6;++face) data[D3D11CalcSubresource(mip,face,count)]={pixels.data()+static_cast<std::size_t>(face)*size*size*4,size*16,0};
            }
            D3D11_TEXTURE2D_DESC texture{};texture.Width=texture.Height=probe.FaceSize;texture.ArraySize=6;texture.MipLevels=count;texture.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;texture.SampleDesc.Count=1;texture.Usage=D3D11_USAGE_IMMUTABLE;texture.BindFlags=D3D11_BIND_SHADER_RESOURCE;texture.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> cube;Check(device->CreateTexture2D(&texture,data.data(),&cube));Check(device->CreateShaderResourceView(cube.Get(),nullptr,&Cube));
            const auto integration=IntegrateBrdf();texture.Width=texture.Height=64;texture.ArraySize=texture.MipLevels=1;texture.Format=DXGI_FORMAT_R32G32_FLOAT;texture.MiscFlags=0;
            D3D11_SUBRESOURCE_DATA lookup{integration.data(),64*8,0};Microsoft::WRL::ComPtr<ID3D11Texture2D> brdf;Check(device->CreateTexture2D(&texture,&lookup,&brdf));Check(device->CreateShaderResourceView(brdf.Get(),nullptr,&Brdf));
            const float values[16]={1,static_cast<float>(count-1),0,0,position[0],position[1],position[2],0,minimum[0],minimum[1],minimum[2],0,maximum[0],maximum[1],maximum[2],0};
            D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=sizeof(values);buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;buffer.Usage=D3D11_USAGE_IMMUTABLE;D3D11_SUBRESOURCE_DATA constants{values,0,0};Check(device->CreateBuffer(&buffer,&constants,&Parameters));
            D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampler.MaxLOD=D3D11_FLOAT32_MAX;Check(device->CreateSamplerState(&sampler,&Sampler));
        }
        /// <summary>Reuses exactly the same cube, LUT and metadata for every eligible camera draw.</summary>
        void Bind(ID3D11DeviceContext* context) const {
            ID3D11ShaderResourceView* views[]={Cube.Get(),Brdf.Get()};auto* parameters=Parameters.Get();auto* sampler=Sampler.Get();
            context->PSSetShaderResources(24,2,views);context->PSSetConstantBuffers(12,1,&parameters);context->PSSetSamplers(10,1,&sampler);
        }
        /// <summary>Releases every reflection binding before ordinary geometry, UI, or resource teardown.</summary>
        static void Unbind(ID3D11DeviceContext* context) {
            if(!context) return;
            ID3D11ShaderResourceView* views[2]={nullptr,nullptr};ID3D11Buffer* parameters=nullptr;ID3D11SamplerState* sampler=nullptr;
            context->PSSetShaderResources(24,2,views);context->PSSetConstantBuffers(12,1,&parameters);context->PSSetSamplers(10,1,&sampler);
        }
    };
}
