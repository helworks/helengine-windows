#include "platform/windows/runtime/runtime_memory_snapshot.hpp"

#include <Windows.h>
#include <psapi.h>

#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace helengine::windows {
    namespace {
        /// Converts one raw byte count into a compact mebibyte string for diagnostics logs.
        std::string FormatMiB(std::uint64_t bytes) {
            std::ostringstream builder;
            builder << std::fixed << std::setprecision(2) << (static_cast<double>(bytes) / (1024.0 * 1024.0));
            builder << " MiB";
            return builder.str();
        }
    }

    /// Samples the current process memory counters from the Windows process API.
    RuntimeMemorySnapshot RuntimeMemorySnapshot::Capture() {
        PROCESS_MEMORY_COUNTERS_EX counters {};
        counters.cb = sizeof(counters);
        if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) {
            throw std::runtime_error("GetProcessMemoryInfo failed while capturing runtime memory diagnostics.");
        }

        PERFORMANCE_INFORMATION performanceInformation {};
        performanceInformation.cb = sizeof(performanceInformation);
        if (!GetPerformanceInfo(&performanceInformation, sizeof(performanceInformation))) {
            throw std::runtime_error("GetPerformanceInfo failed while capturing runtime memory diagnostics.");
        }

        const std::uint64_t pageSizeBytes = static_cast<std::uint64_t>(performanceInformation.PageSize);

        RuntimeMemorySnapshot snapshot;
        snapshot.WorkingSetBytes = static_cast<std::uint64_t>(counters.WorkingSetSize);
        snapshot.PeakWorkingSetBytes = static_cast<std::uint64_t>(counters.PeakWorkingSetSize);
        snapshot.PrivateUsageBytes = static_cast<std::uint64_t>(counters.PrivateUsage);
        snapshot.PagefileUsageBytes = static_cast<std::uint64_t>(counters.PagefileUsage);
        snapshot.PeakPagefileUsageBytes = static_cast<std::uint64_t>(counters.PeakPagefileUsage);
        snapshot.QuotaPagedPoolBytes = static_cast<std::uint64_t>(counters.QuotaPagedPoolUsage);
        snapshot.QuotaNonPagedPoolBytes = static_cast<std::uint64_t>(counters.QuotaNonPagedPoolUsage);
        snapshot.PageFaultCount = static_cast<std::uint64_t>(counters.PageFaultCount);
        snapshot.SystemCommitTotalBytes = static_cast<std::uint64_t>(performanceInformation.CommitTotal) * pageSizeBytes;
        snapshot.SystemCommitLimitBytes = static_cast<std::uint64_t>(performanceInformation.CommitLimit) * pageSizeBytes;
        snapshot.AvailablePhysicalBytes = static_cast<std::uint64_t>(performanceInformation.PhysicalAvailable) * pageSizeBytes;
        return snapshot;
    }

    /// Formats the captured counters into one compact human-readable string.
    std::string RuntimeMemorySnapshot::ToSummaryString() const {
        std::ostringstream builder;
        builder << "working_set=" << FormatMiB(WorkingSetBytes)
            << " peak_working_set=" << FormatMiB(PeakWorkingSetBytes)
            << " private_usage=" << FormatMiB(PrivateUsageBytes)
            << " pagefile_usage=" << FormatMiB(PagefileUsageBytes)
            << " peak_pagefile_usage=" << FormatMiB(PeakPagefileUsageBytes)
            << " paged_pool=" << FormatMiB(QuotaPagedPoolBytes)
            << " nonpaged_pool=" << FormatMiB(QuotaNonPagedPoolBytes)
            << " page_faults=" << PageFaultCount
            << " system_commit_total=" << FormatMiB(SystemCommitTotalBytes)
            << " system_commit_limit=" << FormatMiB(SystemCommitLimitBytes)
            << " available_physical=" << FormatMiB(AvailablePhysicalBytes);
        return builder.str();
    }
}
