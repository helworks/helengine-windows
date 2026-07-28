#pragma once

#include <d3d11.h>

namespace helengine::windows {
    /// Initializes the profiler-owned Direct3D 11 timestamp context for the active Windows device.
    /// <param name="device">Direct3D device that owns timestamp-query allocation.</param>
    /// <param name="deviceContext">Immediate Direct3D context that submits timestamp queries.</param>
    void InitializeWindowsTracyProfiler(ID3D11Device* device, ID3D11DeviceContext* deviceContext);

    /// Marks the start of one profiler host frame after the Direct3D context has been initialized.
    /// Direct3D 11 GPU scopes submit their timestamp-query boundaries directly, so its Tracy API has no separate new-frame call.
    void BeginWindowsTracyProfilerFrame();

    /// Collects completed GPU timestamp queries without stalling the Windows frame loop.
    void CollectWindowsTracyProfilerGpu();

    /// Releases the profiler-owned Direct3D timestamp context before its device is destroyed.
    void ShutdownWindowsTracyProfiler();

    /// Emits one named counter only when the profiler player profile is active.
    /// <param name="name">Stable Tracy plot name.</param>
    /// <param name="value">Measured runtime-owned counter value.</param>
    void EmitWindowsTracyProfilerPlot(const char* name, double value);
}

#if defined(TRACY_ENABLE)
#include <tracy/Tracy.hpp>
#include <tracy/TracyD3D11.hpp>

#define HELENGINE_TRACY_ZONE_N(name) ZoneScopedN(name)
#define HELENGINE_TRACY_GPU_ZONE_N(name) TracyD3D11Zone(::helengine::windows::GetWindowsTracyProfilerGpuContext(), name)
#define HELENGINE_TRACY_PLOT(name, value) ::helengine::windows::EmitWindowsTracyProfilerPlot(name, value)

namespace helengine::windows {
    /// Returns the initialized Direct3D profiler context used by GPU zone macros.
    /// <returns>Profiler-owned Tracy Direct3D context, or null until initialization completes.</returns>
    TracyD3D11Ctx GetWindowsTracyProfilerGpuContext();
}
#else
#define HELENGINE_TRACY_ZONE_N(name)
#define HELENGINE_TRACY_GPU_ZONE_N(name)
#define HELENGINE_TRACY_PLOT(name, value)
#endif
