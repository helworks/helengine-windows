#include "platform/windows/win32/win32_camera_toon_pass.hpp"
#include "camera_toon_radio_fixture.hpp"
#include "platform/windows/win32/win32_camera_toon_effects_configuration.hpp"
#include <d3dcompiler.h>
#include <array>
#include <chrono>
#include <thread>
#include "platform/windows/win32/win32_camera_toon_shader_source.hpp"
#include "platform/windows/win32/win32_pearl_surface_shader.hpp"
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace helengine::windows {
    /// Exercises the shipped camera-local helper using synthetic geometry and owned offscreen hardware textures.
    class CameraToonTests {
        /// Owns a hardware graphics device without a window or presentation surface.
        Microsoft::WRL::ComPtr<ID3D11Device> Device;
        /// Owns the immediate context used only by these fixtures.
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> Context;
        /// Retains a standard transform buffer in the renderer's existing 304-byte layout.
        Microsoft::WRL::ComPtr<ID3D11Buffer> Transforms;
        /// Supplies the standard packed forward light buffer.
        Microsoft::WRL::ComPtr<ID3D11Buffer> Lights;
        /// Supplies standard base color at register b3.
        Microsoft::WRL::ComPtr<ID3D11Buffer> Base;
        /// Supplies standard emissive color/strength at register b7.
        Microsoft::WRL::ComPtr<ID3D11Buffer> Emission;
        /// Supplies standard roughness at b4 for reflection filtering assertions.
        Microsoft::WRL::ComPtr<ID3D11Buffer> Roughness;
        /// Samples authored diffuse/emissive fixture textures.
        Microsoft::WRL::ComPtr<ID3D11SamplerState> Sampler;
        /// Enables depth testing for overlapping fixture surfaces.
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> Depth;
        /// Draws explicit fixture vertices without winding sensitivity and with multisampling enabled.
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> Rasterizer;
        /// Stores resolved destination bytes after the last owned readback.
        std::vector<unsigned char> Pixels;
        /// Stores the optional explicit output filename for an owned synthetic fixture image.
        std::string OutputPath;
        /// Stores the fixed destination row width used by pixel assertions.
        static constexpr UINT DestinationWidth = 96;
        /// Stores the fixed destination height independent from the tested viewport size.
        static constexpr UINT DestinationHeight = 80;

    public:
        /// Builds an actual hardware device and the fixed standard-material fixture bindings.
        explicit CameraToonTests(const std::string& outputPath = "") : OutputPath(outputPath) {
            Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                nullptr, 0, D3D11_SDK_VERSION, &Device, nullptr, &Context), "Create toon test hardware device");
            Transforms = CreateBuffer(304); Lights = CreateBuffer(288); Base = CreateBuffer(16); Emission = CreateBuffer(16); Roughness = CreateBuffer(16);
            D3D11_SAMPLER_DESC sampler {};
            sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
            sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler.MaxLOD = D3D11_FLOAT32_MAX;
            Check(Device->CreateSamplerState(&sampler, &Sampler), "Create fixture sampler");
            D3D11_DEPTH_STENCIL_DESC depth {};
            depth.DepthEnable = TRUE; depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; depth.DepthFunc = D3D11_COMPARISON_LESS;
            Check(Device->CreateDepthStencilState(&depth, &Depth), "Create fixture depth state");
            D3D11_RASTERIZER_DESC raster {};
            raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = D3D11_CULL_NONE;
            raster.MultisampleEnable = TRUE; raster.DepthClipEnable = TRUE;
            Check(Device->CreateRasterizerState(&raster, &Rasterizer), "Create fixture rasterizer");
        }

        /// Measures the exact camera pass initialization without creating a window, scene or presentation surface.
        void MeasureShaderStartup() {
            auto start=std::chrono::steady_clock::now();
            Win32CameraToonPass pass(Device.Get(),4,2,3);
            double milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            std::cout << "Clock shader startup(ms): " << milliseconds << std::endl;
            Require(milliseconds<500,"Clock shader construction blocked the window-equivalent thread for at least500ms.");
            if(!pass.TryPrepare()) {
                auto destination=CreateDestination();
                Context->OMSetRenderTargets(1,destination.GetAddressOf(),nullptr);
                bool rejected=false;
                try { pass.Begin(Context.Get(),D3D11_VIEWPORT {0,0,64,48,0,1},4); }
                catch(const std::logic_error&) { rejected=true; }
                Require(rejected,"Pending shaders allowed geometry to bind incomplete GPU state.");
                Microsoft::WRL::ComPtr<ID3D11RenderTargetView> preserved; Context->OMGetRenderTargets(1,&preserved,nullptr);
                Require(preserved.Get()==destination.Get(),"A pending camera modified the following camera's render target.");
            }
            WaitForShaders(pass);
            std::cout << "Clock shaders became ready without blocking construction.\n";
            TestAsyncCompilation();
        }

        /// Verifies settings, independent outlines, lighting, emission, depth, alpha, target resize and context restoration.
        void Run() {
            TestInvalidSettings();
            Win32CameraToonPass pass(Device.Get(), 3.0f, 1.0f, 3);
            WaitForShaders(pass);
            for (UINT samples : { 4u, 1u }) {
                TestSelectedEffects(samples);
                TestPearlSurface(samples);
                TestShadowDirectOnly(samples);
                TestPartIdentity(pass, samples);
                TestOutlineAndEmission(pass, samples);
                TestLightingBands(pass, samples);
                TestInternalEdges(pass, samples);
                TestNarrowRibAndDepth(pass, samples);
                TestSubpixelStrokeWidths(pass, samples);
                TestAuthoredAlpha(pass,samples);
                TestOverlap(pass, samples);
                TestSmallAndMovedViewport(pass, samples);
                TestObliqueCoverage(pass, samples);
                TestComposedContourCoverage(pass, samples);
                TestFollowingStandardPass(pass, samples);
                std::cout << "Camera toon hardware " << samples << "x: emission, album color, alpha, outside/inside widths, bands, depth, resize and state passed.\n";
            }
            TestViewportGpuTime(pass);
        }

    private:
        /// Waits only inside an offscreen fixture; production rendering always uses nonblocking readiness polls.
        static void WaitForShaders(Win32CameraToonPass& pass) {
            auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
            while(!pass.TryPrepare()) {
                Require(std::chrono::steady_clock::now()<deadline,"Background camera shaders did not finish within the fixture timeout.");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        /// Verifies parallel worker identity, complete publication, compiler errors and cancellation without a window.
        void TestAsyncCompilation() {
            DWORD callingThread=GetCurrentThreadId();
            DirectX11ShaderCompilation compilation(BuildCameraToonShaderRequests(std::string(PearlSurfaceShader)+CameraToonGeometryShader+CameraToonCompositeShader));
            DirectX11ShaderCompilation::Bytecodes bytecodes;
            if(!compilation.TryGetBytecodes(bytecodes)) {
                for(const auto& code : bytecodes) Require(!code,"A pending poll modified the caller's result array.");
            }
            auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
            while(!compilation.TryGetBytecodes(bytecodes)) {
                Require(std::chrono::steady_clock::now()<deadline,"Parallel shader compilation timed out.");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            for(const auto& code : bytecodes) Require(code && code->GetBufferSize()>0,"Ready publication contains an empty shader entry.");
            for(DWORD thread : compilation.WorkerThreads()) Require(thread!=0 && thread!=callingThread,"A shader was compiled on the window-equivalent caller thread.");
            Require(compilation.MaximumConcurrentCompilations()>1,"Independent shader entries did not compile in parallel.");
            DirectX11ShaderCompilation invalid(BuildCameraToonShaderRequests("this is deliberately invalid HLSL;"));
            deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
            bool failed=false;
            while(!failed && std::chrono::steady_clock::now()<deadline) {
                try { invalid.TryGetBytecodes(bytecodes); }
                catch(const std::runtime_error& error) { failed=std::string(error.what()).find("DirectX11 shader CameraToon")!=std::string::npos; }
                if(!failed) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            Require(failed,"Background compiler errors were hidden instead of rethrown on the caller.");
            auto start=std::chrono::steady_clock::now();
            {
                Win32CameraToonPass cancelled(Device.Get(),4,2,3);
            }
            double milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            Require(milliseconds<500,"Removing a pending model waited for shader compilation.");
            auto running=std::make_unique<DirectX11ShaderCompilation>(BuildCameraToonShaderRequests(std::string(PearlSurfaceShader)+CameraToonGeometryShader+CameraToonCompositeShader));
            deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
            while(running->MaximumConcurrentCompilations()==0) {
                Require(std::chrono::steady_clock::now()<deadline,"Cancellation fixture never started a compiler worker.");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            start=std::chrono::steady_clock::now(); running.reset();
            milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            Require(milliseconds<500,"Cancelling after a worker started blocked the caller.");
            std::cout << "Parallel worker identity, complete publication, failure reporting and nonblocking cancellation passed.\n";
        }
        /// Throws immediately when a GPU operation fails rather than treating unsupported hardware as a pass.
        static void Check(HRESULT result, const char* message) {
            if (FAILED(result)) throw std::runtime_error(std::string(message) + " HRESULT " + std::to_string(result));
        }
        /// Fails a geometry or restoration assertion with a concrete fixture-specific reason.
        static void Require(bool value, const char* message) {
            if (!value) throw std::runtime_error(message);
        }
        /// Creates one correctly aligned standard constant buffer.
        Microsoft::WRL::ComPtr<ID3D11Buffer> CreateBuffer(UINT bytes) {
            D3D11_BUFFER_DESC descriptor {};
            descriptor.ByteWidth = bytes; descriptor.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
            Check(Device->CreateBuffer(&descriptor, nullptr, &buffer), "Create fixture constant buffer");
            return buffer;
        }
        /// Creates a two-color album fixture whose exact RGB values expose unintended color quantization.
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> CreateAlbum(bool white = false) {
            const unsigned char album[] = { 51, 102, 153, 255, 204, 77, 26, 255 };
            const unsigned char whitePixels[] = { 255, 255, 255, 255, 255, 255, 255, 255 };
            D3D11_TEXTURE2D_DESC descriptor {};
            descriptor.Width = 2; descriptor.Height = 1; descriptor.MipLevels = 1; descriptor.ArraySize = 1;
            descriptor.Format = DXGI_FORMAT_R8G8B8A8_UNORM; descriptor.SampleDesc.Count = 1;
            descriptor.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA initial { white ? whitePixels : album, 8, 0 };
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
            Check(Device->CreateTexture2D(&descriptor, &initial, &texture), "Create album fixture");
            Check(Device->CreateShaderResourceView(texture.Get(), nullptr, &view), "Create album fixture view");
            return view;
        }
        /// Creates an entirely owned RGBA destination initialized to transparent background.
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> CreateDestination(UINT samples = 1) {
            D3D11_TEXTURE2D_DESC descriptor {};
            descriptor.Width = DestinationWidth; descriptor.Height = DestinationHeight;
            descriptor.MipLevels = 1; descriptor.ArraySize = 1; descriptor.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            descriptor.SampleDesc.Count = samples; descriptor.BindFlags = D3D11_BIND_RENDER_TARGET;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            Microsoft::WRL::ComPtr<ID3D11RenderTargetView> view;
            Check(Device->CreateTexture2D(&descriptor, nullptr, &texture), "Create fixture destination");
            Check(Device->CreateRenderTargetView(texture.Get(), nullptr, &view), "Create fixture destination view");
            const float transparent[] = { 0, 0, 0, 0 };
            Context->ClearRenderTargetView(view.Get(), transparent);
            return view;
        }
        /// Restores fixture material defaults and uploads identity transforms using the existing native shader ABI.
        void PrepareMaterial(bool emissive, float normalZ, float normalX = 0.0f, bool whiteDiffuse = false, bool receivesShadows = false) {
            std::array<float, 76> transforms {};
            for (UINT matrix = 0; matrix < 3; ++matrix) {
                for (UINT diagonal = 0; diagonal < 4; ++diagonal) transforms[matrix * 16 + diagonal * 5] = 1.0f;
            }
            transforms[50] = 2.0f; transforms[53] = emissive ? 1.0f : 0.0f;
            transforms[52] = receivesShadows ? 1.0f : 0.0f;
            Context->UpdateSubresource(Transforms.Get(), 0, nullptr, transforms.data(), 0, 0);
            std::array<float, 72> lights {};
            if (!emissive) {
                lights[4] = 1.0f;
                lights[8] = lights[9] = lights[10] = 3.14159265f;
                lights[12] = -normalX; lights[14] = -normalZ;
            }
            Context->UpdateSubresource(Lights.Get(), 0, nullptr, lights.data(), 0, 0);
            const float base[] = { 1, 1, 1, 1 };
            const float emission[] = { 0, 0, 0, 0 };
            Context->UpdateSubresource(Base.Get(), 0, nullptr, base, 0, 0);
            Context->UpdateSubresource(Emission.Get(), 0, nullptr, emission, 0, 0);
            ID3D11Buffer* transformsBuffer = Transforms.Get(); Context->VSSetConstantBuffers(0, 1, &transformsBuffer); Context->PSSetConstantBuffers(0, 1, &transformsBuffer);
            ID3D11Buffer* lightBuffer = Lights.Get(); Context->PSSetConstantBuffers(1, 1, &lightBuffer);
            ID3D11Buffer* baseBuffer = Base.Get(); Context->PSSetConstantBuffers(3, 1, &baseBuffer);
            ID3D11Buffer* emissionBuffer = Emission.Get(); Context->PSSetConstantBuffers(7, 1, &emissionBuffer);
            auto album = CreateAlbum(); auto diffuse = whiteDiffuse ? CreateAlbum(true) : album;
            ID3D11ShaderResourceView* diffuseView = diffuse.Get(); ID3D11ShaderResourceView* albumView = album.Get();
            Context->PSSetShaderResources(0, 1, &diffuseView); Context->PSSetShaderResources(7, 1, &albumView);
            ID3D11SamplerState* sampler = Sampler.Get(); Context->PSSetSamplers(0, 1, &sampler); Context->PSSetSamplers(7, 1, &sampler);
            Context->RSSetState(Rasterizer.Get()); Context->OMSetDepthStencilState(Depth.Get(), 0);
            Context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFFu);
        }
        /// Draws two coplanar triangles sharing normal data; no triangle boundary should be outlined.
        void DrawRectangle(Win32CameraToonPass& pass, float left, float top, float right, float bottom, float normalX = 0.0f, float normalZ = 1.0f, float depth = 0.5f, float reflectivity = 0.0f, float bloomWeight = 0.0f, const void* material = nullptr) {
            const float vertices[] = {
                left,top,depth, normalX,0,normalZ, 0,0, right,top,depth, normalX,0,normalZ, 1,0, left,bottom,depth, normalX,0,normalZ, 0,1,
                left,bottom,depth, normalX,0,normalZ, 0,1, right,top,depth, normalX,0,normalZ, 1,0, right,bottom,depth, normalX,0,normalZ, 1,1
            };
            D3D11_BUFFER_DESC descriptor {};
            descriptor.ByteWidth = sizeof(vertices); descriptor.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA initial { vertices, 0, 0 };
            Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer;
            Check(Device->CreateBuffer(&descriptor, &initial, &vertexBuffer), "Create geometric fixture vertices");
            ID3D11Buffer* buffer = vertexBuffer.Get(); UINT stride = 32, offset = 0;
            Context->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);
            Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            if(material!=nullptr) pass.BindRegisteredGeometry(Context.Get(),material);
            else pass.BindGeometry(Context.Get(), reflectivity, bloomWeight);
            Context->Draw(6, 0);
        }
        /// Proves selected reflection filtering, emissive glow exclusion, valid alpha and effect opt-out on real 1x/4x targets.
        void TestSelectedEffects(UINT samples) {
            Win32CameraToonPass pass(Device.Get(),0,0,3);
            WaitForShaders(pass);
            CameraToonEffectsConfiguration effects;
            effects.ToonEnabled=false; effects.ReflectionsEnabled=true; effects.ReflectionStrength=1;
            effects.BloomEnabled=true; effects.BloomStrength=1; effects.BloomThreshold=0.1f; effects.BloomRadius=12;
            pass.ConfigureEffects(effects);
            int reflectiveMaterial=0,emissiveMaterial=0,albumMaterial=0;
            pass.ConfigureMaterialEffects(&reflectiveMaterial,1,0);
            pass.ConfigureMaterialEffects(&emissiveMaterial,0,1);
            std::vector<uint8_t> environment(32*16*4,255);
            for (UINT y=0;y<16;++y) for (UINT x=0;x<32;++x) {
                UINT offset=(y*32+x)*4;
                environment[offset]=environment[offset+1]=environment[offset+2]=(x>=15 && x<=17) ? 255 : 0;
            }
            pass.SetEnvironment(Context.Get(),32,16,environment.data(),environment.size());
            D3D11_VIEWPORT viewport {7,9,64,48,0,1};
            auto destination=CreateDestination(samples);
            PrepareMaterial(false,1,0,true);
            const float black[] {0,0,0,1}; Context->UpdateSubresource(Base.Get(),0,nullptr,black,0,0);
            const float smooth[] {0,0,0,0}; Context->UpdateSubresource(Roughness.Get(),0,nullptr,smooth,0,0);
            ID3D11Buffer* roughness=Roughness.Get(); Context->PSSetConstantBuffers(4,1,&roughness);
            auto roughnessTexture=CreateAlbum(true); ID3D11ShaderResourceView* roughnessView=roughnessTexture.Get();
            Context->PSSetShaderResources(6,1,&roughnessView); ID3D11SamplerState* roughnessSampler=Sampler.Get(); Context->PSSetSamplers(6,1,&roughnessSampler);
            pass.Begin(Context.Get(),viewport,samples);
            DrawRectangle(pass,-0.5f,0.5f,0.5f,-0.5f,0,1,0.5f,0,0,&reflectiveMaterial);
            pass.Composite(Context.Get(),destination.Get(),nullptr,viewport); Readback(destination.Get());
            UINT center=((9+24)*DestinationWidth+7+32)*4;
            int reflected=Pixels[center];
            Require(reflected>150,"Registered smooth material did not reflect the uploaded environment.");
            destination=CreateDestination(samples);
            const float rough[] {1,0,0,0}; Context->UpdateSubresource(Roughness.Get(),0,nullptr,rough,0,0);
            pass.Begin(Context.Get(),viewport,samples);
            DrawRectangle(pass,-0.5f,0.5f,0.5f,-0.5f,0,1,0.5f,0,0,&reflectiveMaterial);
            pass.Composite(Context.Get(),destination.Get(),nullptr,viewport); Readback(destination.Get());
            Require(Pixels[center]+40<reflected,"Standard material roughness did not filter environment reflection.");
            pass.RemoveMaterialEffects(&reflectiveMaterial); destination=CreateDestination(samples);
            pass.Begin(Context.Get(),viewport,samples);
            DrawRectangle(pass,-0.5f,0.5f,0.5f,-0.5f,0,1,0.5f,0,0,&reflectiveMaterial);
            pass.Composite(Context.Get(),destination.Get(),nullptr,viewport); Readback(destination.Get());
            Require(Pixels[center]==0,"Removed material identity retained borrowed reflection eligibility.");
            destination=CreateDestination(samples); PrepareMaterial(true,1);
            auto glowSentinel=CreateAlbum(true);
            ID3D11ShaderResourceView* savedGlowViews[] {glowSentinel.Get(),glowSentinel.Get(),glowSentinel.Get()};
            Context->PSSetShaderResources(11,3,savedGlowViews);
            ID3D11SamplerState* savedGlowSampler=Sampler.Get(); Context->PSSetSamplers(8,1,&savedGlowSampler);
            pass.Begin(Context.Get(),viewport,samples);
            DrawRectangle(pass,-0.125f,0.125f,0.125f,-0.125f,0,1,0.5f,0,0,&emissiveMaterial);
            pass.Composite(Context.Get(),destination.Get(),nullptr,viewport); Readback(destination.Get());
            for(UINT slot=11;slot<=13;++slot) {
                Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> restored;
                Context->PSGetShaderResources(slot,1,&restored);
                Require(restored.Get()==glowSentinel.Get(),"Enabled bloom did not restore a prior glow/environment resource binding.");
            }
            Microsoft::WRL::ComPtr<ID3D11SamplerState> restoredGlowSampler;
            Context->PSGetSamplers(8,1,&restoredGlowSampler);
            Require(restoredGlowSampler.Get()==Sampler.Get(),"Enabled bloom leaked its linear sampler.");
            ID3D11ShaderResourceView* emptyGlowViews[3] {}; Context->PSSetShaderResources(11,3,emptyGlowViews);
            savedGlowSampler=nullptr; Context->PSSetSamplers(8,1,&savedGlowSampler);
            UINT halo=((9+24)*DestinationWidth+7+39)*4;
            Require(Pixels[halo+3]>0,"Selected emissive material did not produce glow outside its geometry.");
            for (UINT y=0;y<DestinationHeight;++y) for (UINT x=0;x<DestinationWidth;++x) {
                UINT offset=(y*DestinationWidth+x)*4;
                Require(Pixels[offset]<=Pixels[offset+3] && Pixels[offset+1]<=Pixels[offset+3] && Pixels[offset+2]<=Pixels[offset+3],"Glow violated premultiplied-alpha bounds.");
                if(x<7 || x>=71 || y<9 || y>=57) Require(Pixels[offset+3]==0,"Glow escaped the camera viewport.");
            }
            destination=CreateDestination(samples); PrepareMaterial(true,1);
            pass.Begin(Context.Get(),viewport,samples);
            DrawRectangle(pass,-0.125f,0.125f,0.125f,-0.125f,0,1,0.5f,0,0,&albumMaterial);
            pass.Composite(Context.Get(),destination.Get(),nullptr,viewport); Readback(destination.Get());
            Require(Pixels[halo+3]==0,"Unregistered album emission entered bloom.");
            Require(std::abs(static_cast<int>(Pixels[center])-204)<=1,"Unregistered album color changed with enabled effects.");
            effects.ReflectionsEnabled=false; effects.BloomEnabled=false; pass.ConfigureEffects(effects);
            destination=CreateDestination(samples); PrepareMaterial(true,1);
            pass.Begin(Context.Get(),viewport,samples); DrawRectangle(pass,-0.125f,0.125f,0.125f,-0.125f,0,1,0.5f,1,1);
            pass.Composite(Context.Get(),destination.Get(),nullptr,viewport); Readback(destination.Get());
            Require(Pixels[halo+3]==0,"Disabled bloom retained a previous-frame glow.");
        }
        /// Proves a separate registered pearl shader changes angular color and direct highlights without touching LCDs or opt-out draws.
        void TestPearlSurface(UINT samples) {
            Win32CameraToonPass pass(Device.Get(),0,0,3);
            WaitForShaders(pass);
            CameraToonEffectsConfiguration effects; effects.ToonEnabled=false;
            effects.PearlEnabled=true; effects.PearlStrength=0.8f; effects.PearlHighlight=0.55f; effects.PearlWidth=0.45f;
            pass.ConfigureEffects(effects);
            int shell=0,screen=0; pass.ConfigureMaterialEffects(&shell,0,0,true);
            D3D11_VIEWPORT viewport {7,9,64,48,0,1};
            const float lilac[] {0.43f,0.19f,0.76f,1};
            PrepareMaterial(false,1,0,true); Context->UpdateSubresource(Base.Get(),0,nullptr,lilac,0,0);
            const float smooth[] {0.1f,0,0,0}; Context->UpdateSubresource(Roughness.Get(),0,nullptr,smooth,0,0);
            ID3D11Buffer* roughness=Roughness.Get(); Context->PSSetConstantBuffers(4,1,&roughness);
            auto white=CreateAlbum(true); ID3D11ShaderResourceView* whiteView=white.Get(); Context->PSSetShaderResources(6,1,&whiteView);
            ID3D11SamplerState* sampler=Sampler.Get(); Context->PSSetSamplers(6,1,&sampler);
            UINT center=((9+24)*DestinationWidth+7+32)*4;
            auto destination=CreateDestination(samples);
            pass.Begin(Context.Get(),viewport,samples); DrawRectangle(pass,-0.5f,0.5f,0.5f,-0.5f,0,1,0.5f,0,0,&shell);
            pass.Composite(Context.Get(),destination.Get(),nullptr,viewport); Readback(destination.Get());
            std::array<unsigned char,4> pearl {Pixels[center],Pixels[center+1],Pixels[center+2],Pixels[center+3]};
            destination=CreateDestination(samples);
            pass.Begin(Context.Get(),viewport,samples); DrawRectangle(pass,-0.5f,0.5f,0.5f,-0.5f,0,1,0.5f,0,0,&screen);
            pass.Composite(Context.Get(),destination.Get(),nullptr,viewport); Readback(destination.Get());
            std::array<unsigned char,4> ordinary {Pixels[center],Pixels[center+1],Pixels[center+2],Pixels[center+3]};
            Require(pearl[0]>ordinary[0]+20 && pearl[1]>ordinary[1]+20,"Pearl shell lacks a clearly visible direct-light highlight.");
            Require(std::abs(static_cast<int>(ordinary[0])-110)<=1 && std::abs(static_cast<int>(ordinary[1])-48)<=1,"Unregistered LCD was changed by pearl shading.");
            effects.PearlHighlight=0; pass.ConfigureEffects(effects); destination=CreateDestination(samples);
            pass.Begin(Context.Get(),viewport,samples); DrawRectangle(pass,-0.5f,0.5f,0.5f,-0.5f,0,1,0.5f,0,0,&shell);
            pass.Composite(Context.Get(),destination.Get(),nullptr,viewport); Readback(destination.Get());
            int frontalGreen=Pixels[center+1]; int frontalBlue=Pixels[center+2];
            PrepareMaterial(false,0.6f,0.8f,true); Context->UpdateSubresource(Base.Get(),0,nullptr,lilac,0,0);
            destination=CreateDestination(samples);
            pass.Begin(Context.Get(),viewport,samples); DrawRectangle(pass,-0.5f,0.5f,0.5f,-0.5f,0.8f,0.6f,0.5f,0,0,&shell);
            pass.Composite(Context.Get(),destination.Get(),nullptr,viewport); Readback(destination.Get());
            Require(std::abs(static_cast<int>(Pixels[center+1])-frontalGreen)+std::abs(static_cast<int>(Pixels[center+2])-frontalBlue)>15,"Pearl angular color does not change as a surface turns.");
            effects.PearlStrength=0; effects.PearlHighlight=0.3f; effects.PearlWidth=0; pass.ConfigureEffects(effects);
            PrepareMaterial(false,1,0,true); Context->UpdateSubresource(Base.Get(),0,nullptr,lilac,0,0);
            const float polished[] {0,0,0,0}; Context->UpdateSubresource(Roughness.Get(),0,nullptr,polished,0,0);
            destination=CreateDestination(samples); Context->PSSetShader(nullptr,nullptr,0);
            pass.Begin(Context.Get(),viewport,samples); DrawRectangle(pass,-0.5f,0.5f,0.5f,-0.5f,0.6f,0.8f,0.5f,0,0,&shell);
            pass.Composite(Context.Get(),destination.Get(),nullptr,viewport); Readback(destination.Get());
            int narrow=Pixels[center+1];
            Microsoft::WRL::ComPtr<ID3D11PixelShader> restored; Context->PSGetShader(&restored,nullptr,nullptr);
            Require(!restored,"Pearl geometry leaked its separate pixel shader into the following camera.");
            effects.PearlWidth=1; pass.ConfigureEffects(effects);
            const float rough[] {1,0,0,0}; Context->UpdateSubresource(Roughness.Get(),0,nullptr,rough,0,0);
            destination=CreateDestination(samples);
            pass.Begin(Context.Get(),viewport,samples); DrawRectangle(pass,-0.5f,0.5f,0.5f,-0.5f,0.6f,0.8f,0.5f,0,0,&shell);
            pass.Composite(Context.Get(),destination.Get(),nullptr,viewport); Readback(destination.Get());
            Require(Pixels[center+1]>narrow+8,"Pearl roughness/width did not broaden the off-axis light highlight.");
            effects.PearlStrength=1; effects.PearlHighlight=0; pass.ConfigureEffects(effects);
            PrepareMaterial(false,0.0001f,1,true); Context->UpdateSubresource(Base.Get(),0,nullptr,lilac,0,0);
            destination=CreateDestination(samples);
            pass.Begin(Context.Get(),viewport,samples); DrawRectangle(pass,-0.5f,0.5f,0.5f,-0.5f,1,0.0001f,0.5f,0,0,&shell);
            pass.Composite(Context.Get(),destination.Get(),nullptr,viewport); Readback(destination.Get());
            Require(Pixels[center+3]==255 && Pixels[center+2]>100,"Pearl grazing-angle surface lost finite color or coverage.");
            effects.PearlEnabled=false; pass.ConfigureEffects(effects); destination=CreateDestination(samples);
            PrepareMaterial(false,1,0,true); Context->UpdateSubresource(Base.Get(),0,nullptr,lilac,0,0);
            pass.Begin(Context.Get(),viewport,samples); DrawRectangle(pass,-0.5f,0.5f,0.5f,-0.5f,0,1,0.5f,0,0,&shell);
            pass.Composite(Context.Get(),destination.Get(),nullptr,viewport); Readback(destination.Get());
            for(UINT channel=0;channel<4;++channel) Require(Pixels[center+channel]==ordinary[channel],"Disabled pearl shader did not exactly restore ordinary shading.");
            effects.PearlEnabled=true; effects.PearlHighlight=1; pass.ConfigureEffects(effects); destination=CreateDestination(samples);
            PrepareMaterial(false,1,0,true); std::array<float,72> darkness {}; Context->UpdateSubresource(Lights.Get(),0,nullptr,darkness.data(),0,0); const float black[] {0,0,0,1}; Context->UpdateSubresource(Base.Get(),0,nullptr,black,0,0);
            pass.Begin(Context.Get(),viewport,samples); DrawRectangle(pass,-0.5f,0.5f,0.5f,-0.5f,0,1,0.5f,0,0,&shell);
            pass.Composite(Context.Get(),destination.Get(),nullptr,viewport); Readback(destination.Get());
            Require(Pixels[center]==0 && Pixels[center+1]==0 && Pixels[center+2]==0,"Pearl coating emits light without an illuminating light.");
        }
        /// Verifies the second packed light receives shadow visibility without attenuating ambient or emission.
        void TestShadowDirectOnly(UINT samples) {
            Win32CameraToonPass pass(Device.Get(),0,0,3);
            WaitForShaders(pass);
            CameraToonEffectsConfiguration effects; effects.ToonEnabled=false;
            effects.ShadowsEnabled=true; effects.ShadowStrength=1; effects.ShadowSoftness=1.25f;
            pass.ConfigureEffects(effects);
            auto shadowBuffer=CreateBuffer(400);
            std::array<float,100> shadow {};
            shadow[0]=1; shadow[1]=shadow[2]=1;
            shadow[30]=shadow[31]=1; shadow[32]=shadow[33]=1;
            for (UINT diagonal=0;diagonal<4;++diagonal) shadow[36+diagonal*5]=1;
            Context->UpdateSubresource(shadowBuffer.Get(),0,nullptr,shadow.data(),0,0);
            ID3D11Buffer* shadowBinding=shadowBuffer.Get(); Context->PSSetConstantBuffers(2,1,&shadowBinding);
            float depth=0;
            D3D11_TEXTURE2D_DESC descriptor {}; descriptor.Width=descriptor.Height=descriptor.MipLevels=descriptor.ArraySize=1;
            descriptor.Format=DXGI_FORMAT_R32_FLOAT; descriptor.SampleDesc.Count=1; descriptor.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA initial {&depth,sizeof(float),0};
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
            Check(Device->CreateTexture2D(&descriptor,&initial,&texture),"Create owned shadow depth texture");
            Check(Device->CreateShaderResourceView(texture.Get(),nullptr,&view),"Create owned shadow SRV");
            D3D11_VIEWPORT viewport {7,9,64,48,0,1}; auto destination=CreateDestination(samples);
            PrepareMaterial(false,1,0,true,true);
            std::array<float,72> lights {}; lights[0]=lights[1]=lights[2]=0.2f; lights[4]=2; lights[14]=-1;
            lights[24]=lights[25]=lights[26]=0.6f*3.14159265f; lights[30]=-1;
            Context->UpdateSubresource(Lights.Get(),0,nullptr,lights.data(),0,0);
            const float emission[] {1,1,1,0.1f}; Context->UpdateSubresource(Emission.Get(),0,nullptr,emission,0,0);
            ID3D11ShaderResourceView* binding=view.Get(); Context->PSSetShaderResources(1,1,&binding);
            ID3D11SamplerState* sampler=Sampler.Get(); Context->PSSetSamplers(1,1,&sampler);
            pass.Begin(Context.Get(),viewport,samples); DrawRectangle(pass,-0.5f,0.5f,0.5f,-0.5f);
            pass.Composite(Context.Get(),destination.Get(),nullptr,viewport); Readback(destination.Get());
            ExpectChannel(7+32,9+24,0,77,"Shadow visibility removed ambient/emission or ignored the second light slot.");
            effects.ShadowsEnabled=false; pass.ConfigureEffects(effects); destination=CreateDestination(samples);
            pass.Begin(Context.Get(),viewport,samples); DrawRectangle(pass,-0.5f,0.5f,0.5f,-0.5f);
            pass.Composite(Context.Get(),destination.Get(),nullptr,viewport); Readback(destination.Get());
            ExpectChannel(7+32,9+24,0,230,"Disabling shadows failed to restore direct lighting.");
        }
        /// Copies only the fixture-owned destination to CPU memory for deterministic pixel assertions.
        void Readback(ID3D11RenderTargetView* destination) {
            Microsoft::WRL::ComPtr<ID3D11Resource> resource; destination->GetResource(&resource);
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture; Check(resource.As(&texture), "Get fixture destination texture");
            D3D11_TEXTURE2D_DESC descriptor {}; texture->GetDesc(&descriptor);
            Context->OMSetRenderTargets(0, nullptr, nullptr);
            if (descriptor.SampleDesc.Count > 1) {
                descriptor.SampleDesc.Count = 1;
                Microsoft::WRL::ComPtr<ID3D11Texture2D> resolved;
                Check(Device->CreateTexture2D(&descriptor, nullptr, &resolved), "Create owned multisample destination resolve");
                Context->ResolveSubresource(resolved.Get(), 0, texture.Get(), 0, descriptor.Format);
                texture = resolved;
            }
            descriptor.BindFlags = 0; descriptor.Usage = D3D11_USAGE_STAGING; descriptor.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
            Check(Device->CreateTexture2D(&descriptor, nullptr, &staging), "Create fixture readback");
            Context->OMSetRenderTargets(0, nullptr, nullptr); Context->CopyResource(staging.Get(), texture.Get());
            D3D11_MAPPED_SUBRESOURCE mapped {}; Check(Context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map owned toon fixture");
            Pixels.resize(DestinationWidth * DestinationHeight * 4);
            for (UINT row = 0; row < DestinationHeight; ++row) std::memcpy(Pixels.data() + row * DestinationWidth * 4,
                static_cast<const unsigned char*>(mapped.pData) + row * mapped.RowPitch, DestinationWidth * 4);
            Context->Unmap(staging.Get(), 0);
        }
        /// Compares a single channel with an explicit one-byte rounding tolerance.
        void ExpectChannel(UINT x, UINT y, UINT channel, int expected, const char* message) {
            int actual = Pixels[(y * DestinationWidth + x) * 4 + channel];
            if (std::abs(actual - expected) > 1) throw std::runtime_error(std::string(message) + " actual=" + std::to_string(actual) + " expected=" + std::to_string(expected));
        }
        /// Verifies exact album emission, silhouette width, transparent exterior and the absence of diagonal triangle lines.
        void TestOutlineAndEmission(Win32CameraToonPass& pass, UINT samples) {
            D3D11_VIEWPORT viewport { 7, 9, 64, 48, 0, 1 };
            auto destination = CreateDestination(samples); PrepareMaterial(true, 1, 0, true);
            auto sentinel=CreateAlbum(true);
            ID3D11ShaderResourceView* priorResources[] = {sentinel.Get(),sentinel.Get(),sentinel.Get(),sentinel.Get(),sentinel.Get(),sentinel.Get()};
            Context->PSSetShaderResources(8,6,priorResources);
            ID3D11SamplerState* priorSampler=Sampler.Get(); Context->PSSetSamplers(8,1,&priorSampler);
            ID3D11Buffer* priorParameters=Emission.Get();
            Context->PSSetConstantBuffers(8,1,&priorParameters); Context->VSSetConstantBuffers(8,1,&priorParameters);
            Microsoft::WRL::ComPtr<ID3D11VertexShader> priorVertex;
            Microsoft::WRL::ComPtr<ID3D11PixelShader> priorPixel;
            Microsoft::WRL::ComPtr<ID3D11InputLayout> priorLayout;
            Context->VSGetShader(&priorVertex,nullptr,nullptr); Context->PSGetShader(&priorPixel,nullptr,nullptr);
            Context->IAGetInputLayout(&priorLayout);
            pass.Configure(3, 0, 3); pass.Begin(Context.Get(), viewport, samples);
            DrawRectangle(pass, -0.5f, 0.5f, 0.5f, -0.5f);
            pass.Composite(Context.Get(), destination.Get(), nullptr, viewport);
            Microsoft::WRL::ComPtr<ID3D11BlendState> blend; Context->OMGetBlendState(&blend, nullptr, nullptr);
            Require(blend == nullptr, "Toon pass leaked its blend state into subsequent cameras.");
            Microsoft::WRL::ComPtr<ID3D11Buffer> parameters;
            Context->PSGetConstantBuffers(8,1,&parameters);
            Require(parameters.Get()==Emission.Get(), "Toon pass did not restore its prior pixel parameter binding.");
            parameters.Reset(); Context->VSGetConstantBuffers(8,1,&parameters);
            Require(parameters.Get()==Emission.Get(), "Toon pass did not restore its prior vertex parameter binding.");
            for (UINT slot=8;slot<=13;++slot) {
                Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> restored;
                Context->PSGetShaderResources(slot,1,&restored);
                Require(restored.Get()==sentinel.Get(), "Toon pass did not restore a reserved resource binding.");
            }
            Microsoft::WRL::ComPtr<ID3D11SamplerState> restoredSampler; Context->PSGetSamplers(8,1,&restoredSampler);
            Require(restoredSampler.Get()==Sampler.Get(),"Camera effects leaked their linear sampler into a following camera.");
            Microsoft::WRL::ComPtr<ID3D11VertexShader> restoredVertex;
            Microsoft::WRL::ComPtr<ID3D11PixelShader> restoredPixel;
            Microsoft::WRL::ComPtr<ID3D11InputLayout> restoredLayout;
            Context->VSGetShader(&restoredVertex,nullptr,nullptr); Context->PSGetShader(&restoredPixel,nullptr,nullptr);
            Context->IAGetInputLayout(&restoredLayout);
            Require(restoredVertex.Get()==priorVertex.Get() && restoredPixel.Get()==priorPixel.Get()
                && restoredLayout.Get()==priorLayout.Get(), "Toon pass did not restore the pipeline from before Begin.");
            ID3D11ShaderResourceView* emptyResources[6] {};
            Context->PSSetShaderResources(8,6,emptyResources);
            ID3D11Buffer* emptyParameters=nullptr;
            Context->PSSetConstantBuffers(8,1,&emptyParameters); Context->VSSetConstantBuffers(8,1,&emptyParameters);
            Readback(destination.Get());
            ExpectChannel(7+13, 9+24, 3, 255, "Outside silhouette did not extend three pixels.");
            ExpectChannel(7+12, 9+24, 3, 0, "Outside silhouette exceeded three pixels.");
            ExpectChannel(7+22, 9+24, 0, 51, "Emissive album red was quantized.");
            ExpectChannel(7+22, 9+24, 1, 102, "Emissive album green was quantized.");
            ExpectChannel(7+42, 9+24, 0, 204, "Second album texel was changed.");
            ExpectChannel(7+32, 9+24, 3, 255, "Coplanar triangle seam lost coverage.");
            ExpectChannel(0, 0, 3, 0, "Toon composition changed pixels outside the viewport.");
            ExpectChannel(7+5, 9+5, 3, 0, "Alpha outside geometry and outlines was not transparent.");
            if (samples == 4 && !OutputPath.empty()) SaveOwnedFixture(OutputPath);
        }
        /// Writes only synthetic fixture pixels as a bitmap, composited over gray to expose transparent margins.
        void SaveOwnedFixture(const std::string& path) {
            BITMAPFILEHEADER header {};
            header.bfType = 0x4d42; header.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
            header.bfSize = header.bfOffBits + static_cast<DWORD>(Pixels.size());
            BITMAPINFOHEADER info {};
            info.biSize = sizeof(info); info.biWidth = DestinationWidth; info.biHeight = -static_cast<LONG>(DestinationHeight);
            info.biPlanes = 1; info.biBitCount = 32; info.biCompression = BI_RGB;
            std::vector<unsigned char> visible(Pixels.size());
            for (UINT pixel = 0; pixel < DestinationWidth * DestinationHeight; ++pixel) {
                int inverseAlpha = 255 - Pixels[pixel * 4 + 3];
                visible[pixel*4] = static_cast<unsigned char>(Pixels[pixel*4+2] + (230*inverseAlpha+127)/255);
                visible[pixel*4+1] = static_cast<unsigned char>(Pixels[pixel*4+1] + (230*inverseAlpha+127)/255);
                visible[pixel*4+2] = static_cast<unsigned char>(Pixels[pixel*4] + (230*inverseAlpha+127)/255);
                visible[pixel*4+3] = 255;
            }
            std::ofstream output(path, std::ios::binary);
            if (!output) throw std::runtime_error("Cannot create owned toon fixture bitmap.");
            output.write(reinterpret_cast<const char*>(&header), sizeof(header));
            output.write(reinterpret_cast<const char*>(&info), sizeof(info));
            output.write(reinterpret_cast<const char*>(visible.data()), static_cast<std::streamsize>(visible.size()));
            if (!output) throw std::runtime_error("Cannot write owned toon fixture bitmap.");
        }
        /// Checks three actual diffuse light levels while keeping source color separate from illumination.
        void TestLightingBands(Win32CameraToonPass& pass, UINT samples) {
            D3D11_VIEWPORT viewport { 0, 0, 64, 48, 0, 1 };
            const float cosines[] = { 0.1f, 0.5f, 0.95f };
            const int expected[] = { 0, 26, 51 };
            pass.Configure(0, 0, 3);
            for (UINT band = 0; band < 3; ++band) {
                auto destination = CreateDestination(); PrepareMaterial(false, cosines[band], std::sqrt(1.0f-cosines[band]*cosines[band]));
                pass.Begin(Context.Get(), viewport, samples); DrawRectangle(pass, -0.5f, 0.5f, 0.5f, -0.5f);
                pass.Composite(Context.Get(), destination.Get(), nullptr, viewport); Readback(destination.Get());
                ExpectChannel(22, 24, 0, expected[band], "Three-band diffuse illumination was incorrect.");
            }
        }
        /// Checks centered shared-part strokes and rejects triangle seams within the same authored piece.
        void TestInternalEdges(Win32CameraToonPass& pass, UINT samples) {
            D3D11_VIEWPORT viewport { 0, 0, 64, 48, 0, 1 };
            auto destination = CreateDestination(); PrepareMaterial(false, 1); pass.Configure(0, 1, 3);
            const float white[] = {1,1,1,1}; Context->UpdateSubresource(Emission.Get(),0,nullptr,white,0,0);
            pass.Begin(Context.Get(), viewport, samples);
            DrawRectangle(pass, -0.5f, 0.5f, 0, -0.5f); DrawRectangle(pass, 0, 0.5f, 0.5f, -0.5f);
            pass.Composite(Context.Get(), destination.Get(), nullptr, viewport); Readback(destination.Get());
            double inkArea=0;
            for (UINT x=28;x<=35;++x) inkArea+=(255.0-Pixels[(24*DestinationWidth+x)*4])/252.0;
            Require(std::abs(inkArea-1.0)<0.03, "A shared coplanar interface did not retain one centered pixel of ink.");
            Require(Pixels[(24*DestinationWidth+31)*4]>3 && Pixels[(24*DestinationWidth+31)*4]<255
                && Pixels[(24*DestinationWidth+32)*4]>3 && Pixels[(24*DestinationWidth+32)*4]<255,
                "A boundary at a pixel edge did not distribute its centered stroke over both pixels.");
            ExpectChannel(30, 24, 0, 255, "Inside edge exceeded one physical pixel.");
            ExpectChannel(15, 24, 3, 0, "Inside width incorrectly created an outside silhouette.");
            destination = CreateDestination(); PrepareMaterial(true, 1); pass.Configure(0, 2, 3);
            pass.Begin(Context.Get(), viewport, samples); DrawRectangle(pass, -0.5f, 0.5f, 0.5f, -0.5f);
            pass.Composite(Context.Get(), destination.Get(), nullptr, viewport); Readback(destination.Get());
            ExpectChannel(31, 24, 0, 51, "Coplanar mesh triangle boundary was outlined.");
        }
        /// Requires visible one-pixel seams and solid exterior ink when MSAA straddles a half-pixel boundary.
        void TestSubpixelStrokeWidths(Win32CameraToonPass& pass, UINT samples) {
            if (samples != 4) return;
            D3D11_VIEWPORT viewport { 0, 0, 64, 48, 0, 1 };
            auto destination = CreateDestination(); PrepareMaterial(true, 1); pass.Configure(0, 1, 3);
            pass.Begin(Context.Get(), viewport, samples);
            DrawRectangle(pass, -0.5f, 0.5f, 0.015625f, -0.5f);
            DrawRectangle(pass, 0.015625f, 0.5f, 0.5f, -0.5f, 1, 0);
            pass.Composite(Context.Get(), destination.Get(), nullptr, viewport); Readback(destination.Get());
            UINT inkPixels = 0;
            for (UINT x = 29; x <= 35; ++x) {
                if (Pixels[(24 * DestinationWidth + x) * 4] < 10) ++inkPixels;
            }
            Require(inkPixels == 1, "A half-pixel MSAA part seam lost its one-pixel internal stroke.");
            destination = CreateDestination(); PrepareMaterial(true, 1); pass.Configure(3, 0, 3);
            pass.Begin(Context.Get(), viewport, samples);
            DrawRectangle(pass, -0.484375f, 0.5f, 0.5f, -0.5f);
            pass.Composite(Context.Get(), destination.Get(), nullptr, viewport); Readback(destination.Get());
            ExpectChannel(16, 24, 3, 255, "Outside ink did not fill uncovered samples along the antialiased silhouette.");
            destination = CreateDestination(); PrepareMaterial(true, 1);
            const float translucent[] = { 1,1,1,0.5f };
            Context->UpdateSubresource(Base.Get(), 0, nullptr, translucent, 0, 0);
            pass.Begin(Context.Get(), viewport, samples);
            DrawRectangle(pass, -0.484375f, 0.5f, 0.5f, -0.5f);
            pass.Composite(Context.Get(), destination.Get(), nullptr, viewport); Readback(destination.Get());
            ExpectChannel(22, 24, 3, 128, "Silhouette ink flattened authored material transparency in the surface interior.");
            ExpectChannel(16, 24, 3, 192, "Silhouette ink replaced covered translucent material samples.");
        }
        /// Preserves authored half opacity under shared internal ink for both supported sample counts.
        void TestAuthoredAlpha(Win32CameraToonPass& pass, UINT samples) {
            D3D11_VIEWPORT viewport {0,0,64,48,0,1};
            auto destination=CreateDestination(samples); PrepareMaterial(true,1); pass.Configure(0,2,3);
            const float translucent[] = {1,1,1,0.5f}; Context->UpdateSubresource(Base.Get(),0,nullptr,translucent,0,0);
            pass.Begin(Context.Get(),viewport,samples);
            DrawRectangle(pass,-0.5f,0.5f,0,-0.5f); DrawRectangle(pass,0,0.5f,0.5f,-0.5f);
            pass.Composite(Context.Get(),destination.Get(),nullptr,viewport); Readback(destination.Get());
            ExpectChannel(22,24,3,128,"Toon changed authored surface opacity away from a part interface.");
            ExpectChannel(31,24,3,128,"Internal ink flattened authored surface opacity at a part interface.");
            Require(Pixels[(24*DestinationWidth+31)*4]<10,"Translucent shared-part interface lost its internal ink.");
        }
        /// Preserves both boundaries of narrow authored parts and measures centered physical-pixel strokes.
        void TestNarrowRibAndDepth(Win32CameraToonPass& pass, UINT samples) {
            D3D11_VIEWPORT viewport { 0,0,64,48,0,1 };
            for (float width : { 1.0f,2.0f,1.1f,3.0f }) {
                auto destination = CreateDestination(); PrepareMaterial(false,1); pass.Configure(0,width,3);
                const float white[] = {1,1,1,1}; Context->UpdateSubresource(Emission.Get(),0,nullptr,white,0,0);
                pass.Begin(Context.Get(),viewport,samples);
                DrawRectangle(pass,-0.5f,0.5f,0,-0.5f); DrawRectangle(pass,0,0.5f,0.5f,-0.5f,1,0);
                pass.Composite(Context.Get(),destination.Get(),nullptr,viewport); Readback(destination.Get());
                double inkArea=0;
                for (UINT x=28;x<=35;++x) inkArea+=(255.0-Pixels[(24*DestinationWidth+x)*4])/252.0;
                if (std::abs(inkArea-width)>=0.03) throw std::runtime_error("Internal AA diameter: samples="+std::to_string(samples)+" width="+std::to_string(width)+" area="+std::to_string(inkArea));
            }
            auto destination=CreateDestination(); PrepareMaterial(false,1); pass.Configure(0,1,3);
            const float whiteSeam[] = {1,1,1,1}; Context->UpdateSubresource(Emission.Get(),0,nullptr,whiteSeam,0,0);
            pass.Begin(Context.Get(),viewport,samples);
            DrawRectangle(pass,-0.5f,0.5f,0.015625f,-0.5f,0,1,0.2f);
            DrawRectangle(pass,0.015625f,0.5f,0.5f,-0.5f,0,1,0.8f);
            pass.Composite(Context.Get(),destination.Get(),nullptr,viewport); Readback(destination.Get());
            UINT depthCount=0; double seamInkArea=0;
            for (UINT x=29;x<=35;++x) {
                if (Pixels[(24*DestinationWidth+x)*4]<10) ++depthCount;
                seamInkArea+=(255.0-Pixels[(24*DestinationWidth+x)*4])/252.0;
            }
            Require(std::abs(seamInkArea-1.0)<0.03, "Part seam did not retain one integrated pixel at a half-pixel geometry offset.");
            if (samples==4 && depthCount!=1) throw std::runtime_error("Half-pixel part seam opaque count="+std::to_string(depthCount));
            destination=CreateDestination(); PrepareMaterial(false,1);
            const float whiteRib[] = {1,1,1,1}; Context->UpdateSubresource(Emission.Get(),0,nullptr,whiteRib,0,0);
            pass.Begin(Context.Get(),viewport,samples);
            DrawRectangle(pass,-0.5f,0.5f,0,-0.5f);
            DrawRectangle(pass,0,0.5f,0.0625f,-0.5f,1,0);
            DrawRectangle(pass,0.0625f,0.5f,0.5f,-0.5f);
            pass.Composite(Context.Get(),destination.Get(),nullptr,viewport); Readback(destination.Get());
            double ribInkArea=0;
            for (UINT x=29;x<=36;++x) ribInkArea+=(255.0-Pixels[(24*DestinationWidth+x)*4])/252.0;
            Require(std::abs(ribInkArea-2.0)<0.03, "Thin rib lost one of its two separate centered part boundaries.");
        }
        /// Confirms a later farther surface cannot replace the nearer geometry's material or metadata.
        void TestOverlap(Win32CameraToonPass& pass, UINT samples) {
            D3D11_VIEWPORT viewport { 0, 0, 64, 48, 0, 1 };
            auto destination = CreateDestination(); PrepareMaterial(true, 1); pass.Configure(0, 2, 3);
            pass.Begin(Context.Get(), viewport, samples); DrawRectangle(pass, -0.5f, 0.5f, 0.5f, -0.5f, 0, 1, 0.2f);
            pass.Composite(Context.Get(), destination.Get(), nullptr, viewport); Readback(destination.Get());
            const auto nearPixels=Pixels;
            destination=CreateDestination(); PrepareMaterial(true,1);
            pass.Begin(Context.Get(),viewport,samples); DrawRectangle(pass,-0.5f,0.5f,0.5f,-0.5f,0,1,0.2f);
            const float emission[] = { 1, 0, 0, 1 }; Context->UpdateSubresource(Emission.Get(), 0, nullptr, emission, 0, 0);
            DrawRectangle(pass, -0.5f, 0.5f, 0.5f, -0.5f, 0, 1, 0.8f);
            pass.Composite(Context.Get(), destination.Get(), nullptr, viewport); Readback(destination.Get());
            Require(Pixels==nearPixels, "A fully occluded part changed visible color, coverage or internal ink.");
            ExpectChannel(22, 24, 1, 102, "Farther overlap replaced the near surface despite local depth testing.");
        }
        /// Reuses the same pass at a tiny moved viewport, ensuring target replacement and origin handling stay local.
        void TestSmallAndMovedViewport(Win32CameraToonPass& pass, UINT samples) {
            D3D11_VIEWPORT viewport { 17, 13, 4, 4, 0, 1 };
            auto destination = CreateDestination(); PrepareMaterial(true, 1); pass.Configure(0, 0, 3);
            pass.Begin(Context.Get(), viewport, samples); DrawRectangle(pass, -1, 1, 1, -1);
            pass.Composite(Context.Get(), destination.Get(), nullptr, viewport); Readback(destination.Get());
            ExpectChannel(17, 13, 0, 51, "Moved tiny viewport did not sample its new local target.");
            ExpectChannel(20, 16, 0, 204, "Resized viewport retained old target dimensions.");
            ExpectChannel(16, 13, 3, 0, "Moved viewport contaminated its neighboring pixels.");
        }
        /// Exercises genuinely mixed 4x coverage on an oblique subpixel triangle rather than only aligned rectangles.
        void TestObliqueCoverage(Win32CameraToonPass& pass, UINT samples) {
            D3D11_VIEWPORT viewport { 7, 9, 64, 48, 0, 1 };
            auto destination = CreateDestination(samples); PrepareMaterial(true, 1, 0, true); pass.Configure(0, 0, 3);
            pass.Begin(Context.Get(), viewport, samples);
            const float vertices[] = {
                -0.71f,0.63f,0.5f, 0,0,1, 0,0,
                 0.68f,0.21f,0.5f, 0,0,1, 0,0,
                -0.19f,-0.67f,0.5f, 0,0,1, 0,0
            };
            D3D11_BUFFER_DESC descriptor {}; descriptor.ByteWidth = sizeof(vertices); descriptor.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA initial { vertices, 0, 0 };
            Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer;
            Check(Device->CreateBuffer(&descriptor, &initial, &vertexBuffer), "Create oblique fixture geometry");
            ID3D11Buffer* buffer = vertexBuffer.Get(); UINT stride = 32, offset = 0;
            Context->IASetVertexBuffers(0, 1, &buffer, &stride, &offset); Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            pass.BindGeometry(Context.Get()); Context->Draw(3, 0);
            pass.Composite(Context.Get(), destination.Get(), nullptr, viewport); Readback(destination.Get());
            UINT partialCount = 0;
            for (UINT pixel = 0; pixel < DestinationWidth * DestinationHeight; ++pixel) {
                int alpha = Pixels[pixel * 4 + 3];
                if (alpha > 1 && alpha < 254) {
                    ++partialCount;
                    Require(std::abs(static_cast<int>(Pixels[pixel*4]) - (51*alpha+127)/255) <= 1, "Mixed coverage damaged premultiplied emission color.");
                }
            }
            Require(samples == 4 ? partialCount > 0 : partialCount == 0, "Oblique coverage did not match the requested sample count.");
        }
        /// Draws an owned slanted quad with two differently oriented surfaces sharing an oblique diagonal.
        void DrawContourPanel(Win32CameraToonPass& pass, bool splitNormals = true, bool separateParts = true, bool splitDepth = false) {
            float normalX=splitNormals ? 1.0f : 0.0f, normalZ=splitNormals ? 0.0f : 1.0f;
            float secondDepth=splitDepth ? 0.7f : 0.5f;
            const float vertices[] = {
                -0.65f,0.70f,0.5f, 0,0,1, 0,0, 0.73f,0.26f,0.5f, 0,0,1, 1,0, 0.45f,-0.68f,0.5f, 0,0,1, 1,1,
                0.45f,-0.68f,secondDepth, normalX,0,normalZ, 1,1, -0.83f,-0.24f,secondDepth, normalX,0,normalZ, 0,1, -0.65f,0.70f,secondDepth, normalX,0,normalZ, 0,0
            };
            D3D11_BUFFER_DESC descriptor {}; descriptor.ByteWidth=sizeof(vertices); descriptor.BindFlags=D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA initial {vertices,0,0};
            Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
            Check(Device->CreateBuffer(&descriptor,&initial,&buffer), "Create owned contour panel");
            ID3D11Buffer* geometry=buffer.Get(); UINT stride=32,offset=0;
            Context->IASetVertexBuffers(0,1,&geometry,&stride,&offset);
            Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            pass.BindGeometry(Context.Get());
            if (separateParts) {
                Context->Draw(3,0); pass.BindGeometry(Context.Get()); Context->Draw(3,3);
            } else {
                Context->Draw(6,0);
            }
        }
        /// Rejects same-piece normal creases and requires a contour between separate coplanar pieces.
        void TestPartIdentity(Win32CameraToonPass& pass, UINT samples) {
            D3D11_VIEWPORT viewport {7,9,64,48,0,1};
            auto destination=CreateDestination(samples); PrepareMaterial(false,1);
            const float white[] = {1,1,1,1}; Context->UpdateSubresource(Emission.Get(),0,nullptr,white,0,0);
            pass.Configure(0,2,3); pass.Begin(Context.Get(),viewport,samples); DrawContourPanel(pass,true,false,true);
            pass.Composite(Context.Get(),destination.Get(),nullptr,viewport); Readback(destination.Get());
            for (UINT pixel=0;pixel<DestinationWidth*DestinationHeight;++pixel) {
                if (Pixels[pixel*4+3]==255) Require(Pixels[pixel*4]==255,
                    "Normal/depth changes inside one authored piece still created spurious internal ink.");
            }
            destination=CreateDestination(samples); PrepareMaterial(false,1);
            Context->UpdateSubresource(Emission.Get(),0,nullptr,white,0,0);
            pass.Begin(Context.Get(),viewport,samples); DrawContourPanel(pass,false,true);
            pass.Composite(Context.Get(),destination.Get(),nullptr,viewport); Readback(destination.Get());
            UINT dark=0;
            for (UINT pixel=0;pixel<DestinationWidth*DestinationHeight;++pixel) {
                if (Pixels[pixel*4+3]==255 && Pixels[pixel*4]<245) ++dark;
            }
            Require(dark>=20, "Separate visible coplanar pieces did not receive their shared internal contour.");
        }
        /// Requires fractional coverage on the composed outlines, rather than only on the earlier geometry resolve.
        void TestComposedContourCoverage(Win32CameraToonPass& pass, UINT samples) {
            D3D11_VIEWPORT viewport {7,9,64,48,0,1};
            auto destination=CreateDestination(samples); PrepareMaterial(true,1,0,true); pass.Configure(3,0,3);
            pass.Begin(Context.Get(),viewport,samples); DrawContourPanel(pass);
            pass.Composite(Context.Get(),destination.Get(),nullptr,viewport); Readback(destination.Get());
            UINT partialExterior=0;
            for (UINT pixel=0;pixel<DestinationWidth*DestinationHeight;++pixel) {
                int alpha=Pixels[pixel*4+3];
                if (alpha>1 && alpha<254 && Pixels[pixel*4]<=4 && Pixels[pixel*4+1]<=5 && Pixels[pixel*4+2]<=7) {
                    ++partialExterior;
                    Require(std::abs(static_cast<int>(Pixels[pixel*4])-(3*alpha+127)/255)<=1,
                        "Exterior AA damaged premultiplied ink color.");
                }
            }
            Require(partialExterior>=12, "Composed oblique exterior remained binary after geometry MSAA resolve.");
            if (!OutputPath.empty()) SaveOwnedFixture(OutputPath+".exterior.bmp");
            destination=CreateDestination(samples); PrepareMaterial(false,1);
            const float white[] = {1,1,1,1}; Context->UpdateSubresource(Emission.Get(),0,nullptr,white,0,0);
            pass.Configure(0,0,3); pass.Begin(Context.Get(),viewport,samples); DrawContourPanel(pass);
            pass.Composite(Context.Get(),destination.Get(),nullptr,viewport); Readback(destination.Get());
            std::vector<unsigned char> baseline=Pixels;
            destination=CreateDestination(samples); PrepareMaterial(false,1);
            Context->UpdateSubresource(Emission.Get(),0,nullptr,white,0,0);
            pass.Configure(0,1,3); pass.Begin(Context.Get(),viewport,samples); DrawContourPanel(pass,false,false);
            pass.Composite(Context.Get(),destination.Get(),nullptr,viewport); Readback(destination.Get());
            for (UINT pixel=0;pixel<DestinationWidth*DestinationHeight;++pixel) {
                if (baseline[pixel*4+3]==255) Require(Pixels[pixel*4]==255,
                    "Smooth perspective distance on a coplanar panel created a false internal contour.");
            }
            destination=CreateDestination(samples); PrepareMaterial(false,1);
            Context->UpdateSubresource(Emission.Get(),0,nullptr,white,0,0);
            pass.Configure(0,1,3); pass.Begin(Context.Get(),viewport,samples); DrawContourPanel(pass);
            pass.Composite(Context.Get(),destination.Get(),nullptr,viewport); Readback(destination.Get());
            UINT partialInterior=0;
            for (UINT pixel=0;pixel<DestinationWidth*DestinationHeight;++pixel) {
                if (baseline[pixel*4+3]==255 && baseline[pixel*4]==255 && Pixels[pixel*4]>8 && Pixels[pixel*4]<250) {
                    ++partialInterior;
                    Require(Pixels[pixel*4+3]==255, "Internal stroke AA changed opaque surface alpha.");
                }
            }
            Require(partialInterior>=12, "Composed oblique internal stroke remained binary instead of integrating pixel coverage.");
            if (!OutputPath.empty()) SaveOwnedFixture(OutputPath+".interior.bmp");
            double totalInk=0;
            for (UINT y=18;y<30;++y) {
                double rowInk=0;
                for (UINT x=8;x<58;++x) {
                    UINT offset=((y+9)*DestinationWidth+x+7)*4;
                    if (baseline[offset+3]==255) rowInk+=(255.0-Pixels[offset])/252.0;
                }
                if (!(rowInk>0.8 && rowInk<2.2)) throw std::runtime_error("Oblique AA erased or overexpanded internal stroke: samples="+std::to_string(samples)+" row="+std::to_string(y)+" ink="+std::to_string(rowInk));
                totalInk+=rowInk;
            }
            // The authored diagonal spans35.2px horizontally and33.12px vertically, so1px normal width covers1.46px per row.
            Require(std::abs(totalInk/12.0-std::sqrt(35.2*35.2+33.12*33.12)/33.12)<0.3,
                "Oblique internal AA changed its integrated one-pixel width.");
        }
        /// Draws a real ordinary shader after toon composition to verify opt-out submission and later-camera output.
        void TestFollowingStandardPass(Win32CameraToonPass& pass, UINT samples) {
            D3D11_VIEWPORT viewport { 0, 0, 64, 48, 0, 1 };
            auto destination = CreateDestination(samples); PrepareMaterial(true, 1); pass.Configure(3, 1, 3);
            pass.Begin(Context.Get(), viewport, samples); DrawRectangle(pass, -0.5f, 0.5f, 0.5f, -0.5f);
            pass.Composite(Context.Get(), destination.Get(), nullptr, viewport);
            const char* source = R"(
                float4 VS(uint id : SV_VertexID) : SV_POSITION {
                    float2 positions[3] = { float2(-1,-1), float2(-1,3), float2(3,-1) };
                    return float4(positions[id], 0, 1);
                }
                float4 PS() : SV_Target { return float4(0.8,0.2,0.6,1); }
            )";
            Microsoft::WRL::ComPtr<ID3DBlob> vertexBinary, pixelBinary, errors;
            Check(D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, "VS", "vs_5_0", 0, 0, &vertexBinary, &errors), "Compile ordinary following-camera vertex shader");
            Check(D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, "PS", "ps_5_0", 0, 0, &pixelBinary, &errors), "Compile ordinary following-camera pixel shader");
            Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader; Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader;
            Check(Device->CreateVertexShader(vertexBinary->GetBufferPointer(), vertexBinary->GetBufferSize(), nullptr, &vertexShader), "Create ordinary following-camera vertex shader");
            Check(Device->CreatePixelShader(pixelBinary->GetBufferPointer(), pixelBinary->GetBufferSize(), nullptr, &pixelShader), "Create ordinary following-camera pixel shader");
            Context->VSSetShader(vertexShader.Get(), nullptr, 0); Context->PSSetShader(pixelShader.Get(), nullptr, 0);
            Context->IASetInputLayout(nullptr); Context->OMSetDepthStencilState(nullptr, 0);
            // The ordinary renderer binds its own shader and uses the restored main target, without invoking a toon helper.
            Context->Draw(3, 0); Readback(destination.Get());
            ExpectChannel(13, 24, 0, 204, "Following ordinary camera retained a toon silhouette.");
            ExpectChannel(32, 24, 1, 51, "Following ordinary camera retained toon light bands.");
            ExpectChannel(5, 5, 3, 255, "Following ordinary camera did not use the restored main output target.");
        }
        /// Measures one warmed actual-size camera pass with GPU timestamps, without presenting or capturing a window.
        void TestViewportGpuTime(Win32CameraToonPass& pass) {
            D3D11_TEXTURE2D_DESC descriptor {}; descriptor.Width=703; descriptor.Height=627;
            descriptor.MipLevels=1; descriptor.ArraySize=1; descriptor.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
            descriptor.SampleDesc.Count=4; descriptor.BindFlags=D3D11_BIND_RENDER_TARGET;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
            Microsoft::WRL::ComPtr<ID3D11RenderTargetView> destination;
            Check(Device->CreateTexture2D(&descriptor,nullptr,&texture), "Create actual-size owned toon destination");
            Check(Device->CreateRenderTargetView(texture.Get(),nullptr,&destination), "Create actual-size owned toon target");
            D3D11_VIEWPORT viewport {0,0,703,627,0,1};
            PrepareMaterial(true,1); pass.Configure(6,3,3);
            pass.Begin(Context.Get(),viewport,4); DrawContourPanel(pass);
            pass.Composite(Context.Get(),destination.Get(),nullptr,viewport);
            Microsoft::WRL::ComPtr<ID3D11Query> disjoint,start,end;
            D3D11_QUERY_DESC query {}; query.Query=D3D11_QUERY_TIMESTAMP_DISJOINT;
            Check(Device->CreateQuery(&query,&disjoint), "Create owned toon timing interval");
            query.Query=D3D11_QUERY_TIMESTAMP;
            Check(Device->CreateQuery(&query,&start), "Create owned toon start timestamp");
            Check(Device->CreateQuery(&query,&end), "Create owned toon end timestamp");
            PrepareMaterial(true,1);
            Context->Begin(disjoint.Get()); Context->End(start.Get());
            pass.Begin(Context.Get(),viewport,4); DrawContourPanel(pass);
            pass.Composite(Context.Get(),destination.Get(),nullptr,viewport);
            Context->End(end.Get()); Context->End(disjoint.Get()); Context->Flush();
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT interval {}; HRESULT result;
            ULONGLONG deadline=GetTickCount64()+10000;
            while ((result=Context->GetData(disjoint.Get(),&interval,sizeof(interval),0))==S_FALSE) {
                Require(GetTickCount64()<deadline, "Owned toon GPU timestamp did not complete within ten seconds."); Sleep(0);
            }
            Check(result, "Read owned toon GPU interval");
            UINT64 first=0,last=0;
            Require(Context->GetData(start.Get(),&first,sizeof(first),0)==S_OK
                && Context->GetData(end.Get(),&last,sizeof(last),0)==S_OK, "Owned toon timestamps were incomplete.");
            Require(!interval.Disjoint && interval.Frequency>0, "Owned toon timestamp interval changed frequency; timing is invalid.");
            double milliseconds=1000.0*static_cast<double>(last-first)/static_cast<double>(interval.Frequency);
            std::cout << "Owned703x6274x toon GPU time(ms): " << milliseconds << " (synthetic panel only; not whole-app FPS).\n";
        }
        /// Rejects NaN, negative/excessive pixel widths, unsupported bands and missing device dependencies.
        void TestInvalidSettings() {
            Win32CameraToonPass pass(Device.Get(), 3, 1, 3);
            WaitForShaders(pass);
            for (float invalid : { -1.0f, 17.0f, std::numeric_limits<float>::quiet_NaN() }) {
                bool rejected = false;
                try { pass.Configure(invalid, 1, 3); } catch (const std::invalid_argument&) { rejected = true; }
                Require(rejected, "Invalid outside width was accepted.");
                rejected = false;
                try { pass.Configure(3, invalid, 3); } catch (const std::invalid_argument&) { rejected = true; }
                Require(rejected, "Invalid inside width was accepted.");
            }
            for (int invalid : { 0, 1, 17 }) {
                bool rejected = false;
                try { pass.Configure(3, 1, invalid); } catch (const std::invalid_argument&) { rejected = true; }
                Require(rejected, "Unsupported toon band count was accepted.");
            }
            CameraToonEffectsConfiguration pearl;
            for(float invalid : {-0.01f,1.01f,std::numeric_limits<float>::quiet_NaN()}) {
                pearl.PearlStrength=invalid; bool failed=false;
                try { pass.ConfigureEffects(pearl); } catch(const std::invalid_argument&) { failed=true; }
                Require(failed,"Invalid pearl intensity was accepted.");
                pearl.PearlStrength=0; pearl.PearlHighlight=invalid; failed=false;
                try { pass.ConfigureEffects(pearl); } catch(const std::invalid_argument&) { failed=true; }
                Require(failed,"Invalid pearl highlight was accepted.");
                pearl.PearlHighlight=0; pearl.PearlWidth=invalid; failed=false;
                try { pass.ConfigureEffects(pearl); } catch(const std::invalid_argument&) { failed=true; }
                Require(failed,"Invalid pearl width was accepted."); pearl.PearlWidth=0;
            }
            CameraToonEffectsConfiguration effects;
            effects.BloomRadius=25;
            bool rejected=false;
            try { pass.ConfigureEffects(effects); } catch(const std::invalid_argument&) {rejected=true;}
            Require(rejected,"Unbounded bloom spread was accepted.");
            effects.BloomRadius=0; effects.ReflectionStrength=std::numeric_limits<float>::quiet_NaN(); rejected=false;
            try { pass.ConfigureEffects(effects); } catch(const std::invalid_argument&) {rejected=true;}
            Require(rejected,"Nonfinite reflection strength was accepted.");
            const uint8_t pixel[] {255,255,255,255}; rejected=false;
            try { pass.SetEnvironment(Context.Get(),1,2,pixel,sizeof(pixel)); } catch(const std::invalid_argument&) {rejected=true;}
            Require(rejected,"Truncated RGBA environment upload was accepted.");
        }
    };
}

/// Runs only synthetic hardware geometry; no user window, desktop surface or application is captured.
int main(int argc, char** argv) {
    try {
        if(argc==2 && std::string(argv[1])=="--shader-startup") {
            helengine::windows::CameraToonTests tests; tests.MeasureShaderStartup(); return 0;
        }
        if (argc>1 && (std::string(argv[1])=="--radio" || std::string(argv[1])=="--radio-effects" || std::string(argv[1])=="--radio-pearl")) {
            if (argc<6 || argc>9) throw std::invalid_argument("Use --radio geometry.bin output.bmp outsidePixels insidePixels [offsetPixels] [renderScale] [samples].");
            helengine::windows::CameraToonRadioFixture::Render(argv[2],argv[3],std::stof(argv[4]),std::stof(argv[5]),argc>=7 ? std::stof(argv[6]) : 0.0f,argc>=8 ? std::stoi(argv[7]) : 1,argc==9 ? static_cast<UINT>(std::stoi(argv[8])) : 4u,std::string(argv[1])!="--radio",std::string(argv[1])=="--radio-pearl");
            return 0;
        }
        helengine::windows::CameraToonTests tests(argc>1 ? argv[1] : ""); tests.Run(); return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
