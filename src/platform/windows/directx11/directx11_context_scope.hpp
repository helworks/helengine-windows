#pragma once
#include <d3d11_1.h>
#include <stdexcept>
#include <wrl/client.h>

namespace helengine::windows {
    /// Restores the caller's complete immediate-context state even when an isolated GPU operation throws.
    class DirectX11ContextScope {
        /// Borrows the immediate context for this stack-bound operation.
        ID3D11DeviceContext1* Context;
        /// Retains the exact state displaced by the isolated operation.
        Microsoft::WRL::ComPtr<ID3DDeviceContextState> Previous;
    public:
        /// Swaps to a valid reusable engine-owned state without changing the caller's saved bindings.
        DirectX11ContextScope(ID3D11DeviceContext1* context,ID3DDeviceContextState* state) : Context(context) {
            if(context==nullptr || state==nullptr) { throw std::invalid_argument("An isolated GPU context and state are required."); }
            Context->SwapDeviceContextState(state,&Previous);
        }
        /// Restores every context binding before leaving the GPU operation's scope.
        ~DirectX11ContextScope() { Context->SwapDeviceContextState(Previous.Get(),nullptr); }
        /// Prevents two scope instances from restoring the same displaced state.
        DirectX11ContextScope(const DirectX11ContextScope&)=delete;
        /// Prevents replacing the state that an active scope must restore.
        DirectX11ContextScope& operator=(const DirectX11ContextScope&)=delete;
    };
}