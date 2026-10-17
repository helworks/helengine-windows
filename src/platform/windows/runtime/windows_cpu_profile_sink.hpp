#pragma once

#if defined(TRACY_ENABLE) && __has_include("IRuntimeCpuProfileSink.hpp")
#include <array>
#include <cstdint>
#include <unordered_map>

#include "IRuntimeCpuProfileSink.hpp"

namespace helengine::windows {
    /// Bridges the existing Core CPU probes to duration plots without allocating in steady-state updates.
    class WindowsCpuProfileSink final : public ::IRuntimeCpuProfileSink {
    public:
        /// Reads the monotonic host clock in the microsecond units required by the shared contract.
        std::uint64_t ReadMicroseconds() override;
        /// Accumulates one Core or UI command-building phase for the current frame.
        void RecordStage(::RuntimeCpuProfileStage stage, std::uint64_t elapsedMicroseconds) override;
        /// Accumulates one actual update invocation with its component and owning entity identities.
        void RecordUpdateable(::IUpdateable* item, std::int32_t index, std::uint32_t typeHash,
            std::uint32_t ownerSceneEntityId, std::uint64_t elapsedMicroseconds) override;
        /// Registers the concrete native type outside the timed update loop.
        void RegisterType(::IUpdateable* item, std::uint32_t typeHash, HeCppString typeName) override;
        /// Counts rejected non-monotonic clock samples rather than recording invalid durations.
        void ClockFault() override;
        /// Publishes measured durations and invocation counts after update/draw, then clears frame totals.
        void FlushFrame();

    private:
        /// Bounds profiler-only metadata; exceeding it is reported instead of silently dropping identities.
        static constexpr std::size_t Capacity = 512;
        /// Stores stable character buffers that remain valid for Tracy's pointer-based plot-name protocol.
        struct ComponentRow {
            /// Borrows the registered updateable identity without extending its lifetime.
            ::IUpdateable* Item = nullptr;
            /// Stores the native type name resolved during registration.
            char TypeName[128]{};
            /// Stores the immutable name of this instance's duration plot.
            char PlotName[320]{};
            /// Identifies the owning entity row, or Capacity for non-component updateables.
            std::size_t EntityIndex = Capacity;
            /// Accumulates this frame's measured execution time in shared-contract units.
            std::uint64_t Microseconds = 0;
            /// Counts actual invocations so skipped components are distinguishable from fast components.
            std::uint32_t Calls = 0;
        };
        /// Aggregates update time for one actual entity pointer, distinguishing identical ids in different scenes.
        struct EntityRow {
            /// Borrows the owning entity identity.
            const void* Identity = nullptr;
            /// Stores the immutable entity duration plot name.
            char PlotName[192]{};
            /// Accumulates all measured component updates owned by this entity during the frame.
            std::uint64_t Microseconds = 0;
            /// Counts this entity's executed component updates during the frame.
            std::uint32_t Calls = 0;
        };
        /// Resolves or creates the bounded metadata row of one actual owning entity.
        std::size_t ResolveEntity(const void* identity, std::uint32_t ownerSceneEntityId);
        /// Keeps plot-name storage at a stable address for the complete process lifetime.
        std::array<ComponentRow, Capacity> Components{};
        /// Keeps entity plot-name storage stable across registration-map rehashes.
        std::array<EntityRow, Capacity> Entities{};
        /// Maps updateable identities to their preallocated component rows.
        std::unordered_map<::IUpdateable*, std::size_t> ComponentIndices;
        /// Maps actual entity identities to preallocated aggregate rows.
        std::unordered_map<const void*, std::size_t> EntityIndices;
        /// Counts initialized component metadata rows.
        std::size_t ComponentCount = 0;
        /// Counts initialized entity metadata rows.
        std::size_t EntityCount = 0;
        /// Accumulates the shared Core stage durations for the current frame.
        std::array<std::uint64_t, 18> StageMicroseconds{};
        /// Counts clock faults across the profiling session.
        std::uint64_t ClockFaultCount = 0;
    };
}
#endif
