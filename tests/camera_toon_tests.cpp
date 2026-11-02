#include "platform/windows/win32/win32_camera_toon_pass.hpp"
#include "camera_toon_radio_fixture.hpp"
#include "platform/windows/win32/win32_camera_toon_effects_configuration.hpp"
#include <d3dcompiler.h>
#include <DirectXPackedVector.h>
#include "platform/windows/win32/win32_material_output_state.hpp"
#include <array>
#include <chrono>
#include <thread>
#include "platform/windows/win32/win32_camera_toon_shader_source.hpp"
#include "platform/windows/win32/win32_pearl_surface_shader.hpp"
#include <algorithm>
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

        /// <summary>Runs only the normal-crease attachment regressions without a presentation surface.</summary>
        void RunCreases() { for(UINT samples : {1u,4u}) { TestCreaseJunctions(samples); TestCreaseWidthAtAngles(samples); TestCreaseBackgroundIndependence(samples); TestNormalCreases(samples); } }

        /// <summary>Checks the low-cost normal layer on sharp, smooth, coplanar, excluded and occluded meshes at both sample counts.</summary>
        void RunNormalLayer() {
            for(UINT samples : {1u,4u}) for(int mode=0;mode<6;++mode) {
                Win32CameraToonPass pass(Device.Get(),2,0,3);WaitForShaders(pass);
                CameraToonEffectsConfiguration effects;effects.CreaseAngleDegrees=mode==4 ? 180.f : 45.f;
                pass.ConfigureEffects(effects);PrepareMaterial(true,1,0,true);
                auto destination=CreateDestination();auto emission=CreateAlbum(true);auto* view=emission.Get();Context->PSSetShaderResources(7,1,&view);
                D3D11_VIEWPORT viewport{0,0,64,48,0,1};pass.Begin(Context.Get(),viewport,samples);
                DrawCreasePanel(pass,0,mode==1,mode==2,mode!=3,0,1,true);
                if(mode==5) DrawRectangle(pass,-.3f,.8f,.3f,-.8f,0,1,.1f);
                pass.Composite(Context.Get(),destination.Get(),nullptr,viewport);Readback(destination.Get());
                bool ink=Pixels[(24*DestinationWidth+31)*4]<15 || Pixels[(24*DestinationWidth+32)*4]<15;
                Require(ink==(mode==0),"Normal layer must affect only visible sharp eligible creases.");
                Require(Pixels[(18*DestinationWidth+18)*4]>230,"Normal layer marked a coplanar triangle interior.");
                std::cout << "Normal layer " << samples << "x mode=" << mode << " passed." << std::endl;
            }
        }

        /// <summary>Requires thin cloth to outline against a nearby wall without marking coplanar mesh or material boundaries.</summary>
        void RunThinSurface() {
            for(UINT samples : {1u,4u}) for(bool gap : {false,true}) for(bool perspective : {false,true}) {
                Win32CameraToonPass pass(Device.Get(),2,0,3);WaitForShaders(pass);
                auto destination=CreateDestination();PrepareMaterial(false,1);
                D3D11_VIEWPORT viewport{0,0,64,48,0,1};pass.Begin(Context.Get(),viewport,samples,perspective ? .1f : 0.f,perspective ? 100.f : 1.f);
                const float wall=perspective ? (100.f-10.f/3.7f)/99.9f : .5f;
                const float cloth=gap ? (perspective ? (100.f-10.f/3.688f)/99.9f : .498f) : wall;
                int wallOwner=0,clothOwner=0;
                DrawRectangle(pass,-.9f,.9f,.9f,-.9f,0,1,wall,0,0,nullptr,&wallOwner);
                DrawRectangle(pass,-.5f,.5f,0,-.5f,0,1,cloth,0,0,nullptr,&clothOwner);
                DrawRectangle(pass,0,.5f,.5f,-.5f,0,1,cloth,0,0,nullptr,&clothOwner);
                pass.Composite(Context.Get(),destination.Get(),nullptr,viewport);Readback(destination.Get());
                Require((Pixels[(24*DestinationWidth+16)*4]<15)==gap,"Thin cloth depth gap must outline; coplanar identities must remain unmarked.");
                Require(Pixels[(24*DestinationWidth+31)*4]>=15,"Cloth material slots must not acquire a stripe seam.");
                Require(Pixels[(24*DestinationWidth+15)*4]>=15,"Thin cloth ink must stay on the foreground side.");
                std::cout << "Thin surface " << samples << "x gap=" << gap << " perspective=" << perspective << " passed." << std::endl;
            }
        }

        /// <summary>Checks unilateral depth edges independently of mesh IDs and excludes coplanar material seams.</summary>
        void RunDepthSobel() {
            RunDepthOpening();RunSilhouetteBounds();
            for(UINT samples : {1u,4u}) for(bool sameMesh : {false,true}) for(bool stepped : {false,true}) {
                Win32CameraToonPass pass(Device.Get(),3,5,3);WaitForShaders(pass);
                auto destination=CreateDestination();PrepareMaterial(false,1);
                D3D11_VIEWPORT viewport{0,0,64,48,0,1};pass.Begin(Context.Get(),viewport,samples);
                int firstOwner=0,secondOwner=0;
                DrawRectangle(pass,-.75f,.75f,0,-.75f,0,1,.2f,0,0,nullptr,&firstOwner);
                DrawRectangle(pass,0,.75f,.75f,-.75f,0,1,stepped ? .8f : .2f,0,0,nullptr,sameMesh ? &firstOwner : &secondOwner);
                pass.Composite(Context.Get(),destination.Get(),nullptr,viewport);Readback(destination.Get());
                bool ink=Pixels[(24*DestinationWidth+31)*4]<15;
                Require(ink==stepped,"Depth edges must ignore mesh ownership and reject coplanar seams.");
                Require(Pixels[(24*DestinationWidth+9)*4]<15,"Mesh exterior lost its outline.");
                std::cout << "Depth Sobel " << samples << "x shared-mesh=" << sameMesh << " passed." << std::endl;
            }
        }

        /// <summary>Requires an opening edge between parallel same-material surfaces but rejects a continuous sloped plane.</summary>
        void RunDepthOpening() {
            for(UINT samples : {1u,4u}) for(bool stepped : {false,true}) for(bool translated : {false,true}) for(bool perspective : {false,true}) {
                Win32CameraToonPass pass(Device.Get(),2,0,3);WaitForShaders(pass);
                auto destination=CreateDestination();PrepareMaterial(false,1);
                if(translated) {
                    std::array<float,76> transform{};
                    for(int matrix=0;matrix<3;++matrix) for(int diagonal=0;diagonal<4;++diagonal) transform[matrix*16+diagonal*5]=1;
                    transform[3]=8.2f;transform[7]=2.3f;transform[11]=8.2f;transform[50]=2;
                    Context->UpdateSubresource(Transforms.Get(),0,nullptr,transform.data(),0,0);
                }
                D3D11_VIEWPORT viewport{0,0,64,48,0,1};pass.Begin(Context.Get(),viewport,samples,perspective ? .1f : 0.f,perspective ? 100.f : 1.f);
                float vertices[96]{};
                const float coordinates[]={-.75f,.75f,0,.75f,-.75f,-.75f,-.75f,-.75f,0,.75f,0,-.75f,0,.75f,.75f,.75f,0,-.75f,0,-.75f,.75f,.75f,.75f,-.75f};
                for(int index=0;index<12;++index) {
                    float x=coordinates[index*2];vertices[index*8]=x;vertices[index*8+1]=coordinates[index*2+1];
                    vertices[index*8+2]=stepped ? (index<6 ? .2f : .8f) : .5f+x*.3f;
                    vertices[index*8+3]=stepped ? 0 : -.28734789f;vertices[index*8+5]=stepped ? 1 : .95782629f;
                }
                D3D11_BUFFER_DESC desc{};desc.ByteWidth=sizeof(vertices);desc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
                D3D11_SUBRESOURCE_DATA data{vertices,0,0};Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
                Check(Device->CreateBuffer(&desc,&data,&buffer),"Create same-part depth opening");
                ID3D11Buffer* raw=buffer.Get();UINT stride=32,offset=0;Context->IASetVertexBuffers(0,1,&raw,&stride,&offset);
                Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);pass.BindGeometry(Context.Get(),0,0);Context->Draw(12,0);
                pass.Composite(Context.Get(),destination.Get(),nullptr,viewport);Readback(destination.Get());
                bool ink=Pixels[(24*DestinationWidth+31)*4]<15 || Pixels[(24*DestinationWidth+32)*4]<15;
                Require(ink==stepped,"Depth Sobel must detect a same-mesh opening and reject continuous slopes.");
                if(!stepped) for(int y=10;y<38;++y) for(int x=12;x<52;++x)
                    Require(Pixels[(y*DestinationWidth+x)*4]>=15,"Position quantization introduced false ink on a continuous surface.");
                std::cout << "Depth opening " << samples << "x stepped=" << stepped << " passed." << std::endl;
            }
        }

        /// <summary>Rejects ink beyond the configured radius around acute finite triangle edges.</summary>
        void RunSilhouetteBounds() {
            for(UINT samples : {1u,4u}) for(bool shade : {false,true}) {
                Win32CameraToonPass pass(Device.Get(),4,0,3); WaitForShaders(pass);
                D3D11_VIEWPORT viewport{0,0,64,48,0,1};
                auto destination=CreateDestination(); PrepareMaterial(true,1); pass.Begin(Context.Get(),viewport,samples);
                const std::vector<float> points=shade ? std::vector<float>{20,40,28,16,36,16,44,40,38,37,26,37} : std::vector<float>{20,18,42,19,29,40};
                const std::vector<int> indices=shade ? std::vector<int>{0,1,2,0,2,5,5,2,4,4,2,3} : std::vector<int>{0,1,2};
                std::vector<float> vertices(indices.size()*8,0);
                for(size_t i=0;i<indices.size();++i) { int source=indices[i];vertices[i*8]=points[source*2]/32-1;vertices[i*8+1]=1-points[source*2+1]/24;vertices[i*8+2]=.5f;vertices[i*8+5]=1; }
                D3D11_BUFFER_DESC descriptor{};descriptor.ByteWidth=static_cast<UINT>(vertices.size()*sizeof(float));descriptor.BindFlags=D3D11_BIND_VERTEX_BUFFER;
                D3D11_SUBRESOURCE_DATA initial{vertices.data(),0,0};Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
                Check(Device->CreateBuffer(&descriptor,&initial,&buffer),"Create acute silhouette fixture");
                UINT stride=32,offset=0;ID3D11Buffer* raw=buffer.Get();Context->IASetVertexBuffers(0,1,&raw,&stride,&offset);
                Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);pass.BindGeometry(Context.Get(),0,0);Context->Draw(static_cast<UINT>(indices.size()),0);
                pass.Composite(Context.Get(),destination.Get(),nullptr,viewport);Readback(destination.Get());
                if(!OutputPath.empty() && shade && samples==4) SaveOwnedFixture(OutputPath);
                int escaped=0;double furthest=0;
                for(int y=0;y<48;++y) for(int x=0;x<64;++x) {
                    double distance=1e9;bool inside=false;
                    for(int edge=0;edge<static_cast<int>(points.size()/2);++edge) {
                        int next=(edge+1)%static_cast<int>(points.size()/2);double ax=points[edge*2],ay=points[edge*2+1],dx=points[next*2]-ax,dy=points[next*2+1]-ay;
                        double px=x+.5-ax,py=y+.5-ay,t=std::clamp((px*dx+py*dy)/(dx*dx+dy*dy),0.0,1.0);
                        distance=std::min(distance,std::hypot(px-t*dx,py-t*dy));if((ay>y+.5)!=(ay+dy>y+.5) && x+.5<ax+dx*py/dy) inside=!inside;
                    }
                    if(!inside && Pixels[(y*DestinationWidth+x)*4+3]>16) { furthest=std::max(furthest,distance);if(distance>4.71) ++escaped; }
                }
                std::cout << "Silhouette radius " << samples << "x shade=" << shade << ": maximum=" << furthest << " escaped=" << escaped << std::endl;
                Require(escaped==0,"Finite silhouette expansion produced corner spikes beyond its four-pixel radius.");
            }
        }

        /// <summary>Checks adjacent registered wall corners and the existing sofa eligibility contract without creating a window.</summary>
        void RunWallInk() {
            for(UINT samples : {1u,4u}) {
                TestRegisteredWallCorners(samples);
                TestRegisteredToonSurface(samples);
                std::cout << "Registered wall corners and sofa contours " << samples << "x passed.\n";
            }
        }

        /// <summary>Runs dynamic occlusion and radiance separation on both supported sample counts.</summary>
        void RunGtao() { for(UINT samples : {1u,4u}) { TestAoPreservesDirectAndEmission(samples); TestContactOcclusion(samples); TestGtaoCameraAndMotion(samples); TestGtaoHdrAndGlass(samples); } TestGtaoGpuTime(); }

        /// Verifies settings, independent outlines, lighting, emission, depth, alpha, target resize and context restoration.
        void Run() {
            TestInvalidSettings();
            RunDepthSobel();
            Win32CameraToonPass pass(Device.Get(), 3.0f, 1.0f, 3);
            WaitForShaders(pass);
            for (UINT samples : { 4u, 1u }) {
                TestHdrDisplayAndAlpha(samples);
                TestContactOcclusion(samples);
                TestAoPreservesDirectAndEmission(samples);
                TestGtaoCameraAndMotion(samples);
                TestGtaoHdrAndGlass(samples);
                TestRegisteredToonSurface(samples);
                TestSelectedEffects(samples);
                TestPearlSurface(samples);
                TestShadowDirectOnly(samples);
                TestPartIdentity(pass, samples);
                TestOutlineAndEmission(pass, samples);
                TestLightingBands(pass, samples);
                TestAuthoredAlpha(pass,samples);
                TestOverlap(pass, samples);
                TestSmallAndMovedViewport(pass, samples);
                TestObliqueCoverage(pass, samples);
                TestFollowingStandardPass(pass, samples);
                TestSequentialScratchSharing(pass,samples);
                std::cout << "Camera toon hardware " << samples << "x: emission, album color, alpha, external silhouettes, bands, depth, resize and state passed.\n";
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
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> CreateDestination(UINT samples = 1, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM) {
            D3D11_TEXTURE2D_DESC descriptor {};
            descriptor.Width = DestinationWidth; descriptor.Height = DestinationHeight;
            descriptor.MipLevels = 1; descriptor.ArraySize = 1; descriptor.Format = format;
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
        void DrawRectangle(Win32CameraToonPass& pass, float left, float top, float right, float bottom, float normalX = 0.0f, float normalZ = 1.0f, float depth = 0.5f, float reflectivity = 0.0f, float bloomWeight = 0.0f, const void* material = nullptr, const void* owner = nullptr) {
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
            if(material!=nullptr) pass.BindRegisteredGeometry(Context.Get(),material,false,false,owner);
            else pass.BindGeometry(Context.Get(), reflectivity, bloomWeight,false,false,1,0,false,owner);
            Context->Draw(6, 0);
        }

        /// <summary>Draws both visible faces in one mesh submission so the shared crease cannot use distinct part IDs.</summary>
        void DrawCreasePanel(Win32CameraToonPass& pass, float offset, bool smooth, bool coplanar, bool outlined, float angle=0, float scale=1, bool flatDepth=false) {
            const float n=0.70710678f;
            float x0=offset-0.6f,x1=offset,x2=offset+0.6f;
            float z0=coplanar || flatDepth ? 0.2f : 0.8f,z1=0.2f;
            float leftX=coplanar ? 0.0f : -n,rightX=coplanar ? 0.0f : n;
            float outerZ=coplanar ? 1.0f : n;
            float vertices[]={
                x0,0.75f,z0,leftX,0,outerZ,0,0, x1,0.75f,z1,smooth ? 0.0f:leftX,0,smooth ? 1.0f:outerZ,0,0, x0,-0.75f,z0,leftX,0,outerZ,0,0,
                x0,-0.75f,z0,leftX,0,outerZ,0,0, x1,0.75f,z1,smooth ? 0.0f:leftX,0,smooth ? 1.0f:outerZ,0,0, x1,-0.75f,z1,smooth ? 0.0f:leftX,0,smooth ? 1.0f:outerZ,0,0,
                x1,0.75f,z1,smooth ? 0.0f:rightX,0,smooth ? 1.0f:outerZ,0,0, x2,0.75f,z0,rightX,0,outerZ,0,0, x1,-0.75f,z1,smooth ? 0.0f:rightX,0,smooth ? 1.0f:outerZ,0,0,
                x1,-0.75f,z1,smooth ? 0.0f:rightX,0,smooth ? 1.0f:outerZ,0,0, x2,0.75f,z0,rightX,0,outerZ,0,0, x2,-0.75f,z0,rightX,0,outerZ,0,0
            };
            for(int vertex=0;vertex<12;++vertex) {
                float x=vertices[vertex*8],y=vertices[vertex*8+1];
                vertices[vertex*8]=scale*(x*std::cos(angle)-y*std::sin(angle));
                vertices[vertex*8+1]=scale*(x*std::sin(angle)+y*std::cos(angle));
            }
            D3D11_BUFFER_DESC descriptor{};descriptor.ByteWidth=sizeof(vertices);descriptor.BindFlags=D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA initial{vertices,0,0};Microsoft::WRL::ComPtr<ID3D11Buffer> vertex;
            Check(Device->CreateBuffer(&descriptor,&initial,&vertex),"Create one-part normal crease fixture");
            auto* buffer=vertex.Get();UINT stride=32,start=0;Context->IASetVertexBuffers(0,1,&buffer,&stride,&start);
            Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            pass.BindGeometry(Context.Get(),0,0,false,false,outlined ? 1.0f : 0.0f);Context->Draw(12,0);
        }
        /// <summary>Rejects lost crease shading, triangle seams, smooth-surface contours and hidden or ineligible edges on real attachments.</summary>
        void TestNormalCreases(UINT samples) {
            Win32CameraToonPass pass(Device.Get(),2,0,3);WaitForShaders(pass);
            D3D11_VIEWPORT viewport{7,9,64,48,0,1};
            CameraToonEffectsConfiguration effects;
            for(float invalid : {-1.0f,181.0f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
                effects.CreaseAngleDegrees=invalid;bool rejected=false;
                try { pass.ConfigureEffects(effects); } catch(const std::invalid_argument&) { rejected=true; }
                Require(rejected,"Invalid normal-crease angle was accepted");
            }
            for(int mode=0;mode<10;++mode) {
                effects.CreaseAngleDegrees=mode==1 ? 80.0f : mode==2 ? 0.1f : mode==3 ? 0.0f : mode==6 ? 100.0f : mode==7 ? 180.0f : 45.0f;
                pass.ConfigureEffects(effects);pass.Configure(0,mode==9 ? 1.0f : 2.0f,3);
                PrepareMaterial(true,1,0,true);auto destination=CreateDestination(samples);
                auto whiteEmission=CreateAlbum(true);auto* emissionView=whiteEmission.Get();Context->PSSetShaderResources(7,1,&emissionView);
                pass.Begin(Context.Get(),viewport,samples);
                float offset=mode==1 ? 0.015625f : 0.0f;
                DrawCreasePanel(pass,offset,mode==2 || mode==8,mode==3,mode!=4);
                if(mode==5) DrawRectangle(pass,-0.3f,0.8f,0.3f,-0.8f,0,1,0.1f);
                pass.Composite(Context.Get(),destination.Get(),nullptr,viewport);Readback(destination.Get());
                double ink=0;
                for(UINT x=28;x<=36;++x) ink+=(255.0-Pixels[((9+24)*DestinationWidth+7+x)*4])/252.0;
                if(mode<2 || mode==9) {
                    Require(mode==9 ? ink>0.9 && ink<1.1 : ink>1.8 && ink<2.2,"Single-part 90-degree normal crease lost its physical width");
                    ExpectChannel(7+24,9+24,0,255,"Normal crease contaminated a flat face interior");
                } else Require(ink<0.02,"Normal creases outlined smooth, coplanar, ineligible or hidden geometry");
                ExpectChannel(7+24,9+18,0,255,"Coplanar triangle diagonal received a normal crease");
            }
            std::cout << "Normal creases " << samples << "x: validated angle, 45/80/100/180-degree thresholds, one/two-pixel width, half-pixel phase, smooth/coplanar/hidden/disabled passed.\n";
        }
        /// <summary>Three faces meeting at a vertex must not turn their interiors into triangular ink patches.</summary>
        void TestCreaseJunctions(UINT samples) {
            Win32CameraToonPass pass(Device.Get(),2,0,3);WaitForShaders(pass);
            D3D11_VIEWPORT viewport{7,9,64,48,0,1};
            double worst=0;int outside=0;
            for(float scale : {0.8f,0.35f,0.15f}) for(float angle : {0.0f,0.3f,0.7f,1.2f}) {
                float vertices[72]{};double ends[3][2]{};
                for(int face=0;face<3;++face) {
                    double a=angle+face*6.283185307179586/3;
                    ends[face][0]=32*scale*std::cos(a);ends[face][1]=-24*scale*std::sin(a);
                    for(int corner=0;corner<3;++corner) {
                        int vertex=(face*3+corner)*8;
                        double direction=angle+(face+corner-1)*6.283185307179586/3;
                        vertices[vertex]=corner==0 ? 0:static_cast<float>(scale*std::cos(direction));
                        vertices[vertex+1]=corner==0 ? 0:static_cast<float>(scale*std::sin(direction));
                        vertices[vertex+2]=.5f;vertices[vertex+3+face]=1;
                    }
                }
                D3D11_BUFFER_DESC descriptor{};descriptor.ByteWidth=sizeof(vertices);descriptor.BindFlags=D3D11_BIND_VERTEX_BUFFER;
                D3D11_SUBRESOURCE_DATA initial{vertices,0,0};Microsoft::WRL::ComPtr<ID3D11Buffer> geometry;
                Check(Device->CreateBuffer(&descriptor,&initial,&geometry),"Create three-face crease junction");
                PrepareMaterial(true,1,0,true);auto destination=CreateDestination(samples);
                auto emission=CreateAlbum(true);auto* view=emission.Get();Context->PSSetShaderResources(7,1,&view);
                pass.Begin(Context.Get(),viewport,samples);
                auto* buffer=geometry.Get();UINT stride=32,start=0;Context->IASetVertexBuffers(0,1,&buffer,&stride,&start);
                Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);pass.BindGeometry(Context.Get());Context->Draw(9,0);
                pass.Composite(Context.Get(),destination.Get(),nullptr,viewport);Readback(destination.Get());
                for(UINT y=1;y<47;++y) for(UINT x=1;x<63;++x) {
                    auto pixel=((9+y)*DestinationWidth+7+x)*4;
                    if(Pixels[pixel+3]!=255 || Pixels[pixel]>=230) continue;
                    double px=x+.5-32,py=y+.5-24,distance=1000;
                    for(int edge=0;edge<3;++edge) {
                        double dx=ends[edge][0],dy=ends[edge][1];
                        double t=std::max(0.0,std::min(1.0,(px*dx+py*dy)/(dx*dx+dy*dy)));
                        distance=std::min(distance,std::sqrt((px-t*dx)*(px-t*dx)+(py-t*dy)*(py-t*dy)));
                    }
                    worst=std::max(worst,distance);if(distance>1.75) ++outside;
                }
            }
            std::cout<<"Crease junction "<<samples<<"x max-distance="<<worst<<" outside="<<outside<<"\n";
            Require(outside==0,"Three-face corner produced triangular ink outside its edge footprint");
        }
        /// <summary>Bounds sharp-crease ink to the configured physical width for rotated and small foreground pieces.</summary>
        void TestCreaseWidthAtAngles(UINT samples) {
            Win32CameraToonPass pass(Device.Get(),2,0,3);WaitForShaders(pass);
            D3D11_VIEWPORT viewport{7,9,64,48,0,1};
            double worst=0;int outside=0;
            for(float scale : {1.0f,0.25f,0.10f}) for(float angle : {0.0f,0.2f,0.4f,0.7f,0.9f,1.2f,1.57f}) {
                PrepareMaterial(true,1,0,true);auto destination=CreateDestination(samples);
                auto emission=CreateAlbum(true);auto* view=emission.Get();Context->PSSetShaderResources(7,1,&view);
                pass.Begin(Context.Get(),viewport,samples);DrawCreasePanel(pass,0,false,false,true,angle,scale);
                pass.Composite(Context.Get(),destination.Get(),nullptr,viewport);Readback(destination.Get());
                double dx=-32*std::sin(angle),dy=-24*std::cos(angle),length=std::sqrt(dx*dx+dy*dy);
                for(UINT y=1;y<47;++y) for(UINT x=1;x<63;++x) {
                    auto pixel=((9+y)*DestinationWidth+7+x)*4;
                    if(Pixels[pixel+3]!=255 || Pixels[pixel]>=230) continue;
                    double distance=std::abs((x+.5-32)*dy-(y+.5-24)*dx)/length;
                    worst=std::max(worst,distance);
                    if(distance>1.75) ++outside;
                }
                if(!OutputPath.empty() && angle==0.7f && scale==0.25f) SaveOwnedFixture(OutputPath);
            }
            std::cout<<"Crease width "<<samples<<"x max-distance="<<worst<<" outside="<<outside<<"\n";
            Require(outside==0,"Crease corners produce ink beyond their configured width and pixel footprint");
        }
        /// <summary>Changing unrelated background normals must not rotate or extend the foreground crease at its endpoints.</summary>
        void TestCreaseBackgroundIndependence(UINT samples) {
            Win32CameraToonPass pass(Device.Get(),2,0,3);WaitForShaders(pass);
            D3D11_VIEWPORT viewport{7,9,64,48,0,1};
            int backgroundIdentity=0;
            pass.ConfigureMaterialEffects(&backgroundIdentity,0,0,false,true,0);
            for(float scale : {1.0f,0.25f,0.10f}) for(float angle : {0.0f,0.4f,0.9f,1.57f}) {
                std::vector<unsigned char> reference;
                for(int variant=0;variant<3;++variant) {
                    PrepareMaterial(true,1,0,true);auto destination=CreateDestination(samples);
                    auto emission=CreateAlbum(true);auto* view=emission.Get();Context->PSSetShaderResources(7,1,&view);
                    pass.Begin(Context.Get(),viewport,samples);
                    DrawRectangle(pass,-1,1,1,-1,variant==1 ? 1.0f:variant==2 ? -1.0f:0.0f,variant==0 ? 1.0f:0.0f,0.95f,0,0,&backgroundIdentity);
                    DrawCreasePanel(pass,0,false,false,true,angle,scale);
                    pass.Composite(Context.Get(),destination.Get(),nullptr,viewport);Readback(destination.Get());
                    if(variant==0) reference=Pixels;
                    else {
                        int changed=0;
                        for(UINT y=1;y<47;++y) for(UINT x=1;x<63;++x) {
                            auto pixel=((9+y)*DestinationWidth+7+x)*4;
                            if(std::abs(int(Pixels[pixel])-int(reference[pixel]))>2) ++changed;
                        }
                        std::cout<<"Crease background independence "<<samples<<"x scale="<<scale<<" angle="<<angle<<" variant="<<variant<<" changed="<<changed<<"\n";
                        Require(changed==0,"Unrelated background normals deform a foreground crease endpoint");
                    }
                }
            }
        }
        /// Verifies compatible sequential cameras reuse actual GPU targets while overlapping camera passes remain independent.
        void TestSequentialScratchSharing(Win32CameraToonPass& first, UINT samples) {
            Win32CameraToonPass second(Device.Get(),0,0,3); WaitForShaders(second);
            CameraToonEffectsConfiguration effects; effects.ToonEnabled=false; second.ConfigureEffects(effects);
            D3D11_VIEWPORT viewport {7,9,64,48,0,1};
            PrepareMaterial(false,1,0,true);
            auto destination=CreateDestination(samples);
            first.Begin(Context.Get(),viewport,samples);
            Microsoft::WRL::ComPtr<ID3D11RenderTargetView> firstTarget;
            Context->OMGetRenderTargets(1,&firstTarget,nullptr);
            DrawRectangle(first,-0.5f,0.5f,0.5f,-0.5f);
            first.Composite(Context.Get(),destination.Get(),nullptr,viewport);
            second.Begin(Context.Get(),viewport,samples);
            Microsoft::WRL::ComPtr<ID3D11RenderTargetView> secondTarget;
            Context->OMGetRenderTargets(1,&secondTarget,nullptr);
            Require(firstTarget.Get()==secondTarget.Get(),"Compatible sequential camera passes duplicated their viewport scratch targets.");
            DrawRectangle(second,-0.5f,0.5f,0.5f,-0.5f);
            second.Composite(Context.Get(),destination.Get(),nullptr,viewport);
            first.Begin(Context.Get(),viewport,samples);
            Context->OMGetRenderTargets(1,firstTarget.ReleaseAndGetAddressOf(),nullptr);
            DrawRectangle(first,-0.5f,0.5f,0.5f,-0.5f);
            second.Begin(Context.Get(),viewport,samples);
            Context->OMGetRenderTargets(1,secondTarget.ReleaseAndGetAddressOf(),nullptr);
            Require(firstTarget.Get()!=secondTarget.Get(),"Overlapping camera passes borrowed the same busy scratch targets.");
            DrawRectangle(second,-0.25f,0.25f,0.25f,-0.25f);
            second.Composite(Context.Get(),destination.Get(),nullptr,viewport);
            first.Composite(Context.Get(),destination.Get(),nullptr,viewport);
            Readback(destination.Get());
            ExpectChannel(7+32,9+24,3,255,"Shared scratch ownership damaged final opaque coverage.");
        }
        /// Verifies radiance above one survives until exposure, with authored alpha and HDR bloom remaining premultiplied.
        void TestHdrDisplayAndAlpha(UINT samples) {
            Win32CameraToonPass pass(Device.Get(),0,0,3); WaitForShaders(pass);
            CameraToonEffectsConfiguration effects; effects.ToonEnabled=false; effects.ToneMappingEnabled=true; effects.Exposure=0.25f;
            pass.ConfigureEffects(effects);
            D3D11_VIEWPORT viewport {7,9,64,48,0,1};
            UINT center=((9+24)*DestinationWidth+7+32)*4;
            PrepareMaterial(false,1,0,true);
            std::array<float,72> lights {}; lights[0]=lights[1]=lights[2]=1;
            Context->UpdateSubresource(Lights.Get(),0,nullptr,lights.data(),0,0);
            auto destination=CreateDestination(samples);
            pass.Begin(Context.Get(),viewport,samples); DrawRectangle(pass,-0.5f,0.5f,0.5f,-0.5f);
            pass.Composite(Context.Get(),destination.Get(),nullptr,viewport); Readback(destination.Get());
            int low=Pixels[center];
            lights[0]=lights[1]=lights[2]=4; Context->UpdateSubresource(Lights.Get(),0,nullptr,lights.data(),0,0);
            destination=CreateDestination(samples);
            pass.Begin(Context.Get(),viewport,samples); DrawRectangle(pass,-0.5f,0.5f,0.5f,-0.5f);
            pass.Composite(Context.Get(),destination.Get(),nullptr,viewport); Readback(destination.Get());
            int high=Pixels[center];
            Require(high>low+35 && high<250,"HDR radiance was clipped before exposure or filmic output lost highlight headroom.");
            const float translucent[] {1,1,1,0.35f}; Context->UpdateSubresource(Base.Get(),0,nullptr,translucent,0,0);
            destination=CreateDestination(samples);
            pass.Begin(Context.Get(),viewport,samples); DrawRectangle(pass,-0.5f,0.5f,0.5f,-0.5f);
            pass.Composite(Context.Get(),destination.Get(),nullptr,viewport); Readback(destination.Get());
            Require(std::abs(static_cast<int>(Pixels[center+3])-89)<=1,"Tone mapping changed authored alpha.");
            Require(std::abs(static_cast<int>(Pixels[center])-static_cast<int>(std::round(high*0.35)))<=2,"Tone mapping was applied to premultiplied radiance without unpremultiplication.");
            effects.BloomEnabled=true; effects.BloomStrength=1; effects.BloomThreshold=1; effects.BloomRadius=12; pass.ConfigureEffects(effects);
            const float emission[] {4,2,1,1}; Context->UpdateSubresource(Emission.Get(),0,nullptr,emission,0,0);
            destination=CreateDestination(samples);
            pass.Begin(Context.Get(),viewport,samples); DrawRectangle(pass,-0.125f,0.125f,0.125f,-0.125f,0,1,0.5f,0,1);
            pass.Composite(Context.Get(),destination.Get(),nullptr,viewport); Readback(destination.Get());
            UINT halo=((9+24)*DestinationWidth+7+39)*4;
            Require(Pixels[halo+3]>0,"HDR selected emission did not produce a transparent bloom halo.");
            for(UINT offset=0;offset<Pixels.size();offset+=4) {
                Require(Pixels[offset]<=Pixels[offset+3] && Pixels[offset+1]<=Pixels[offset+3] && Pixels[offset+2]<=Pixels[offset+3],"HDR composite violated premultiplied-alpha bounds.");
            }
        }
        /// <summary>Requires contact visibility to attenuate diffuse indirect light while retaining direct sun and emission.</summary>
        void TestAoPreservesDirectAndEmission(UINT samples) {
            Win32CameraToonPass pass(Device.Get(),0,0,3); WaitForShaders(pass);
            CameraToonEffectsConfiguration effects; effects.ToonEnabled=false;
            effects.AmbientOcclusionStrength=1; effects.AmbientOcclusionRadius=0.35f;
            effects.AmbientOcclusionBias=0.01f; effects.AmbientOcclusionPixelRadius=12;
            D3D11_VIEWPORT viewport {7,9,64,48,0,1};
            UINT contact=((9+24)*DestinationWidth+7+30)*4;
            for(int source=0;source<2;++source) {
                PrepareMaterial(false,1,0,true);
                std::array<float,72> lights {};
                if(source==0) { lights[4]=1; lights[8]=lights[9]=lights[10]=1.25663706f; lights[14]=-1; }
                Context->UpdateSubresource(Lights.Get(),0,nullptr,lights.data(),0,0);
                const float emitted[]={0.4f,0.2f,0.1f,source==1 ? 1.0f : 0.0f};
                Context->UpdateSubresource(Emission.Get(),0,nullptr,emitted,0,0);
                std::vector<unsigned char> baseline;
                for(int enabled=0;enabled<2;++enabled) {
                    effects.AmbientOcclusionEnabled=enabled!=0; pass.ConfigureEffects(effects);
                    auto destination=CreateDestination(samples);
                    pass.Begin(Context.Get(),viewport,samples);
                    DrawRectangle(pass,-0.75f,0.6f,0,-0.6f);
                    DrawRectangle(pass,0,0.6f,0.75f,-0.6f,0,1,0.65f);
                    pass.Composite(Context.Get(),destination.Get(),nullptr,viewport); Readback(destination.Get());
                    if(enabled==0) baseline=Pixels;
                    else for(UINT channel=0;channel<4;++channel) Require(std::abs(int(Pixels[contact+channel])-int(baseline[contact+channel]))<=1,
                        source==0 ? "AO attenuated direct sunlight." : "AO attenuated emissive radiance.");
                }
            }
        }
        /// <summary>Reads one owned FP16 attachment pixel after resolving multisample values, preserving values above one.</summary>
        std::array<float,4> ReadHdrPixel(ID3D11RenderTargetView* target,UINT x,UINT y) {
            Microsoft::WRL::ComPtr<ID3D11Resource> resource;target->GetResource(&resource);
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;Check(resource.As(&texture),"Read HDR fixture texture");
            D3D11_TEXTURE2D_DESC descriptor{};texture->GetDesc(&descriptor);
            if(descriptor.SampleDesc.Count>1) {
                descriptor.SampleDesc.Count=1;descriptor.BindFlags=0;
                Microsoft::WRL::ComPtr<ID3D11Texture2D> resolved;Check(Device->CreateTexture2D(&descriptor,nullptr,&resolved),"Create HDR fixture resolve");
                Context->ResolveSubresource(resolved.Get(),0,texture.Get(),0,descriptor.Format);texture=resolved;
            }
            descriptor.Usage=D3D11_USAGE_STAGING;descriptor.CPUAccessFlags=D3D11_CPU_ACCESS_READ;descriptor.BindFlags=0;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;Check(Device->CreateTexture2D(&descriptor,nullptr,&staging),"Create HDR fixture readback");
            Context->CopyResource(staging.Get(),texture.Get());D3D11_MAPPED_SUBRESOURCE mapped{};Check(Context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped),"Map HDR fixture readback");
            const auto* pixel=reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(mapped.pData)+y*mapped.RowPitch)+x*4;
            std::array<float,4> result{};for(UINT channel=0;channel<4;++channel) result[channel]=DirectX::PackedVector::XMConvertHalfToFloat(pixel[channel]);
            Context->Unmap(staging.Get(),0);return result;
        }
        /// <summary>Requires independent FP16 indirect occlusion, unchanged direct/emissive alpha and correctly attenuated opaque lighting beneath glass.</summary>
        void TestGtaoHdrAndGlass(UINT samples) {
            Win32CameraToonPass pass(Device.Get(),0,0,3);WaitForShaders(pass);
            CameraToonEffectsConfiguration effects;effects.ToonEnabled=false;effects.ToneMappingEnabled=true;
            effects.AmbientOcclusionStrength=1;effects.AmbientOcclusionRadius=0.35f;effects.AmbientOcclusionBias=0.01f;effects.AmbientOcclusionPixelRadius=12;
            D3D11_VIEWPORT viewport{7,9,64,48,0,1};Win32MaterialOutputState output;output.Initialize(Device.Get());
            std::array<float,4> indirectBaseline{},indirectBlocked{},ambientUnblocked{},ambientBlocked{};
            for(int source=0;source<4;++source) for(int enabled=0;enabled<2;++enabled) {
                PrepareMaterial(false,1,0,true);
                std::array<float,72> lights{};
                if(source==0 || source==3) lights[0]=lights[1]=lights[2]=0.5f;
                if(source==1) {lights[4]=1;lights[8]=lights[9]=lights[10]=12.5663706f;lights[14]=-1;}
                Context->UpdateSubresource(Lights.Get(),0,nullptr,lights.data(),0,0);
                const float emitted[]={4,2,1,source==2 ? 1.0f:0.0f};Context->UpdateSubresource(Emission.Get(),0,nullptr,emitted,0,0);
                const float surface[]={1,1,1,source==1 || source==2 ? 0.35f:1.0f};Context->UpdateSubresource(Base.Get(),0,nullptr,surface,0,0);
                effects.AmbientOcclusionEnabled=enabled!=0;pass.ConfigureEffects(effects);
                auto sentinel=CreateAlbum();ID3D11ShaderResourceView* expectedViews[11]{};
                for(UINT index=0;index<11;++index) expectedViews[index]=sentinel.Get();Context->PSSetShaderResources(17,11,expectedViews);
                auto destination=CreateDestination(samples,DXGI_FORMAT_R16G16B16A16_FLOAT);pass.Begin(Context.Get(),viewport,samples);
                DrawRectangle(pass,-1,1,0,-1);DrawRectangle(pass,0,1,1,-1,0,1,0.65f);
                if(source==3) {
                    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> position,normal;ID3D11RenderTargetView* attachments[7]{};
                    Context->OMGetRenderTargets(7,attachments,nullptr);
                    for(UINT index=0;index<7;++index) {if(index==4) position.Attach(attachments[index]);else if(index==5) normal.Attach(attachments[index]);else if(attachments[index]) attachments[index]->Release();}
                    auto originalPosition=ReadHdrPixel(position.Get(),30,24),originalNormal=ReadHdrPixel(normal.Get(),30,24);
                    lights.fill(0);Context->UpdateSubresource(Lights.Get(),0,nullptr,lights.data(),0,0);
                    const float glassEmission[]={2,1,0.5f,1};Context->UpdateSubresource(Emission.Get(),0,nullptr,glassEmission,0,0);
                    const float glass[]={1,1,1,0.25f};Context->UpdateSubresource(Base.Get(),0,nullptr,glass,0,0);
                    output.Bind(Context.Get(),true,true,true,true);DrawRectangle(pass,-1,1,1,-1,0,1,0.2f);
                    Require(ReadHdrPixel(position.Get(),30,24)==originalPosition && ReadHdrPixel(normal.Get(),30,24)==originalNormal,"AO glass replaced the opaque occluder's geometry.");
                }
                pass.Composite(Context.Get(),destination.Get(),nullptr,viewport);
                ID3D11ShaderResourceView* restoredViews[11]{};Context->PSGetShaderResources(17,11,restoredViews);
                for(UINT index=0;index<11;++index) {bool same=restoredViews[index]==expectedViews[index];if(restoredViews[index]) restoredViews[index]->Release();Require(same,"GTAO leaked its SRVs or changed shared GI/probe slots17-27.");}
                ID3D11ShaderResourceView* emptyViews[11]{};Context->PSSetShaderResources(17,11,emptyViews);
                auto value=ReadHdrPixel(destination.Get(),37,33);
                if(source==0) {
                    if(enabled==0) {indirectBaseline=value;ambientUnblocked=value;}
                    else {indirectBlocked=value;ambientBlocked=value;Require(value[0]<indirectBaseline[0]*0.9f,"FP16 AO failed to attenuate diffuse indirect radiance.");}
                } else if(source==1 || source==2) {
                    if(enabled==0) indirectBaseline=value;
                    else for(UINT channel=0;channel<4;++channel) Require(std::abs(value[channel]-indirectBaseline[channel])<0.002f,"FP16 AO changed direct/emissive radiance or authored alpha.");
                    Require(value[0]>value[3],"HDR fixture lost highlight radiance above premultiplied white.");
                    Require(std::abs(value[3]-0.35f)<0.002f,"FP16 AO changed translucent surface alpha.");
                } else {
                    // All glass-case channels stay below the linear scene shoulder at one.
                    if(enabled==0) indirectBaseline=value;
                    else {
                        Require(value[0]<indirectBaseline[0]-0.01f,"Glass discarded the opaque surface's indirect AO.");
                        Require(value[0]>0.49f,"AO attenuated glass emission rather than only its transmitted indirect background.");
                    }
                    const auto& ambient=enabled==0 ? ambientUnblocked:ambientBlocked;
                    const float glassRadiance[]={0.5f,0.25f,0.125f};
                    for(UINT channel=0;channel<3;++channel) Require(std::abs(value[channel]-(ambient[channel]*0.75f+glassRadiance[channel]))<0.003f,"Glass AO failed indirect transmittance/emission separation.");
                    Require(std::abs(value[3]-1)<0.002f,"AO glass changed accumulated opaque alpha.");
                }
            }
        }
        /// <summary>Requires translated camera equivalence, immediate occluder movement and clean viewport borders without temporal data.</summary>
        void TestGtaoCameraAndMotion(UINT samples) {
            Win32CameraToonPass pass(Device.Get(),0,0,3);WaitForShaders(pass);
            CameraToonEffectsConfiguration effects;effects.ToonEnabled=false;effects.AmbientOcclusionEnabled=true;
            effects.AmbientOcclusionStrength=1;effects.AmbientOcclusionRadius=0.35f;effects.AmbientOcclusionBias=0.01f;effects.AmbientOcclusionPixelRadius=12;
            pass.ConfigureEffects(effects);D3D11_VIEWPORT viewport{7,9,64,48,0,1};
            std::vector<unsigned char> reference;
            UINT contact=((9+24)*DestinationWidth+7+30)*4;
            int blocked=0;
            for(int frame=0;frame<5;++frame) {
                PrepareMaterial(false,1,0,true);
                std::array<float,72> lights{};lights[0]=lights[1]=lights[2]=0.5f;
                Context->UpdateSubresource(Lights.Get(),0,nullptr,lights.data(),0,0);
                if(frame==1) {
                    std::array<float,76> transforms{};
                    for(UINT matrix=0;matrix<3;++matrix) for(UINT diagonal=0;diagonal<4;++diagonal) transforms[matrix*16+diagonal*5]=1;
                    transforms[3]=2;transforms[7]=1;transforms[11]=2;
                    transforms[48]=2;transforms[49]=1;transforms[50]=4;
                    Context->UpdateSubresource(Transforms.Get(),0,nullptr,transforms.data(),0,0);
                }
                auto destination=CreateDestination(samples);pass.Begin(Context.Get(),viewport,samples);
                DrawRectangle(pass,-1,1,0,-1);
                if(frame<2 || frame==4) DrawRectangle(pass,0,1,1,-1,0,1,0.65f);
                else if(frame==2) DrawRectangle(pass,0.6f,1,1,-1,0,1,0.65f);
                pass.Composite(Context.Get(),destination.Get(),nullptr,viewport);Readback(destination.Get());
                if(frame==0) {reference=Pixels;blocked=Pixels[contact];Require(blocked<119,"Dynamic corner was not visibly occluded.");}
                if(frame==1) for(UINT channel=0;channel<4;++channel) Require(std::abs(int(Pixels[contact+channel])-int(reference[contact+channel]))<=2,"GTAO depends on the world origin instead of the active camera.");
                if(frame==2 || frame==3) Require(Pixels[contact]>blocked+8,"Moved or removed occluder retained previous-frame AO.");
                if(frame==4) Require(std::abs(int(Pixels[contact])-blocked)<=1,"Returning occluder did not produce same-frame visibility.");
                UINT border=((9+24)*DestinationWidth+7)*4;
                Require(std::abs(int(Pixels[border])-128)<=1,"Missing offscreen samples darkened a flat viewport border.");
                UINT distant=((9+24)*DestinationWidth+7+12)*4;
                Require(std::abs(int(Pixels[distant])-128)<=1,"Spatial denoising spread contact shadow beyond its bounded radius.");
            }
        }
        /// <summary>Reports warmed synthetic 1080p GPU timings for enabled/disabled AO without predicting target-GPU frame rate.</summary>
        void TestGtaoGpuTime() {
            Win32CameraToonPass pass(Device.Get(),0,0,3);WaitForShaders(pass);
            D3D11_TEXTURE2D_DESC descriptor{};descriptor.Width=1920;descriptor.Height=1080;descriptor.MipLevels=1;descriptor.ArraySize=1;
            descriptor.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;descriptor.SampleDesc.Count=1;descriptor.BindFlags=D3D11_BIND_RENDER_TARGET;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;Microsoft::WRL::ComPtr<ID3D11RenderTargetView> destination;
            Check(Device->CreateTexture2D(&descriptor,nullptr,&texture),"Create GTAO timing texture");Check(Device->CreateRenderTargetView(texture.Get(),nullptr,&destination),"Create GTAO timing output");
            D3D11_VIEWPORT viewport{0,0,1920,1080,0,1};PrepareMaterial(false,1,0,true);
            Microsoft::WRL::ComPtr<ID3D11Query> disjoint,start,end;D3D11_QUERY_DESC query{};query.Query=D3D11_QUERY_TIMESTAMP_DISJOINT;
            Check(Device->CreateQuery(&query,&disjoint),"Create GTAO timing interval");query.Query=D3D11_QUERY_TIMESTAMP;
            Check(Device->CreateQuery(&query,&start),"Create GTAO start timestamp");Check(Device->CreateQuery(&query,&end),"Create GTAO end timestamp");
            for(UINT samples:{1u,4u}) for(int enabled=0;enabled<2;++enabled) {
                CameraToonEffectsConfiguration effects;effects.ToonEnabled=false;effects.AmbientOcclusionEnabled=enabled!=0;
                effects.AmbientOcclusionStrength=0.65f;effects.AmbientOcclusionRadius=0.22f;effects.AmbientOcclusionBias=0.008f;effects.AmbientOcclusionPixelRadius=24;
                pass.ConfigureEffects(effects);
                for(int iteration=0;iteration<4;++iteration) {
                    if(iteration==3) {Context->Begin(disjoint.Get());Context->End(start.Get());}
                    pass.Begin(Context.Get(),viewport,samples);DrawRectangle(pass,-1,1,0,-1);DrawRectangle(pass,0,1,1,-1,0,1,0.65f);
                    pass.Composite(Context.Get(),destination.Get(),nullptr,viewport);
                    if(iteration==3) {Context->End(end.Get());Context->End(disjoint.Get());}
                }
                Context->Flush();D3D11_QUERY_DATA_TIMESTAMP_DISJOINT interval{};HRESULT result;ULONGLONG deadline=GetTickCount64()+10000;
                while((result=Context->GetData(disjoint.Get(),&interval,sizeof(interval),0))==S_FALSE) {Require(GetTickCount64()<deadline,"GTAO timing query timed out.");Sleep(0);}
                Check(result,"Read GTAO timing interval");UINT64 first=0,last=0;
                Require(Context->GetData(start.Get(),&first,sizeof(first),0)==S_OK && Context->GetData(end.Get(),&last,sizeof(last),0)==S_OK,"GTAO timestamps incomplete.");
                Require(!interval.Disjoint && interval.Frequency>0,"GTAO timing frequency changed.");
                std::cout<<"Synthetic1920x1080 "<<samples<<"x AO="<<enabled<<" GPU(ms): "<<1000.0*double(last-first)/double(interval.Frequency)<<" (whole camera pass; current GPU only).\n";
            }
        }
        /// Compares a nearby depth step with coplanar, separated, disabled and empty-background controls.
        void TestContactOcclusion(UINT samples) {
            Win32CameraToonPass pass(Device.Get(),0,0,3); WaitForShaders(pass);
            CameraToonEffectsConfiguration effects; effects.ToonEnabled=false;
            effects.AmbientOcclusionStrength=1; effects.AmbientOcclusionRadius=0.35f;
            effects.AmbientOcclusionBias=0.01f; effects.AmbientOcclusionPixelRadius=12;
            D3D11_VIEWPORT viewport {7,9,64,48,0,1};
            PrepareMaterial(false,1,0,true);
            std::array<float,72> lights {}; lights[0]=lights[1]=lights[2]=0.5f;
            Context->UpdateSubresource(Lights.Get(),0,nullptr,lights.data(),0,0);
            UINT contact=((9+24)*DestinationWidth+7+30)*4;
            UINT distant=((9+24)*DestinationWidth+7+12)*4;
            int unoccluded=0,occluded=0;
            for(int control=0;control<5;++control) {
                effects.AmbientOcclusionEnabled=control!=0; pass.ConfigureEffects(effects);
                auto destination=CreateDestination(samples);
                pass.Begin(Context.Get(),viewport,samples);
                DrawRectangle(pass,-0.75f,0.6f,0,-0.6f);
                if(control!=4) DrawRectangle(pass,0,0.6f,0.75f,-0.6f,0,1,control==2 ? 0.5f : control==3 ? 0.99f : 0.65f);
                pass.Composite(Context.Get(),destination.Get(),nullptr,viewport); Readback(destination.Get());
                if(control==0) unoccluded=Pixels[contact];
                else if(control==1) {
                    occluded=Pixels[contact];
                    Require(occluded+8<unoccluded,"Nearby depth geometry did not produce contact occlusion.");
                    Require(Pixels[distant]>occluded+8,"Contact occlusion darkened a remote coplanar surface equally.");
                } else Require(std::abs(static_cast<int>(Pixels[contact])-unoccluded)<=1,"AO occluded coplanar, distant or empty geometry.");
                UINT background=((9+3)*DestinationWidth+7+60)*4;
                Require(Pixels[background+3]==0 && Pixels[background]==0,"AO painted the empty transparent background.");
            }
        }
        /// <summary>Proves distinct solid walls share a visible corner while their interiors and viewport border remain unmarked.</summary>
        void TestRegisteredWallCorners(UINT samples) {
            Win32CameraToonPass pass(Device.Get(),3,2,3);WaitForShaders(pass);
            CameraToonEffectsConfiguration effects;effects.ToonEnabled=false;pass.ConfigureEffects(effects);
            int back=0,left=0;pass.ConfigureMaterialEffects(&back,0,0,false,true,1.5f);pass.ConfigureMaterialEffects(&left,0,0,false,true,1.5f);
            D3D11_VIEWPORT viewport{7,9,64,48,0,1};PrepareMaterial(false,1,0,true);auto destination=CreateDestination(samples);
            pass.Begin(Context.Get(),viewport,samples);
            DrawRectangle(pass,-1,1,0,-1,0,1,0.5f,0,0,&back);DrawRectangle(pass,0,1,1,-1,0,1,0.5f,0,0,&left);
            pass.Composite(Context.Get(),destination.Get(),nullptr,viewport);Readback(destination.Get());
            Require(Pixels[((9+24)*DestinationWidth+7+31)*4]<10 && Pixels[((9+24)*DestinationWidth+7+32)*4]<10,"Adjacent registered walls lost their shared ink corner.");
            ExpectChannel(7+8,9+24,0,255,"Wall contour darkened a uniform interior.");
            ExpectChannel(7+55,9+24,0,255,"Wall contour darkened the neighboring uniform interior.");
            ExpectChannel(7,9+24,0,255,"Viewport border created a false room contour.");
        }
        /// Proves only an explicitly registered material receives quantization and thick contours over an ordinary room surface.
        void TestRegisteredToonSurface(UINT samples) {
            Win32CameraToonPass pass(Device.Get(),3,2,3); WaitForShaders(pass);
            CameraToonEffectsConfiguration effects; effects.ToonEnabled=false; pass.ConfigureEffects(effects);
            int sofa=0; pass.ConfigureMaterialEffects(&sofa,0,0,false,true,1.5f);
            D3D11_VIEWPORT viewport {7,9,64,48,0,1};
            PrepareMaterial(false,1,0,true);
            auto destination=CreateDestination(samples);
            pass.Begin(Context.Get(),viewport,samples);
            DrawRectangle(pass,-0.7f,0.5f,-0.1f,-0.5f,0.8f,0.6f,0.5f,0,0,&sofa);
            DrawRectangle(pass,0.1f,0.5f,0.7f,-0.5f,0.8f,0.6f);
            pass.Composite(Context.Get(),destination.Get(),nullptr,viewport); Readback(destination.Get());
            ExpectChannel(7+20,9+24,0,128,"Registered sofa did not use stepped light bands.");
            ExpectChannel(7+44,9+24,0,153,"Unregistered room material was quantized.");
            ExpectChannel(7+7,9+24,3,255,"Registered sofa thick outline did not extend beyond its geometry.");
            ExpectChannel(7+57,9+24,3,0,"Unregistered room material received an outside contour.");
            destination=CreateDestination(samples);
            pass.Begin(Context.Get(),viewport,samples);
            DrawRectangle(pass,-1,1,1,-1,0,1,0.7f);
            DrawRectangle(pass,-0.7f,0.5f,-0.1f,-0.5f,0.8f,0.6f,0.3f,0,0,&sofa);
            pass.Composite(Context.Get(),destination.Get(),nullptr,viewport); Readback(destination.Get());
            Require(Pixels[((9+24)*DestinationWidth+7+7)*4]<10,"Sofa contour disappeared against an opaque room surface.");
            pass.RemoveMaterialEffects(&sofa); destination=CreateDestination(samples);
            pass.Begin(Context.Get(),viewport,samples); DrawRectangle(pass,-0.7f,0.5f,-0.1f,-0.5f,0.8f,0.6f,0.5f,0,0,&sofa);
            pass.Composite(Context.Get(),destination.Get(),nullptr,viewport); Readback(destination.Get());
            ExpectChannel(7+7,9+24,3,0,"Removed sofa identity retained contour eligibility.");
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
        /// Isolates part-identity contours from optional sharp-normal creases and requires separate coplanar pieces to remain outlined.
        void TestPartIdentity(Win32CameraToonPass& pass, UINT samples) {
            CameraToonEffectsConfiguration legacy;legacy.CreaseAngleDegrees=180;pass.ConfigureEffects(legacy);
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
            effects.Exposure=17;
            bool displayRejected=false;
            try { pass.ConfigureEffects(effects); } catch(const std::invalid_argument&) {displayRejected=true;}
            Require(displayRejected,"Unbounded HDR exposure was accepted.");
            effects.Exposure=1; effects.AmbientOcclusionPixelRadius=65; displayRejected=false;
            try { pass.ConfigureEffects(effects); } catch(const std::invalid_argument&) {displayRejected=true;}
            Require(displayRejected,"Unbounded contact occlusion footprint was accepted.");
            effects.AmbientOcclusionPixelRadius=24;
            int materialIdentity=0; displayRejected=false;
            try { pass.ConfigureMaterialEffects(&materialIdentity,0,0,false,true,5); } catch(const std::invalid_argument&) {displayRejected=true;}
            Require(displayRejected,"Unbounded material contour width was accepted.");
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
        if(argc==2 && std::string(argv[1])=="--thin-surface") { helengine::windows::CameraToonTests tests;tests.RunThinSurface();return 0; }
        if(argc==2 && std::string(argv[1])=="--normal-layer") { helengine::windows::CameraToonTests tests;tests.RunNormalLayer();return 0; }
        if(argc==2 && (std::string(argv[1])=="--external-only" || std::string(argv[1])=="--depth-sobel")) { helengine::windows::CameraToonTests tests;tests.RunDepthSobel();return 0; }
        if(argc==2 && std::string(argv[1])=="--depth-opening") { helengine::windows::CameraToonTests tests;tests.RunDepthOpening();return 0; }
        if(argc>=2 && std::string(argv[1])=="--silhouette-bounds") { helengine::windows::CameraToonTests tests(argc>2 ? argv[2] : "");tests.RunSilhouetteBounds();return 0; }
        if(argc==2 && std::string(argv[1])=="--gtao") {
            helengine::windows::CameraToonTests tests;tests.RunGtao();return 0;
        }
        if(argc==2 && std::string(argv[1])=="--creases") {
            helengine::windows::CameraToonTests tests;tests.RunCreases();return 0;
        }
        if(argc==2 && std::string(argv[1])=="--wall-ink") {
            helengine::windows::CameraToonTests tests;tests.RunWallInk();return 0;
        }
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
