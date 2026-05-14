#pragma once

#include <cstdint>
#include <string>

namespace helengine::windows {
    /// Captures one point-in-time view of the current process memory usage on Windows.
    class RuntimeMemorySnapshot {
    public:
        /// Samples the current process memory counters from the Windows process API.
        static RuntimeMemorySnapshot Capture();

        /// Formats the captured counters into one compact human-readable string.
        std::string ToSummaryString() const;

        /// Stores the current working-set size in bytes.
        std::uint64_t WorkingSetBytes = 0;

        /// Stores the peak working-set size in bytes.
        std::uint64_t PeakWorkingSetBytes = 0;

        /// Stores the current pagefile-backed private usage in bytes.
        std::uint64_t PrivateUsageBytes = 0;

        /// Stores the current committed pagefile usage in bytes.
        std::uint64_t PagefileUsageBytes = 0;

        /// Stores the peak committed pagefile usage in bytes.
        std::uint64_t PeakPagefileUsageBytes = 0;

        /// Stores the current paged-pool quota usage in bytes.
        std::uint64_t QuotaPagedPoolBytes = 0;

        /// Stores the current non-paged-pool quota usage in bytes.
        std::uint64_t QuotaNonPagedPoolBytes = 0;

        /// Stores the current process page-fault count.
        std::uint64_t PageFaultCount = 0;

        /// Stores the system commit total in bytes at capture time.
        std::uint64_t SystemCommitTotalBytes = 0;

        /// Stores the system commit limit in bytes at capture time.
        std::uint64_t SystemCommitLimitBytes = 0;

        /// Stores the currently available physical memory in bytes at capture time.
        std::uint64_t AvailablePhysicalBytes = 0;
    };
}
