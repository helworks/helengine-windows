#include "platform/windows/runtime/windows_cpu_profile_sink.hpp"

#if defined(TRACY_ENABLE) && __has_include("IRuntimeCpuProfileSink.hpp")
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <typeinfo>
#include <tracy/Tracy.hpp>

#include "Component.hpp"
#include "IUpdateable.hpp"

namespace helengine::windows {
    /// Converts the native monotonic clock to the units defined by IRuntimeCpuProfileSink.
    std::uint64_t WindowsCpuProfileSink::ReadMicroseconds() {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    /// Rejects unknown phase ids rather than attributing their samples to another phase.
    void WindowsCpuProfileSink::RecordStage(::RuntimeCpuProfileStage stage, std::uint64_t elapsedMicroseconds) {
        const auto index = static_cast<std::size_t>(stage);
        if (index == 0 || index >= StageMicroseconds.size()) {
            throw std::out_of_range("Unknown Core CPU profiling stage.");
        }
        StageMicroseconds[index] += elapsedMicroseconds;
    }

    /// Records durations after the measured callback; plot emission is deferred outside Engine.Update/Draw.
    void WindowsCpuProfileSink::RecordUpdateable(::IUpdateable* item, std::int32_t index,
        std::uint32_t typeHash, std::uint32_t ownerSceneEntityId, std::uint64_t elapsedMicroseconds) {
        (void)index;
        (void)typeHash;
        const auto found = ComponentIndices.find(item);
        if (found == ComponentIndices.end()) {
            throw std::logic_error("Core CPU profiler received an unregistered updateable.");
        }
        auto& row = Components[found->second];
        if (row.PlotName[0] == '\0') {
            const int length = std::snprintf(row.PlotName, sizeof(row.PlotName),
                "Component.Update/%s/entity=%u/instance=%p", row.TypeName, ownerSceneEntityId,
                static_cast<void*>(item));
            if (length < 0 || static_cast<std::size_t>(length) >= sizeof(row.PlotName)) {
                throw std::length_error("Component CPU profiling identity is too long.");
            }
            auto* component = dynamic_cast<::Component*>(item);
            if (component != nullptr && component->Parent != nullptr) {
                row.EntityIndex = ResolveEntity(component->Parent, ownerSceneEntityId);
            }
        }
        row.Microseconds += elapsedMicroseconds;
        ++row.Calls;
        if (row.EntityIndex != Capacity) {
            auto& entity = Entities[row.EntityIndex];
            entity.Microseconds += elapsedMicroseconds;
            ++entity.Calls;
        }
    }

    /// Uses RTTI for the actual derived type because the generated managed type name can be interface-erased.
    void WindowsCpuProfileSink::RegisterType(::IUpdateable* item, std::uint32_t typeHash, std::string typeName) {
        (void)typeHash;
        if (item == nullptr) {
            throw std::invalid_argument("CPU profiling registration requires an updateable.");
        }
        if (ComponentIndices.contains(item)) {
            return;
        }
        if (ComponentCount == Capacity) {
            throw std::length_error("Windows CPU profiler component metadata capacity exceeded.");
        }
        const char* nativeName = typeid(*item).name();
        if (nativeName != nullptr && nativeName[0] != '\0') {
            typeName = nativeName;
            if (typeName.starts_with("class ")) typeName.erase(0, 6);
            else if (typeName.starts_with("struct ")) typeName.erase(0, 7);
        }
        auto& row = Components[ComponentCount];
        if (typeName.size() >= sizeof(row.TypeName)) {
            throw std::length_error("Windows CPU profiler type name is too long.");
        }
        std::snprintf(row.TypeName, sizeof(row.TypeName), "%s", typeName.c_str());
        row.Item = item;
        ComponentIndices.emplace(item, ComponentCount++);
    }

    /// Tracks invalid samples separately from valid zero-microsecond measurements.
    void WindowsCpuProfileSink::ClockFault() {
        ++ClockFaultCount;
    }

    /// Separates authored scene ids from actual pointers so additive scenes cannot merge unrelated entities.
    std::size_t WindowsCpuProfileSink::ResolveEntity(const void* identity, std::uint32_t ownerSceneEntityId) {
        const auto found = EntityIndices.find(identity);
        if (found != EntityIndices.end()) return found->second;
        if (EntityCount == Capacity) {
            throw std::length_error("Windows CPU profiler entity metadata capacity exceeded.");
        }
        auto& row = Entities[EntityCount];
        row.Identity = identity;
        std::snprintf(row.PlotName, sizeof(row.PlotName), "Entity.Update/id=%u/instance=%p",
            ownerSceneEntityId, identity);
        EntityIndices.emplace(identity, EntityCount);
        return EntityCount++;
    }

    /// Emits frame totals in milliseconds, including zeros when previously active identities stop executing.
    void WindowsCpuProfileSink::FlushFrame() {
        static constexpr const char* StageNames[] = { "unused", "Core.Stage/EarlyInput.ms",
            "Core.Stage/FrameCounters.ms", "Core.Stage/ObjectManagerUpdate.ms", "Core.Stage/Audio.ms",
            "Core.Stage/Physics.ms", "Core.Stage/LateInput.ms", "Core.Stage/PointerInteraction.ms",
            "Core.Stage/FrameBoundary.ms", "Core.Stage/RenderManager3D.ms", "UI.Stage/CommandListSetup.ms",
            "UI.Stage/Clipping.ms", "UI.Stage/Sprites.ms", "UI.Stage/TextLayout.ms",
            "UI.Stage/TextEffectsSetup.ms", "UI.Stage/GlyphEmission.ms", "UI.Stage/RoundedRects.ms",
            "UI.Stage/TrailingClips.ms" };
        std::uint32_t executedComponents = 0;
        std::uint32_t executedEntities = 0;
        std::uint32_t calls = 0;
        for (std::size_t index = 0; index < ComponentCount; ++index) {
            auto& row = Components[index];
            if (row.PlotName[0] != '\0') {
                TracyPlot(row.PlotName, static_cast<double>(row.Microseconds) / 1000.0);
            }
            if (row.Calls != 0) ++executedComponents;
            calls += row.Calls;
            row.Microseconds = 0;
            row.Calls = 0;
        }
        for (std::size_t index = 0; index < EntityCount; ++index) {
            auto& row = Entities[index];
            TracyPlot(row.PlotName, static_cast<double>(row.Microseconds) / 1000.0);
            if (row.Calls != 0) ++executedEntities;
            row.Microseconds = 0;
            row.Calls = 0;
        }
        for (std::size_t index = 1; index < StageMicroseconds.size(); ++index) {
            TracyPlot(StageNames[index], static_cast<double>(StageMicroseconds[index]) / 1000.0);
            StageMicroseconds[index] = 0;
        }
        TracyPlot("Core.UpdateableInstances.Executed", static_cast<std::int64_t>(executedComponents));
        TracyPlot("Core.Entities.Updated", static_cast<std::int64_t>(executedEntities));
        TracyPlot("Core.UpdateableCalls", static_cast<std::int64_t>(calls));
        TracyPlot("Core.Profile.ClockFaults", static_cast<std::int64_t>(ClockFaultCount));
        TracyPlot("Core.Profile.ClockResolutionMicroseconds", 1.0);
    }
}
#endif
