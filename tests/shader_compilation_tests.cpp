#include "platform/windows/directx11/directx11_shader_compilation.hpp"
#include "platform/windows/win32/win32_camera_toon_shader_source.hpp"
#include "platform/windows/win32/win32_pearl_surface_shader.hpp"
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace helengine::windows {
    /// Tests the general backend compiler using owned HLSL only, without a window or a rendering context.
    class ShaderCompilationTests {
        /// Rejects a regression with a concrete diagnostic.
        static void Require(bool condition,const char* message) {
            if(!condition) throw std::runtime_error(message);
        }
        /// Bounds waiting in the fixture; the production API never waits.
        static void Await(DirectX11ShaderCompilation& compilation,DirectX11ShaderCompilation::Bytecodes& output) {
            auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
            while(!compilation.TryGetBytecodes(output)) {
                Require(std::chrono::steady_clock::now()<deadline,"Shader test timed out.");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        /// Creates a slow real effect variant to test independence from other batches and parallel worker execution.
        static DirectX11ShaderRequest SlowRequest() {
            return {std::string(PearlSurfaceShader)+CameraToonGeometryShader+CameraToonCompositeShader,
                "SlowFixture","CompositePS","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,{{"TOON_SAMPLES","4"}}};
        }
    public:
        /// Verifies arbitrary batch sizes, owned macro strings, independent progress, retained errors and cancellation.
        static void Run() {
            DWORD caller=GetCurrentThreadId();
            const std::string source="float4 Vertex(uint id:SV_VertexID):SV_Position{return float4(0,0,0,1);} "
                "float4 Color():SV_Target{return float4(TINT,0,0,1);}";
            std::vector<DirectX11ShaderRequest> requests {
                {source,"GeneralFixture","Vertex","vs_5_0",0,{{"TINT","0"}}},
                {source,"GeneralFixture","Color","ps_5_0",0,{{"TINT","0.25"}}},
                {source,"GeneralFixture","Color","ps_5_0",0,{{"TINT","0.75"}}}
            };
            auto start=std::chrono::steady_clock::now();
            DirectX11ShaderCompilation general(requests);
            Require(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<500,
                "Submitting shaders blocked the caller.");
            requests.clear();
            DirectX11ShaderCompilation::Bytecodes result;
            Await(general,result);
            Require(result.size()==3,"The backend compiler still assumes a fixed effect batch size.");
            for(const auto& code:result) Require(code && code->GetBufferSize()>0,"Ready shader bytecode is empty.");
            Require(result[1]->GetBufferSize()!=result[2]->GetBufferSize()
                || memcmp(result[1]->GetBufferPointer(),result[2]->GetBufferPointer(),result[1]->GetBufferSize())!=0,
                "Owned shader macros did not produce distinct variants.");
            for(DWORD thread:general.WorkerThreads()) Require(thread!=0 && thread!=caller,"Compilation ran on the render thread.");

            DirectX11ShaderCompilation slow({SlowRequest(),SlowRequest()});
            auto sentinel=result;
            if(!slow.TryGetBytecodes(result)) Require(result==sentinel,"A pending batch published partial results.");
            DirectX11ShaderCompilation fast({{source,"IndependentFixture","Color","ps_5_0",0,{{"TINT","1"}}}});
            Await(fast,result);
            Require(result.size()==1,"An independent pipeline did not become ready.");
            Await(slow,result);
            Require(result.size()==2 && slow.MaximumConcurrentCompilations()>1,"Shader jobs did not execute in parallel.");

            DirectX11ShaderCompilation invalid({{"invalid shader","BrokenFixture","Missing","ps_5_0",0,{}}});
            bool failed=false;
            auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
            while(!failed && std::chrono::steady_clock::now()<deadline) {
                try { invalid.TryGetBytecodes(result); }
                catch(const std::runtime_error& error) {
                    failed=std::string(error.what()).find("BrokenFixture")!=std::string::npos;
                }
                if(!failed) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            Require(failed,"Compiler diagnostics were lost.");
            failed=false;
            try { invalid.TryGetBytecodes(result); } catch(const std::runtime_error&) { failed=true; }
            Require(failed,"A shader failure was silently retried or cleared.");
            auto cancelled=std::make_unique<DirectX11ShaderCompilation>(std::vector<DirectX11ShaderRequest>{SlowRequest()});
            deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
            while(cancelled->MaximumConcurrentCompilations()==0) {
                Require(std::chrono::steady_clock::now()<deadline,"Cancellation fixture did not start.");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            start=std::chrono::steady_clock::now(); cancelled.reset();
            Require(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<500,
                "Cancelling a shader waited for a compiler worker.");
            bool rejected=false;
            try { DirectX11ShaderCompilation empty({}); } catch(const std::invalid_argument&) { rejected=true; }
            Require(rejected,"An empty batch was accepted.");
            std::cout<<"General DirectX11 shaders: arbitrary entries/macros, worker identity, parallel progress, errors and cancellation passed.\n";
        }
    };
}
/// Runs CPU-only shader jobs without opening the player or a native window.
int main() {
    try { helengine::windows::ShaderCompilationTests::Run(); return 0; }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
