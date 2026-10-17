#include "platform/windows/runtime/windows_tracy_profiler.hpp"
#include "platform/windows/runtime/windows_cpu_profile_sink.hpp"

#if defined(TRACY_ENABLE) && __has_include("IRuntimeCpuProfileSink.hpp")
#include "Core.hpp"
#include "ObjectManager.hpp"
namespace helengine::windows {
    /// Provides static storage for stable Tracy plot-name pointers until process termination.
    WindowsCpuProfileSink WindowsCpuProfileSinkInstance;

    /// Supplies the existing shared Core hook without changing the Core ABI or generated sources.
    ::IRuntimeCpuProfileSink* GetWindowsCpuProfileSink() {
        return &WindowsCpuProfileSinkInstance;
    }

    /// Emits collected frame data after all timed Core work has completed.
    void FlushWindowsCpuProfileSink(::Core* core) {
        HELENGINE_TRACY_ZONE_N("Profiler.EmitComponentSamples");
        WindowsCpuProfileSinkInstance.FlushFrame();
        auto* objects = core->get_ObjectManager();
        TracyPlot("Core.Entities.Registered", static_cast<std::int64_t>(objects->get_Entities()->get_Count()));
        TracyPlot("Core.Updateables.Registered", static_cast<std::int64_t>(objects->get_Updateables()->get_Count()));
        TracyPlot("Render.Drawables2D.Registered", static_cast<std::int64_t>(objects->get_Drawables2D()->get_Count()));
        TracyPlot("Render.Drawables3D.Registered", static_cast<std::int64_t>(objects->get_Drawables3D()->get_Count()));
        TracyPlot("Render.Cameras.Registered", static_cast<std::int64_t>(objects->get_Cameras()->get_Count()));
    }
}
#endif

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
