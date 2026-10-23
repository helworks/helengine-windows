#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <d3dcompiler.h>
#include "platform/windows/directx11/directx11_output_attachments.hpp"

namespace helengine::windows {
    /// Verifies real engine attachments and resolves on synthetic GPU textures, without HWNDs, presentation or desktop capture.
    class OutputAttachmentsTests {
        /// Owns the test hardware device and shared immediate context.
        Microsoft::WRL::ComPtr<ID3D11Device> Device;
        /// Borrows no external graphics state or user application surfaces.
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> Context;
        /// Generates synthetic triangles from vertex IDs without asset or vertex-buffer dependencies.
        Microsoft::WRL::ComPtr<ID3D11VertexShader> VertexShader;
        /// Writes explicit premultiplied synthetic RGBA for coverage verification.
        Microsoft::WRL::ComPtr<ID3D11PixelShader> PixelShader;
        /// Supplies tint, depth and triangle geometry to both shader stages.
        Microsoft::WRL::ComPtr<ID3D11Buffer> Constants;
        /// Enables coverage rasterization for multisampled test outputs.
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> Rasterizer;
        /// Requires a farther draw to fail against the matching multisampled depth buffer.
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> Depth;
    public:
        /// Creates real hardware resources only; failure is explicit and never substituted by a different renderer.
        OutputAttachmentsTests() {
            Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                nullptr, 0, D3D11_SDK_VERSION, &Device, nullptr, &Context), "Create hardware test device");
            const char* source = R"(
                cbuffer Parameters : register(b0) { float4 Tint; float Depth; float EdgeMode; float2 Padding; };
                float4 VS(uint id : SV_VertexID) : SV_Position {
                    float2 p[3] = { float2(-1,-1), float2(-1,3), float2(3,-1) };
                    float2 e[3] = { float2(-1,-1), float2(-1,1), float2(1,-1) };
                    return float4(EdgeMode > 0 ? e[id] : p[id], Depth, 1);
                }
                float4 PS() : SV_Target { return Tint; }
            )";
            Microsoft::WRL::ComPtr<ID3DBlob> vs, ps, errors;
            Check(D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, "VS", "vs_4_0", 0, 0, &vs, &errors), "Compile synthetic vertex shader");
            Check(D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, "PS", "ps_4_0", 0, 0, &ps, &errors), "Compile synthetic pixel shader");
            Check(Device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &VertexShader), "Create test vertex shader");
            Check(Device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &PixelShader), "Create test pixel shader");
            D3D11_BUFFER_DESC buffer {};
            buffer.ByteWidth = 32; buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            Check(Device->CreateBuffer(&buffer, nullptr, &Constants), "Create test constants");
            D3D11_RASTERIZER_DESC raster {};
            raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = D3D11_CULL_NONE;
            raster.DepthClipEnable = TRUE; raster.MultisampleEnable = TRUE;
            Check(Device->CreateRasterizerState(&raster, &Rasterizer), "Create test rasterizer");
            D3D11_DEPTH_STENCIL_DESC depth {};
            depth.DepthEnable = TRUE; depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; depth.DepthFunc = D3D11_COMPARISON_LESS;
            Check(Device->CreateDepthStencilState(&depth, &Depth), "Create test depth state");
        }

        /// Tests matching sample counts, actual color/depth behavior, partial coverage, resize replacement and shared-context isolation.
        void Run() {
            TestInvalidRequests();
            auto singleDestination = CreateDestination(9, 7);
            auto multiDestination = CreateDestination(16, 16);
            DirectX11OutputAttachments single(Device.Get(), singleDestination.Get(), 1);
            DirectX11OutputAttachments multi(Device.Get(), multiDestination.Get(), 4);
            VerifyDimensions(single, 9, 7, 1);
            VerifyDimensions(multi, 16, 16, 4);
            for (int iteration = 0; iteration < 3; iteration++) {
                Begin(single, 9, 7); Draw(0.5f, 0.25f, 0, 0.5f, 0.2f, false, ~0u); single.Resolve(Context.Get());
                AssertPixel(ReadPixels(singleDestination.Get()), 9, 4, 3, {0,64,128,128});
                Begin(multi, 16, 16); Draw(0.5f, 0.25f, 0, 0.5f, 0.2f, false, 0x5); multi.Resolve(Context.Get());
                AssertPixel(ReadPixels(multiDestination.Get()), 16, 8, 8, {0,32,64,64});
            }
            Begin(multi, 16, 16);
            Draw(1,0,0,1,0.2f,false,~0u); Draw(0,1,0,1,0.8f,false,~0u); multi.Resolve(Context.Get());
            AssertPixel(ReadPixels(multiDestination.Get()),16,8,8,{0,0,255,255});
            Begin(multi,16,16); Draw(1,1,1,1,0.2f,true,~0u); multi.Resolve(Context.Get());
            auto pixels = ReadPixels(multiDestination.Get());
            bool partial = false;
            for (std::size_t index = 3; index < pixels.size(); index += 4) { partial |= pixels[index] > 0 && pixels[index] < 255; }
            Require(partial, "MSAA triangle edges must contain partial coverage.");
            auto resizedDestination = CreateDestination(23, 11);
            DirectX11OutputAttachments resized(Device.Get(),resizedDestination.Get(),4);
            VerifyDimensions(resized,23,11,4);
            Begin(resized,23,11); Draw(0,0,1,1,0.2f,false,~0u); resized.Resolve(Context.Get());
            AssertPixel(ReadPixels(resizedDestination.Get()),23,11,5,{255,0,0,255});
            AssertPixel(ReadPixels(singleDestination.Get()),9,4,3,{0,64,128,128});
            std::cout << "PASS offscreen output attachments: 1x/4x, premultiplied alpha, coverage, depth, resize and shared context\n";
        }
    private:
        /// Creates a single-sample BGRA presentation-equivalent surface without a swapchain or native window.
        Microsoft::WRL::ComPtr<ID3D11Texture2D> CreateDestination(UINT width, UINT height) {
            D3D11_TEXTURE2D_DESC desc {};
            desc.Width=width; desc.Height=height; desc.MipLevels=1; desc.ArraySize=1;
            desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count=1; desc.BindFlags=D3D11_BIND_RENDER_TARGET;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            Check(Device->CreateTexture2D(&desc,nullptr,&texture),"Create synthetic output"); return texture;
        }
        /// Requires each attachment to use the destination's dimensions and exactly the requested color/depth sample count.
        static void VerifyDimensions(DirectX11OutputAttachments& output, UINT width, UINT height, UINT samples) {
            Microsoft::WRL::ComPtr<ID3D11Resource> color,depth;
            output.GetRenderTargetView()->GetResource(&color); output.GetDepthStencilView()->GetResource(&depth);
            Microsoft::WRL::ComPtr<ID3D11Texture2D> colorTexture,depthTexture;
            Check(color.As(&colorTexture),"Read color attachment"); Check(depth.As(&depthTexture),"Read depth attachment");
            D3D11_TEXTURE2D_DESC c {},d {}; colorTexture->GetDesc(&c); depthTexture->GetDesc(&d);
            Require(c.Width==width && c.Height==height && d.Width==width && d.Height==height
                && c.SampleDesc.Count==samples && d.SampleDesc.Count==samples && output.GetSampleCount()==samples,
                "Output color/depth dimensions and samples must match.");
        }
        /// Binds and clears precisely one output before any synthetic draw on the shared context.
        void Begin(DirectX11OutputAttachments& output,UINT width,UINT height) {
            auto* color=output.GetRenderTargetView(); auto* depth=output.GetDepthStencilView();
            Context->OMSetRenderTargets(1,&color,depth);
            const float transparent[4] {}; Context->ClearRenderTargetView(color,transparent);
            Context->ClearDepthStencilView(depth,D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,1,0);
            D3D11_VIEWPORT viewport {0,0,static_cast<float>(width),static_cast<float>(height),0,1};
            Context->RSSetViewports(1,&viewport); Context->RSSetState(Rasterizer.Get()); Context->OMSetDepthStencilState(Depth.Get(),0);
        }
        /// Draws a controlled triangle and optional sample mask; no textures or OS font services are involved.
        void Draw(float red,float green,float blue,float alpha,float depth,bool edge,UINT mask) {
            const float values[8] {red,green,blue,alpha,depth,edge ? 1.0f : 0.0f,0,0};
            Context->UpdateSubresource(Constants.Get(),0,nullptr,values,0,0);
            auto* constants=Constants.Get(); Context->VSSetConstantBuffers(0,1,&constants); Context->PSSetConstantBuffers(0,1,&constants);
            Context->IASetInputLayout(nullptr); Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            Context->VSSetShader(VertexShader.Get(),nullptr,0); Context->PSSetShader(PixelShader.Get(),nullptr,0);
            Context->OMSetBlendState(nullptr,nullptr,mask); Context->Draw(3,0);
        }
        /// Reads the synthetic single-sample destination, proving that callers never need to map a multisampled surface.
        std::vector<unsigned char> ReadPixels(ID3D11Texture2D* source) {
            D3D11_TEXTURE2D_DESC desc {}; source->GetDesc(&desc);
            Require(desc.SampleDesc.Count==1,"Resolve destination must remain single-sample.");
            desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
            Check(Device->CreateTexture2D(&desc,nullptr,&staging),"Create synthetic readback");
            Context->CopyResource(staging.Get(),source);
            D3D11_MAPPED_SUBRESOURCE mapped {}; Check(Context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Read resolved pixels");
            std::vector<unsigned char> pixels(desc.Width*desc.Height*4);
            for (UINT row=0;row<desc.Height;row++) { std::memcpy(pixels.data()+row*desc.Width*4,static_cast<unsigned char*>(mapped.pData)+row*mapped.RowPitch,desc.Width*4); }
            Context->Unmap(staging.Get(),0); return pixels;
        }
        /// Compares BGRA with one quantization unit of tolerance after GPU averaging.
        static void AssertPixel(const std::vector<unsigned char>& pixels,UINT width,UINT x,UINT y,std::array<int,4> expected) {
            for (UINT channel=0;channel<4;channel++) {
                int actual=pixels[(y*width+x)*4+channel];
                if(std::abs(actual-expected[channel])>1) { throw std::runtime_error("Resolved channel mismatch: "+std::to_string(actual)+" expected "+std::to_string(expected[channel])); }
            }
        }
        /// Rejects malformed sample requests and null dependencies before resource allocation.
        void TestInvalidRequests() {
            auto destination=CreateDestination(4,4);
            for (UINT samples : {0u,2u,8u}) {
                bool rejected=false;
                try { DirectX11OutputAttachments invalid(Device.Get(),destination.Get(),samples); }
                catch(const std::invalid_argument&) { rejected=true; }
                Require(rejected,"Unsupported sample request was accepted.");
            }
            bool rejected=false;
            try { DirectX11OutputAttachments invalid(nullptr,destination.Get(),4); } catch(const std::invalid_argument&) { rejected=true; }
            Require(rejected,"Null output device was accepted.");
            rejected=false;
            try { DirectX11OutputAttachments invalid(Device.Get(),nullptr,4); } catch(const std::invalid_argument&) { rejected=true; }
            Require(rejected,"Null resolve destination was accepted.");
        }
        /// Fails assertions without silently skipping hardware coverage.
        static void Require(bool value,const char* message) { if(!value) { throw std::runtime_error(message); } }
        /// Preserves a failed graphics call as an explicit test failure.
        static void Check(HRESULT result,const char* message) { if(FAILED(result)) { throw std::runtime_error(message); } }
    };
}
/// Runs only offscreen synthetic GPU checks; this executable never creates or shows a window.
int main() {
    try { helengine::windows::OutputAttachmentsTests tests; tests.Run(); return 0; }
    catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}