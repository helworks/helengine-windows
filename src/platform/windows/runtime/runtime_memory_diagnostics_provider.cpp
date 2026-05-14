#include "platform/windows/runtime/runtime_memory_diagnostics_provider.hpp"

namespace helengine::windows {
#if defined(HELENGINE_WINDOWS_DEBUG_RUNTIME_DIAGNOSTICS) && __has_include("IRuntimeDiagnosticsProvider.hpp") && __has_include("RuntimeMemoryDiagnosticsSnapshot.hpp")
    /// Captures the current Windows runtime diagnostics snapshot for the shared engine service.
    RuntimeMemoryDiagnosticsSnapshot* RuntimeMemoryDiagnosticsProvider::CaptureSnapshot() {
        RuntimeMemorySnapshot nativeSnapshot = RuntimeMemorySnapshot::Capture();
        RuntimeMemoryDiagnosticsSnapshot* snapshot = new RuntimeMemoryDiagnosticsSnapshot();
        snapshot->set_ResidentBytes(nativeSnapshot.WorkingSetBytes);
        snapshot->set_PeakResidentBytes(nativeSnapshot.PeakWorkingSetBytes);
        snapshot->set_CommittedBytes(nativeSnapshot.PrivateUsageBytes);
        snapshot->set_PeakCommittedBytes(nativeSnapshot.PeakPagefileUsageBytes);
        snapshot->set_AvailablePhysicalBytes(nativeSnapshot.AvailablePhysicalBytes);
        snapshot->set_PageFaultCount(nativeSnapshot.PageFaultCount);
        snapshot->get_DetailMetrics()->Add(new RuntimeDiagnosticsMetric("pagefile_usage_bytes", nativeSnapshot.PagefileUsageBytes));
        snapshot->get_DetailMetrics()->Add(new RuntimeDiagnosticsMetric("quota_paged_pool_bytes", nativeSnapshot.QuotaPagedPoolBytes));
        snapshot->get_DetailMetrics()->Add(new RuntimeDiagnosticsMetric("quota_nonpaged_pool_bytes", nativeSnapshot.QuotaNonPagedPoolBytes));
        snapshot->get_DetailMetrics()->Add(new RuntimeDiagnosticsMetric("system_commit_total_bytes", nativeSnapshot.SystemCommitTotalBytes));
        snapshot->get_DetailMetrics()->Add(new RuntimeDiagnosticsMetric("system_commit_limit_bytes", nativeSnapshot.SystemCommitLimitBytes));
        return snapshot;
    }
#endif
}
