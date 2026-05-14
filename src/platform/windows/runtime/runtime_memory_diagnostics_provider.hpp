#pragma once

#include "platform/windows/runtime/runtime_memory_snapshot.hpp"

#if defined(HELENGINE_WINDOWS_DEBUG_RUNTIME_DIAGNOSTICS) && __has_include("IRuntimeDiagnosticsProvider.hpp")
#include "IRuntimeDiagnosticsProvider.hpp"
#endif

#if defined(HELENGINE_WINDOWS_DEBUG_RUNTIME_DIAGNOSTICS) && __has_include("RuntimeMemoryDiagnosticsSnapshot.hpp")
#include "RuntimeMemoryDiagnosticsSnapshot.hpp"
#endif

#if defined(HELENGINE_WINDOWS_DEBUG_RUNTIME_DIAGNOSTICS) && __has_include("RuntimeDiagnosticsMetric.hpp")
#include "RuntimeDiagnosticsMetric.hpp"
#endif

#if defined(HELENGINE_WINDOWS_DEBUG_RUNTIME_DIAGNOSTICS)
#include "runtime/native_list.hpp"
#endif

namespace helengine::windows {
#if defined(HELENGINE_WINDOWS_DEBUG_RUNTIME_DIAGNOSTICS) && __has_include("IRuntimeDiagnosticsProvider.hpp") && __has_include("RuntimeMemoryDiagnosticsSnapshot.hpp")
    /// Captures Windows process memory counters for the shared engine runtime diagnostics service.
    class RuntimeMemoryDiagnosticsProvider : public IRuntimeDiagnosticsProvider {
    public:
        /// Captures the current Windows runtime diagnostics snapshot for the shared engine service.
        RuntimeMemoryDiagnosticsSnapshot* CaptureSnapshot() override;
    };
#endif
}
