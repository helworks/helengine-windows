#include <array>
#include <chrono>
#include <thread>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "platform/windows/directx11/directx11_preview_processor.hpp"

namespace helengine::windows {
    /// Exercises authorized-surface processing using synthetic GPU pixels, without any HWND or desktop capture.
    class TexturePreviewTests {
        /// Owns the hardware device used for all synthetic capture surfaces.
        Microsoft::WRL::ComPtr<ID3D11Device> Device;
        /// Owns the shared immediate context whose caller bindings must survive processing.
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> Context;
    public:
        /// Creates real GPU dependencies and fails explicitly when hardware initialization is unavailable.
        TexturePreviewTests() {
            Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                nullptr,0,D3D11_SDK_VERSION,&Device,nullptr,&Context),"Create test hardware");
        }
        /// Verifies colors, crop, bounds, caching, explicit rejection and caller-state preservation.
        void Run() {
            auto submitted=std::chrono::steady_clock::now();
            DirectX11PreviewProcessor processor(Device.Get(),Context.Get());
            Require(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-submitted).count()<500,
                "Preview creation blocked on shader compilation.");
            DirectX11TexturePreview preview(Device.Get(),720,480);
            Require(preview.GetTexture()==nullptr,"An unused preview must allocate no image.");
            auto bgra=CreateSource(8,6,DXGI_FORMAT_B8G8R8A8_UNORM,0,4,2);
            auto sentinel=CreateSource(8,6,DXGI_FORMAT_R8G8B8A8_UNORM,D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE,8,6);
            Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
            Check(Device->CreateRenderTargetView(sentinel.Get(),nullptr,&target),"Create caller target");
            auto* bound=target.Get(); Context->OMSetRenderTargets(1,&bound,nullptr);
            D3D11_VIEWPORT viewport {2,3,4,5,0.2f,0.8f}; Context->RSSetViewports(1,&viewport);
            Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
            const float blend[4] {0.1f,0.2f,0.3f,0.4f}; Context->OMSetBlendState(nullptr,blend,0x1234);
            // Exercise Update itself before readiness, not only the polling helper.
            bool updated=processor.Update(preview,bgra.Get(),4,2);
            VerifyCallerState(target.Get());
            if(!updated) {
                Require(preview.GetTexture()==nullptr,"Loading preview shaders allocated an unrendered image.");
                VerifyCallerState(target.Get());
            }
            auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
            while(!processor.TryPrepare()) {
                Require(std::chrono::steady_clock::now()<deadline,"Preview shaders did not finish.");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            processor.Update(preview,bgra.Get(),4,2);
            VerifyCallerState(target.Get()); VerifyCrop(preview,4,2);
            auto* retained=preview.GetTexture();
            DirectX11PreviewProcessor loading(Device.Get(),Context.Get());
            if(!loading.Update(preview,bgra.Get(),4,2)) {
                Require(preview.GetTexture()==retained,"A pending shader discarded the previous preview texture.");
                VerifyCrop(preview,4,2);
                VerifyCallerState(target.Get());
                std::cout<<"Pending Update preserved cached image and caller bindings.\n";
            }
            preview.ResetSourceCopy();
            Require(preview.GetTexture()==retained,"Suspending capture must retain the last image.");
            processor.Update(preview,bgra.Get(),4,2);
            Require(preview.GetTexture()==retained,"Same-size frames must reuse bounded image storage.");
            auto rgba=CreateSource(8,6,DXGI_FORMAT_R8G8B8A8_UNORM,0,4,2);
            processor.Update(preview,rgba.Get(),4,2); VerifyCrop(preview,4,2);
            auto readable=CreateSource(9,7,DXGI_FORMAT_B8G8R8A8_UNORM,D3D11_BIND_SHADER_RESOURCE,5,3);
            processor.Update(preview,readable.Get(),5,3); VerifyCrop(preview,5,3);
            auto resized=CreateSource(10,8,DXGI_FORMAT_R8G8B8A8_UNORM,0,6,4);
            processor.Update(preview,resized.Get(),6,4); VerifyCrop(preview,6,4);
            auto large=CreateSource(1440,960,DXGI_FORMAT_B8G8R8A8_UNORM,0,1440,960,true);
            processor.Update(preview,large.Get(),1440,960);
            D3D11_TEXTURE2D_DESC desc {}; preview.GetTexture()->GetDesc(&desc);
            Require(desc.Width==720 && desc.Height==480,"Large captures must fit the existing 720x480 bound.");
            auto pixels=Read(preview.GetTexture());
            AssertPixel(pixels,720,0,0,{64,32,16,128}); AssertPixel(pixels,720,719,479,{64,32,16,128});
            DirectX11TexturePreview tiny(Device.Get(),1,1);
            processor.Update(tiny,bgra.Get(),1,1); VerifyCrop(tiny,1,1);
            VerifyCallerState(target.Get());
            Reject(processor,preview,nullptr,1,1);
            Reject(processor,preview,bgra.Get(),0,2);
            Reject(processor,preview,bgra.Get(),9,2);
            Reject(processor,preview,preview.GetTexture(),720,480);
            for(int shape=0;shape<4;shape++) {
                D3D11_TEXTURE2D_DESC invalid {}; invalid.Width=invalid.Height=4; invalid.MipLevels=invalid.ArraySize=1;
                invalid.Format=DXGI_FORMAT_R8G8B8A8_UNORM; invalid.SampleDesc.Count=1;
                if(shape==0) { invalid.Format=DXGI_FORMAT_R8_UNORM; }
                if(shape==1) { invalid.MipLevels=2; }
                if(shape==2) { invalid.ArraySize=2; }
                if(shape==3) { invalid.SampleDesc.Count=4; invalid.BindFlags=D3D11_BIND_RENDER_TARGET; }
                Microsoft::WRL::ComPtr<ID3D11Texture2D> unsupported;
                Check(Device->CreateTexture2D(&invalid,nullptr,&unsupported),"Create unsupported source shape");
                Reject(processor,preview,unsupported.Get(),4,4);
            }
            VerifyCallerState(target.Get());
            Microsoft::WRL::ComPtr<ID3D11Device> otherDevice; Microsoft::WRL::ComPtr<ID3D11DeviceContext> otherContext;
            Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,
                &otherDevice,nullptr,&otherContext),"Create foreign device");
            DirectX11TexturePreview foreign(otherDevice.Get(),720,480);
            Reject(processor,foreign,bgra.Get(),4,2);
            bool rejected=false;
            try { DirectX11TexturePreview invalid(Device.Get(),0,480); } catch(const std::invalid_argument&) { rejected=true; }
            Require(rejected,"Zero preview limits must fail explicitly.");
            std::cout << "PASS offscreen texture previews: BGRA/RGBA, crop, bounds, alpha, retention, resize and context isolation\n";
        }
    private:
        /// Creates padded source pixels in the requested channel format and bind configuration.
        Microsoft::WRL::ComPtr<ID3D11Texture2D> CreateSource(UINT width,UINT height,DXGI_FORMAT format,UINT bind,UINT contentWidth,UINT contentHeight,bool uniform=false) {
            std::vector<unsigned char> pixels(width*height*4);
            for(UINT y=0;y<height;y++) { for(UINT x=0;x<width;x++) {
                auto color=uniform ? std::array<int,4>{64,32,16,128} : Color(x,y);
                if(x>=contentWidth || y>=contentHeight) { color={255,0,255,255}; }
                if(format==DXGI_FORMAT_B8G8R8A8_UNORM) { std::swap(color[0],color[2]); }
                for(UINT channel=0;channel<4;channel++) { pixels[(y*width+x)*4+channel]=static_cast<unsigned char>(color[channel]); }
            } }
            D3D11_TEXTURE2D_DESC desc {}; desc.Width=width; desc.Height=height; desc.MipLevels=desc.ArraySize=1;
            desc.Format=format; desc.SampleDesc.Count=1; desc.Usage=D3D11_USAGE_DEFAULT; desc.BindFlags=bind;
            D3D11_SUBRESOURCE_DATA data {pixels.data(),width*4,0};
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            Check(Device->CreateTexture2D(&desc,&data,&texture),"Create synthetic source"); return texture;
        }
        /// Supplies distinct color and alpha channels so channel swaps and unwanted blending cannot pass unnoticed.
        static std::array<int,4> Color(UINT x,UINT y) { return {40+static_cast<int>(x%8)*8,24+static_cast<int>(y%8)*8,16,96+static_cast<int>(x%8)*8}; }
        /// Requires the output to contain exactly the content rectangle, without allocation padding or alpha multiplication.
        void VerifyCrop(DirectX11TexturePreview& preview,UINT width,UINT height) {
            D3D11_TEXTURE2D_DESC desc {}; preview.GetTexture()->GetDesc(&desc);
            Require(desc.Width==width && desc.Height==height && desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM,
                "Small captures must keep native dimensions and use engine RGBA storage.");
            Require(preview.GetShaderResourceView()!=nullptr,"A processed preview must be shader-readable.");
            auto pixels=Read(preview.GetTexture());
            for(UINT y=0;y<height;y++) { for(UINT x=0;x<width;x++) { AssertPixel(pixels,width,x,y,Color(x,y)); } }
        }
        /// Reads a synthetic output after its asynchronous GPU draw completes, only in tests.
        std::vector<unsigned char> Read(ID3D11Texture2D* source) {
            D3D11_TEXTURE2D_DESC desc {}; source->GetDesc(&desc);
            desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ; desc.MiscFlags=0;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
            Check(Device->CreateTexture2D(&desc,nullptr,&staging),"Create preview readback"); Context->CopyResource(staging.Get(),source);
            D3D11_MAPPED_SUBRESOURCE mapped {}; Check(Context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Read preview");
            std::vector<unsigned char> pixels(desc.Width*desc.Height*4);
            for(UINT y=0;y<desc.Height;y++) { std::memcpy(pixels.data()+y*desc.Width*4,static_cast<unsigned char*>(mapped.pData)+y*mapped.RowPitch,desc.Width*4); }
            Context->Unmap(staging.Get(),0); return pixels;
        }
        /// Compares all four unmultiplied RGBA channels within one byte of GPU quantization.
        static void AssertPixel(const std::vector<unsigned char>& pixels,UINT width,UINT x,UINT y,std::array<int,4> expected) {
            for(UINT c=0;c<4;c++) { if(std::abs(static_cast<int>(pixels[(y*width+x)*4+c])-expected[c])>1) {
                throw std::runtime_error("Preview channel mismatch at "+std::to_string(x)+","+std::to_string(y)+" channel "+std::to_string(c));
            } }
        }
        /// Proves representative output, viewport, geometry and blend bindings are unchanged by the isolated GPU pass.
        void VerifyCallerState(ID3D11RenderTargetView* expected) {
            Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target; Context->OMGetRenderTargets(1,&target,nullptr);
            D3D11_VIEWPORT viewport {}; UINT count=1; Context->RSGetViewports(&count,&viewport);
            D3D11_PRIMITIVE_TOPOLOGY topology {}; Context->IAGetPrimitiveTopology(&topology);
            float blend[4] {}; UINT mask=0; Context->OMGetBlendState(nullptr,blend,&mask);
            Require(target.Get()==expected && count==1 && viewport.TopLeftX==2 && viewport.TopLeftY==3 && viewport.Width==4
                && viewport.Height==5 && viewport.MinDepth==0.2f && viewport.MaxDepth==0.8f
                && topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST && mask==0x1234 && blend[0]==0.1f && blend[3]==0.4f,
                "Preview processing must restore the caller's complete context state.");
        }
        /// Requires invalid inputs to fail before mutating caller bindings or producing a substituted image.
        static void Reject(DirectX11PreviewProcessor& processor,DirectX11TexturePreview& preview,ID3D11Texture2D* source,UINT width,UINT height) {
            bool rejected=false; try { processor.Update(preview,source,width,height); } catch(const std::invalid_argument&) { rejected=true; }
            Require(rejected,"Invalid preview integration must throw explicitly.");
        }
        /// Fails the test with its concrete behavioral expectation.
        static void Require(bool condition,const char* message) { if(!condition) { throw std::runtime_error(message); } }
        /// Preserves graphics failures instead of falling back to CPU processing or another device.
        static void Check(HRESULT result,const char* operation) { if(FAILED(result)) { throw std::runtime_error(std::string(operation)+": "+std::to_string(result)); } }
    };
}
/// Runs only offscreen synthetic GPU work and prints failures without starting the player.
int main() {
    try { helengine::windows::TexturePreviewTests tests; tests.Run(); return 0; }
    catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}