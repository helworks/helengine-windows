#include "platform/windows/runtime/runtime_render_diagnostics.hpp"

#include <fstream>
#include <sstream>
#include <unordered_map>

namespace helengine::windows {
    namespace {
        /// Stores the resolved per-run diagnostics log path.
        std::filesystem::path DiagnosticsLogPath;

        /// Tracks whether the diagnostics log has already been initialized for the current process.
        bool DiagnosticsInitialized = false;

        /// Tracks how many times each asset id has been built during the current process lifetime.
        std::unordered_map<std::string, std::uint64_t> AssetBuildCounts;

        /// Builds the diagnostics map key used to pair asset build and release records.
        std::string BuildAssetCountKey(const std::string& assetClass, const std::string& assetId) {
            return assetClass + "|" + assetId;
        }

        /// Replaces multi-line values with one log-safe quoted representation.
        std::string Quote(const std::string& value) {
            std::string sanitized = value;
            for (char& character : sanitized) {
                if (character == '\r' || character == '\n') {
                    character = ' ';
                } else if (character == '"') {
                    character = '\'';
                }
            }

            return "\"" + sanitized + "\"";
        }

        /// Appends one structured line to the per-run diagnostics log when initialization succeeded.
        void AppendLine(const std::string& line) {
            if (!DiagnosticsInitialized) {
                return;
            }

            std::ofstream stream(DiagnosticsLogPath, std::ios::app);
            if (!stream.is_open()) {
                return;
            }

            stream << line << '\n';
        }
    }

    /// Initializes one per-run diagnostics log beside the packaged executable.
    void RuntimeRenderDiagnostics::Initialize(const std::filesystem::path& applicationDirectoryPath) {
        DiagnosticsInitialized = false;
        AssetBuildCounts.clear();

        DiagnosticsLogPath = applicationDirectoryPath / "helengine_windows.diagnostics.log";
        std::filesystem::create_directories(DiagnosticsLogPath.parent_path());

        std::ofstream stream(DiagnosticsLogPath, std::ios::out | std::ios::trunc);
        if (!stream.is_open()) {
            DiagnosticsInitialized = false;
            return;
        }

        DiagnosticsInitialized = true;
        stream << "diagnostics.begin path=" << Quote(DiagnosticsLogPath.string()) << '\n';
    }

    /// Resets process-local counters for one fresh runtime session.
    void RuntimeRenderDiagnostics::Reset() {
        AssetBuildCounts.clear();
    }

    /// Writes one host-side lifecycle event into the diagnostics log.
    void RuntimeRenderDiagnostics::WriteHostEvent(const std::string& category, const std::string& message) {
        if (!DiagnosticsInitialized) {
            return;
        }

        std::ostringstream builder;
        builder << "host.event"
            << " category=" << Quote(category)
            << " message=" << Quote(message);
        AppendLine(builder.str());
    }

    /// Writes one structured packaged-asset load entry into the diagnostics log.
    void RuntimeRenderDiagnostics::RecordPackagedAssetLoad(const std::string& relativePath, const std::string& fullPath) {
        if (!DiagnosticsInitialized) {
            return;
        }

        std::ostringstream builder;
        builder << "content.load"
            << " kind=" << Quote("packaged-asset")
            << " relative_path=" << Quote(relativePath)
            << " full_path=" << Quote(fullPath);
        AppendLine(builder.str());
    }

    /// Writes one structured asset-build entry and tracks whether the same asset id was built again.
    void RuntimeRenderDiagnostics::RecordAssetBuild(
        const std::string& assetClass,
        const std::string& assetId,
        const std::string& detail,
        std::size_t liveResourceCount) {
        if (!DiagnosticsInitialized) {
            return;
        }

        const std::string key = BuildAssetCountKey(assetClass, assetId);
        std::uint64_t buildCount = ++AssetBuildCounts[key];

        std::ostringstream builder;
        builder << "asset.build"
            << " class=" << Quote(assetClass)
            << " asset_id=" << Quote(assetId)
            << " build_count=" << buildCount
            << " duplicate=" << (buildCount > 1 ? "true" : "false")
            << " live_resource_count=" << liveResourceCount
            << " detail=" << Quote(detail);
        AppendLine(builder.str());
    }

    /// Writes one structured asset-release request entry.
    void RuntimeRenderDiagnostics::RecordAssetReleaseRequested(
        const std::string& assetClass,
        const std::string& assetId,
        const std::string& detail) {
        if (!DiagnosticsInitialized) {
            return;
        }

        std::ostringstream builder;
        builder << "asset.release"
            << " phase=" << Quote("requested")
            << " class=" << Quote(assetClass)
            << " asset_id=" << Quote(assetId)
            << " detail=" << Quote(detail);
        AppendLine(builder.str());
    }

    /// Writes one structured asset-release completion entry.
    void RuntimeRenderDiagnostics::RecordAssetReleaseCompleted(
        const std::string& assetClass,
        const std::string& assetId,
        const std::string& detail) {
        if (!DiagnosticsInitialized) {
            return;
        }

        AssetBuildCounts.erase(BuildAssetCountKey(assetClass, assetId));

        std::ostringstream builder;
        builder << "asset.release"
            << " phase=" << Quote("completed")
            << " class=" << Quote(assetClass)
            << " asset_id=" << Quote(assetId)
            << " detail=" << Quote(detail);
        AppendLine(builder.str());
    }

    /// Writes one structured scene-transition checkpoint including RAM and renderer-cache counters.
    void RuntimeRenderDiagnostics::WriteSceneCheckpoint(
        const std::string& label,
        const RuntimeMemorySnapshot& memorySnapshot,
        const RuntimeRenderCounters& renderCounters,
        const std::string& detailMetrics,
        const std::string& coreSceneTransitionStage,
        const std::string& trackedSceneIds,
        const std::string& sceneManagerStage,
        const std::string& sceneManagerSceneId,
        int loadedSceneCount,
        int pendingOperationCount,
        const std::string& sceneLoadStage,
        int rootEntityIndex,
        int entityDepth,
        const std::string& componentTypeId,
        const std::string& textFontRelativePath,
        const std::string& textFontLoadStage,
        const std::string& fontDeserializeStage) {
        if (!DiagnosticsInitialized) {
            return;
        }

        std::ostringstream builder;
        builder << "scene.checkpoint"
            << " label=" << Quote(label)
            << " working_set_bytes=" << memorySnapshot.WorkingSetBytes
            << " peak_working_set_bytes=" << memorySnapshot.PeakWorkingSetBytes
            << " private_usage_bytes=" << memorySnapshot.PrivateUsageBytes
            << " pagefile_usage_bytes=" << memorySnapshot.PagefileUsageBytes
            << " peak_pagefile_usage_bytes=" << memorySnapshot.PeakPagefileUsageBytes
            << " quota_paged_pool_bytes=" << memorySnapshot.QuotaPagedPoolBytes
            << " quota_nonpaged_pool_bytes=" << memorySnapshot.QuotaNonPagedPoolBytes
            << " page_fault_count=" << memorySnapshot.PageFaultCount
            << " system_commit_total_bytes=" << memorySnapshot.SystemCommitTotalBytes
            << " system_commit_limit_bytes=" << memorySnapshot.SystemCommitLimitBytes
            << " available_physical_bytes=" << memorySnapshot.AvailablePhysicalBytes
            << " texture_resources=" << renderCounters.TextureResourceCount
            << " scene_owned_texture_resources=" << renderCounters.SceneOwnedTextureResourceCount
            << " engine_owned_texture_resources=" << renderCounters.EngineOwnedTextureResourceCount
            << " material_shader_resources=" << renderCounters.MaterialShaderResourceCount
            << " material_constant_buffers=" << renderCounters.MaterialConstantBufferCount
            << " model_buffers=" << renderCounters.ModelBufferCount
            << " model_vertex_buffer_bytes=" << renderCounters.ModelVertexBufferBytes
            << " model_index_buffer_bytes=" << renderCounters.ModelIndexBufferBytes
            << " material_constant_buffer_bytes=" << renderCounters.MaterialConstantBufferBytes
            << " core_stage=" << Quote(coreSceneTransitionStage)
            << " tracked_scene_ids=" << Quote(trackedSceneIds)
            << " scene_manager_stage=" << Quote(sceneManagerStage)
            << " scene_manager_scene_id=" << Quote(sceneManagerSceneId)
            << " loaded_scene_count=" << loadedSceneCount
            << " pending_operation_count=" << pendingOperationCount
            << " scene_load_stage=" << Quote(sceneLoadStage)
            << " root_entity_index=" << rootEntityIndex
            << " entity_depth=" << entityDepth
            << " component_type=" << Quote(componentTypeId)
            << " text_font_relative_path=" << Quote(textFontRelativePath)
            << " text_font_load_stage=" << Quote(textFontLoadStage)
            << " font_deserialize_stage=" << Quote(fontDeserializeStage);
        if (!detailMetrics.empty()) {
            builder << detailMetrics;
        }
        AppendLine(builder.str());
    }
}
