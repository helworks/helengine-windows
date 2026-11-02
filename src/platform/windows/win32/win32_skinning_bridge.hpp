#pragma once
#include "platform/windows/win32/win32_render_bridge.hpp"
#include "platform/windows/win32/win32_gpu_skin.hpp"
#include "runtime/array.hpp"
#include <memory>
#include <cstdio>
#include <unordered_map>

namespace helengine::windows {
    /// <summary>Owns optional skins by nonreusable opaque handle; resources release normally at process shutdown.</summary>
    inline std::unordered_map<int64_t,std::unique_ptr<Win32GpuSkin>> AnimationGpuSkins;
    /// <summary>Issues positive identities without reusing deleted handles or exposing native object pointers.</summary>
    inline int64_t NextAnimationGpuSkinHandle=1;
}

/// <summary>Attaches compute skinning only to the caller's private uploaded model, preserving all renderer pass paths.</summary>
inline int64_t HelAnimationCreateGpuSkin(RenderManager3D* renderer, RuntimeModel* instanceModel, Array<int32_t>* joints, Array<float>* weights, int32_t jointCount) {
    using namespace helengine::windows;
    if(!renderer || !dynamic_cast<Win32RenderManager3D*>(renderer)) throw std::invalid_argument("GPU skin requires the Windows Direct3D renderer");
    auto* model=dynamic_cast<Win32RuntimeModel*>(instanceModel);
    if(!model || !joints || !weights || !joints->Data || !weights->Data || joints->Length<=0 || joints->Length!=weights->Length || jointCount<=0)
        throw std::invalid_argument("GPU skin requires a private Windows model and matching authored influence arrays");
    for(const auto& entry:AnimationGpuSkins) if(entry.second->GetTarget()==std::addressof(model->VertexBuffer))
        throw std::logic_error("A GPU skin is already attached to this runtime model instance");
    if(NextAnimationGpuSkinHandle==std::numeric_limits<int64_t>::max()) throw std::overflow_error("GPU skin handle identities exhausted");
    const int64_t handle=NextAnimationGpuSkinHandle++;
    auto insertion=AnimationGpuSkins.try_emplace(handle,nullptr);
    try {
        insertion.first->second=std::make_unique<Win32GpuSkin>(model->VertexBuffer,model->VertexCount,joints->Data,weights->Data,static_cast<size_t>(joints->Length),static_cast<UINT>(jointCount));
    } catch(...) {
        AnimationGpuSkins.erase(insertion.first);
        throw;
    }
    std::fprintf(stderr,"[animation] GPU compute skin created: handle=%lld vertices=%u joints=%d\n",static_cast<long long>(handle),model->VertexCount,jointCount);
    return handle;
}

/// <summary>Dispatches the completed row-vector palette once before any camera or shadow renders the instance.</summary>
inline void HelAnimationUpdateGpuSkin(int64_t handle, Array<float>* palette) {
    const auto found=helengine::windows::AnimationGpuSkins.find(handle);
    if(found==helengine::windows::AnimationGpuSkins.end()) throw std::invalid_argument("Unknown or destroyed GPU skin handle");
    if(!palette || !palette->Data || palette->Length<=0) throw std::invalid_argument("GPU skin requires a complete authored palette");
    found->second->Update(palette->Data,static_cast<size_t>(palette->Length));
}

/// <summary>Detaches the original vertex buffer and releases the skin before its private model is unloaded.</summary>
inline void HelAnimationDestroyGpuSkin(int64_t handle) {
    const auto found=helengine::windows::AnimationGpuSkins.find(handle);
    if(found==helengine::windows::AnimationGpuSkins.end()) throw std::invalid_argument("Unknown or destroyed GPU skin handle");
    found->second->Restore();
    helengine::windows::AnimationGpuSkins.erase(found);
    std::fprintf(stderr,"[animation] GPU compute skin destroyed: handle=%lld\n",static_cast<long long>(handle));
}


/// <summary>Releases renderer buffers and destroys the caller-owned instance only after every GPU skin has detached.</summary>
inline void HelAnimationReleaseInstanceModel(RenderManager3D* renderer, RuntimeModel* instanceModel) {
    using namespace helengine::windows;
    if(!renderer || !dynamic_cast<Win32RenderManager3D*>(renderer)) throw std::invalid_argument("Animated model release requires the Windows Direct3D renderer");
    auto* model=dynamic_cast<Win32RuntimeModel*>(instanceModel);
    if(!model) throw std::invalid_argument("Animated model release requires a private Windows model instance");
    for(const auto& entry:AnimationGpuSkins) if(entry.second->GetTarget()==std::addressof(model->VertexBuffer))
        throw std::logic_error("Detach the GPU skin before releasing its runtime model instance");
    renderer->ReleaseModel(model);
    model->Dispose();
    delete model;
}

