#pragma once
#include "directx11_shader_request.hpp"
#include <windows.h>
#include <wrl/client.h>
#include <exception>
#include <mutex>
#include <utility>
namespace helengine::windows {
    /// Retains only CPU requests and results while background work outlives its renderer owner.
    struct DirectX11ShaderCompilationState {
        /// Owns one compiled blob for each request in its original order.
        using Bytecodes=std::vector<Microsoft::WRL::ComPtr<ID3DBlob>>;
        /// Allocates every result and worker slot before any callback is submitted.
        explicit DirectX11ShaderCompilationState(std::vector<DirectX11ShaderRequest> requests)
            : Requests(std::move(requests)),Results(Requests.size()),Remaining(Requests.size()),ThreadIds(Requests.size()) {}
        /// Protects publication, cancellation and short readiness snapshots; never held during compilation.
        mutable std::mutex Mutex;
        /// Owns immutable translation units without retaining a camera, renderer, device or context.
        const std::vector<DirectX11ShaderRequest> Requests;
        /// Publishes each independent result internally until the whole batch succeeds.
        Bytecodes Results;
        /// Preserves the first failure for every subsequent poll.
        std::exception_ptr Failure;
        /// Assigns exactly one distinct request to each submitted callback.
        std::size_t NextEntry=0;
        /// Counts all submitted callbacks which have not completed or observed cancellation.
        std::size_t Remaining;
        /// Counts compiler calls currently executing outside the lock.
        UINT Active=0;
        /// Records observed worker overlap for renderer diagnostics.
        UINT MaximumActive=0;
        /// Records which worker actually compiled each entry.
        std::vector<DWORD> ThreadIds;
        /// Prevents further publication after the owning handle is released.
        bool Cancelled=false;
    };
}
