#include "platform/windows/directx11/directx11_shader_compilation.hpp"
#include <algorithm>
#include <stdexcept>
#include <system_error>

namespace helengine::windows {
    DirectX11ShaderCompilation::DirectX11ShaderCompilation(std::vector<DirectX11ShaderRequest> requests) {
        if(requests.empty()) throw std::invalid_argument("Shader compilation requires at least one request.");
        for(const auto& request:requests) {
            if(request.Source.empty() || request.SourceName.empty() || request.Entry.empty() || request.Profile.empty())
                throw std::invalid_argument("Shader source, diagnostic name, entry and profile are required.");
            for(const auto& macro:request.Macros) {
                if(macro.Name.empty()) throw std::invalid_argument("Shader macro names cannot be empty.");
            }
        }
        State=std::make_shared<DirectX11ShaderCompilationState>(std::move(requests));
        auto context=std::make_unique<std::shared_ptr<DirectX11ShaderCompilationState>>(State);
        PTP_WORK work=CreateThreadpoolWork(&RunEntry,context.get(),nullptr);
        if(work==nullptr) throw std::system_error(static_cast<int>(GetLastError()),std::system_category(),"Create DirectX11 shader compilation work");
        context.release();
        for(std::size_t entry=0;entry<State->Requests.size();++entry) SubmitThreadpoolWork(work);
    }

    DirectX11ShaderCompilation::~DirectX11ShaderCompilation() {
        std::lock_guard<std::mutex> lock(State->Mutex);
        State->Cancelled=true;
    }

    bool DirectX11ShaderCompilation::TryGetBytecodes(Bytecodes& result) const {
        std::lock_guard<std::mutex> lock(State->Mutex);
        if(State->Failure) std::rethrow_exception(State->Failure);
        if(State->Remaining!=0) return false;
        result=State->Results;
        return true;
    }

    UINT DirectX11ShaderCompilation::MaximumConcurrentCompilations() const {
        std::lock_guard<std::mutex> lock(State->Mutex);
        return State->MaximumActive;
    }

    std::vector<DWORD> DirectX11ShaderCompilation::WorkerThreads() const {
        std::lock_guard<std::mutex> lock(State->Mutex);
        return State->ThreadIds;
    }

    Microsoft::WRL::ComPtr<ID3DBlob> DirectX11ShaderCompilation::Compile(const DirectX11ShaderRequest& request) {
        std::vector<D3D_SHADER_MACRO> macros;
        macros.reserve(request.Macros.size()+1);
        for(const auto& macro:request.Macros) macros.push_back({macro.Name.c_str(),macro.Value.c_str()});
        macros.push_back({nullptr,nullptr});
        Microsoft::WRL::ComPtr<ID3DBlob> binary,errors;
        HRESULT result=D3DCompile(request.Source.data(),request.Source.size(),request.SourceName.c_str(),macros.data(),nullptr,
            request.Entry.c_str(),request.Profile.c_str(),request.Flags,0,&binary,&errors);
        if(FAILED(result)) throw std::runtime_error("DirectX11 shader "+request.SourceName+" / "+request.Entry+" ("+request.Profile+"): "
            +(errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()) : std::to_string(result)));
        return binary;
    }

    void CALLBACK DirectX11ShaderCompilation::RunEntry(PTP_CALLBACK_INSTANCE instance,void* context,PTP_WORK work) noexcept {
        auto* owner=static_cast<std::shared_ptr<DirectX11ShaderCompilationState>*>(context);
        auto state=*owner;
        std::size_t entry;
        bool compile;
        {
            std::lock_guard<std::mutex> lock(state->Mutex);
            entry=state->NextEntry++;
            compile=!state->Cancelled;
            if(compile) {
                state->ThreadIds[entry]=GetCurrentThreadId();
                ++state->Active;
                state->MaximumActive=std::max(state->MaximumActive,state->Active);
            }
        }
        Microsoft::WRL::ComPtr<ID3DBlob> binary;
        std::exception_ptr failure;
        try { if(compile) binary=Compile(state->Requests[entry]); }
        catch(...) { failure=std::current_exception(); }
        bool last;
        {
            std::lock_guard<std::mutex> lock(state->Mutex);
            if(compile) --state->Active;
            if(!state->Cancelled) {
                if(failure && !state->Failure) state->Failure=failure;
                else if(!failure) state->Results[entry]=std::move(binary);
            }
            last=--state->Remaining==0;
        }
        if(last) {
            delete owner;
            CloseThreadpoolWork(work);
        }
    }
}