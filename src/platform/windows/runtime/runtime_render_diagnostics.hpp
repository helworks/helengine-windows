#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

#include "platform/windows/runtime/runtime_memory_snapshot.hpp"

namespace helengine::windows {
    /// Stores the current Windows renderer cache counts sampled at one checkpoint.
    struct RuntimeRenderCounters {
        /// Stores the number of uploaded runtime textures currently cached by the Windows bridge.
        std::size_t TextureResourceCount = 0;

        /// Stores the number of scene-owned texture resources currently cached by the Windows bridge.
        std::size_t SceneOwnedTextureResourceCount = 0;

        /// Stores the number of engine-owned texture resources currently cached by the Windows bridge.
        std::size_t EngineOwnedTextureResourceCount = 0;

        /// Stores the number of compiled material shader-resource bundles currently cached by the Windows bridge.
        std::size_t MaterialShaderResourceCount = 0;

        /// Stores the number of authored material constant buffers currently cached by the Windows bridge.
        std::size_t MaterialConstantBufferCount = 0;

        /// Stores the number of uploaded runtime models currently retaining native vertex or index buffers.
        std::size_t ModelBufferCount = 0;

        /// Stores the total native bytes currently retained by uploaded model vertex buffers.
        std::size_t ModelVertexBufferBytes = 0;

        /// Stores the total native bytes currently retained by uploaded model index buffers.
        std::size_t ModelIndexBufferBytes = 0;

        /// Stores the total native bytes currently retained by authored material constant buffers.
        std::size_t MaterialConstantBufferBytes = 0;
    };

    /// Writes structured Windows runtime diagnostics for scene transitions and asset builds.
    class RuntimeRenderDiagnostics {
    public:
        /// Initializes one per-run diagnostics log beside the packaged executable.
        static void Initialize(const std::filesystem::path& applicationDirectoryPath);

        /// Resets process-local counters for one fresh runtime session.
        static void Reset();

        /// Writes one host-side lifecycle event into the diagnostics log.
        static void WriteHostEvent(const std::string& category, const std::string& message);

        /// Writes one structured packaged-asset load entry into the diagnostics log.
        static void RecordPackagedAssetLoad(const std::string& relativePath, const std::string& fullPath);

        /// Writes one structured asset-build entry and tracks whether the same asset id was built again.
        static void RecordAssetBuild(
            const std::string& assetClass,
            const std::string& assetId,
            const std::string& detail,
            std::size_t liveResourceCount);

        /// Writes one structured asset-release request entry.
        static void RecordAssetReleaseRequested(
            const std::string& assetClass,
            const std::string& assetId,
            const std::string& detail);

        /// Writes one structured asset-release completion entry.
        static void RecordAssetReleaseCompleted(
            const std::string& assetClass,
            const std::string& assetId,
            const std::string& detail);

        /// Writes one structured scene-transition checkpoint including RAM and renderer-cache counters.
        static void WriteSceneCheckpoint(
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
            const std::string& textureRelativePath,
            const std::string& textureLoadStage,
            const std::string& textFontRelativePath,
            const std::string& textFontLoadStage,
            const std::string& fontDeserializeStage);
    };
}
