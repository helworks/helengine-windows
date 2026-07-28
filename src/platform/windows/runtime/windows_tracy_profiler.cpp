#include "platform/windows/runtime/windows_tracy_profiler.hpp"

#if defined(TRACY_ENABLE)
namespace helengine::windows {
    namespace {
        /// Stores the Direct3D timestamp context shared by the Windows immediate rendering paths.
        TracyD3D11Ctx WindowsTracyProfilerGpuContext = nullptr;
    }

    /// Initializes the profiler-owned Direct3D 11 timestamp context for the active Windows device.
    void InitializeWindowsTracyProfiler(ID3D11Device* device, ID3D11DeviceContext* deviceContext) {
        if (device == nullptr || deviceContext == nullptr) {
            return;
        } else if (WindowsTracyProfilerGpuContext != nullptr) {
            return;
        }

        WindowsTracyProfilerGpuContext = TracyD3D11Context(device, deviceContext);
    }

    /// Marks the start of one profiler host frame after the Direct3D context has been initialized.
    void BeginWindowsTracyProfilerFrame() {
        FrameMarkStart("WindowsFrame");
    }

    /// Collects completed GPU timestamp queries without stalling the Windows frame loop.
    void CollectWindowsTracyProfilerGpu() {
        if (WindowsTracyProfilerGpuContext != nullptr) {
            TracyD3D11Collect(WindowsTracyProfilerGpuContext);
        }

        FrameMarkEnd("WindowsFrame");
    }

    /// Releases the profiler-owned Direct3D timestamp context before its device is destroyed.
    void ShutdownWindowsTracyProfiler() {
        if (WindowsTracyProfilerGpuContext != nullptr) {
            TracyD3D11Destroy(WindowsTracyProfilerGpuContext);
            WindowsTracyProfilerGpuContext = nullptr;
        }
    }

    /// Emits one named counter only when the profiler player profile is active.
    void EmitWindowsTracyProfilerPlot(const char* name, double value) {
        TracyPlot(name, value);
    }

    /// Returns the initialized Direct3D profiler context used by GPU zone macros.
    TracyD3D11Ctx GetWindowsTracyProfilerGpuContext() {
        return WindowsTracyProfilerGpuContext;
    }
}
#else
namespace helengine::windows {
    /// Initializes no profiler state when the selected player profile excludes Tracy.
    void InitializeWindowsTracyProfiler(ID3D11Device*, ID3D11DeviceContext*) {
    }

    /// Does nothing when the selected player profile excludes Tracy.
    void BeginWindowsTracyProfilerFrame() {
    }

    /// Does nothing when the selected player profile excludes Tracy.
    void CollectWindowsTracyProfilerGpu() {
    }

    /// Does nothing when the selected player profile excludes Tracy.
    void ShutdownWindowsTracyProfiler() {
    }

    /// Does nothing when the selected player profile excludes Tracy.
    void EmitWindowsTracyProfilerPlot(const char*, double) {
    }
}
#endif
