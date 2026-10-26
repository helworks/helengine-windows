#pragma once
#include "platform/windows/directx11/directx11_shader_compilation_state.hpp"
#include <memory>

namespace helengine::windows {
    /// Schedules independent HLSL entries on Windows workers and exposes nonblocking, all-or-nothing completion.
    class DirectX11ShaderCompilation {
    public:
        /// Preserves the request order for render-thread device setup.
        using Bytecodes = DirectX11ShaderCompilationState::Bytecodes;
        /// Validates owned requests and submits every independent entry without compiling on the caller thread.
        explicit DirectX11ShaderCompilation(std::vector<DirectX11ShaderRequest> requests);
        /// Cancels publication without joining or waiting for an uninterruptible compiler call.
        ~DirectX11ShaderCompilation();
        /// Keeps cancellation owned by one view instead of copying a handle which can revoke another owner.
        DirectX11ShaderCompilation(const DirectX11ShaderCompilation&) = delete;
        /// Prevents assigning competing cancellation ownership to one shared compilation state.
        DirectX11ShaderCompilation& operator=(const DirectX11ShaderCompilation&) = delete;
        /// Copies a complete successful set, returns false while pending, and rethrows any retained compiler error.
        bool TryGetBytecodes(Bytecodes& result) const;
        /// Reports observed compile overlap without changing execution or waiting for callbacks.
        UINT MaximumConcurrentCompilations() const;
        /// Reports the actual worker for each started entry, independently of pass or camera lifetime.
        std::vector<DWORD> WorkerThreads() const;
    private:
        /// Compiles one entry outside the publication lock and closes callback ownership after the last completion.
        static void CALLBACK RunEntry(PTP_CALLBACK_INSTANCE instance, void* context, PTP_WORK work) noexcept;
        /// Compiles the exact existing entry/profile/sample combination and retains full HLSL diagnostics on failure.
        static Microsoft::WRL::ComPtr<ID3DBlob> Compile(const DirectX11ShaderRequest& request);
        /// Shares CPU-only state with outstanding callbacks without making them borrow this object.
        std::shared_ptr<DirectX11ShaderCompilationState> State;
    };
}