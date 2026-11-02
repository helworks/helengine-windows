#pragma once
#include <windows.h>
#include <chrono>
#include <map>
#include <string>
#include <sstream>
#include <algorithm>
#include "platform/windows/runtime/runtime_render_diagnostics.hpp"

namespace helengine::windows {
    /// <summary>Records optional CPU wall time between existing host boundaries without introducing GPU waits.</summary>
    class Win32FrameStageTimings {
        /// <summary>Retains one aggregated boundary interval during a five-second sampling window.</summary>
        struct Sample {
            unsigned Count = 0;
            double Total = 0, Maximum = 0;
        };
    public:
        /// <summary>Collects stage durations only when explicitly enabled through HELENGINE_FRAME_STAGE_TIMINGS.</summary>
        static void Observe(const char* stage, const char* tag) {
            static const bool enabled = GetEnvironmentVariableW(L"HELENGINE_FRAME_STAGE_TIMINGS", nullptr, 0) > 0;
            if (!enabled) return;
            using Clock = std::chrono::steady_clock;
            static auto previous = Clock::now(), reported = previous;
            static std::string previousStage;
            static std::map<std::string, Sample> samples;
            const auto now = Clock::now();
            if (!previousStage.empty()) {
                const double elapsed = std::chrono::duration<double, std::milli>(now - previous).count();
                auto& sample = samples[previousStage];
                ++sample.Count; sample.Total += elapsed; sample.Maximum = std::max(sample.Maximum, elapsed);
            }
            previous = now; previousStage = std::string(tag) + "/" + stage;
            if (std::string(stage) != "completed" || std::chrono::duration<double>(now - reported).count() < 5) return;
            for (const auto& entry : samples) {
                std::ostringstream message;
                message << "boundary=" << entry.first << " samples=" << entry.second.Count
                    << " avg_ms=" << entry.second.Total / entry.second.Count << " max_ms=" << entry.second.Maximum
                    << " total_ms=" << entry.second.Total << " measurement=cpu_wall_until_next_boundary";
                RuntimeRenderDiagnostics::WriteHostEvent("frame.stage-timing", message.str());
            }
            samples.clear(); reported = now;
            // Do not charge diagnostic file writes to the idle/message-loop interval.
            previous = Clock::now();
        }
    };
}
