#pragma once
#include "helgi/VoxelGi.h"
#include "platform/windows/win32/win32_scene_gi_mesh.hpp"
#include "platform/windows/win32/win32_scene_gi_contributor.hpp"
#include "platform/windows/win32/win32_scene_gi_spheres.hpp"
#include "platform/windows/win32/win32_scene_gi_usage.hpp"
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>
#include <unordered_map>

class Core;
class DirectionalLightComponent;
class IDrawable3D;
class ModelAsset;

namespace helengine::windows {
    /// <summary>Submits one authoritative scene to the portable solver and publishes its HDR field to all Windows toon cameras.</summary>
    class Win32SceneGi {
        /// <summary>Borrows the explicitly configured scene owner until ClearSceneGi.</summary>
        Core* Owner;
        /// <summary>Borrows the single authored transport source until ClearSceneGi.</summary>
        DirectionalLightComponent* Sun;
        /// <summary>Retains exact bounds and quality for idempotent configuration.</summary>
        helgi::Config Settings;
        /// <summary>Limits portable solver work once per scene update.</summary>
        std::uint32_t WorkItems;
        /// <summary>Owns camera-independent transport and staged publication.</summary>
        helgi::VoxelGi Solver;
        /// <summary>Retains source state for active contributors only.</summary>
        std::unordered_map<IDrawable3D*, Win32SceneGiContributor> Contributors;
        /// <summary>Allocates unique solver identities for newly encountered drawables.</summary>
        std::uint64_t NextIdentity = 1;
        /// <summary>Deduplicates update work between output windows without consuming camera state.</summary>
        double LastUpdateSeconds = -1;
        /// <summary>Stores the published RGBA32F atlas, never a partially integrated field.</summary>
        Microsoft::WRL::ComPtr<ID3D11Texture2D> Atlas;
        /// <summary>Exposes the atlas in pixel-shader slot seventeen.</summary>
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> AtlasView;
        /// <summary>Stores three IEEE32 float4 registers for origin, cell size and cell counts.</summary>
        Microsoft::WRL::ComPtr<ID3D11Buffer> Parameters;
        /// <summary>Owns the optional diagnostic pipeline only while explicitly enabled.</summary>
        std::unique_ptr<Win32SceneGiSpheres> DebugSpheres;
        /// <summary>Owns receiver visibility and accepted-tap usage only while the sphere diagnostic is enabled.</summary>
        std::unique_ptr<Win32SceneGiUsage> DebugUsage;
        /// <summary>Shows raw RGB irradiance on neutral receivers while preserving albedo in transport.</summary>
        bool IndirectOnly=false;

        /// <summary>Submits changed mesh geometry using current world-space transforms and inherited material colors.</summary>
        void SubmitDrawable(IDrawable3D* drawable);
    public:
        /// <summary>Validates and allocates a finite transport volume and its persistent GPU atlas.</summary>
        Win32SceneGi(ID3D11Device* device, Core* owner, DirectionalLightComponent* sun, const helgi::Config& config, std::uint32_t workItems);
        /// <summary>Compares immutable solver settings while permitting owner, sun and work budget rebinding.</summary>
        bool Matches(const helgi::Config& config) const;
        /// <summary>Updates borrowed sources and the work budget without discarding a published field.</summary>
        void Reconfigure(Core* owner, DirectionalLightComponent* sun, std::uint32_t workItems);
        /// <summary>Copies only geometry needed by optional transport while the raw upload asset remains valid.</summary>
        static std::shared_ptr<Win32SceneGiMesh> CaptureMesh(ModelAsset* asset);
        /// <summary>Scans all registered scene meshes once per update, then advances bounded work and uploads on publication.</summary>
        void Update(ID3D11DeviceContext* context);
        /// <summary>Binds the last complete shared field immediately before a toon geometry draw.</summary>
        void Bind(ID3D11DeviceContext* context) const;
        /// <summary>Enables or releases renderer-only sphere resources without changing solver state or the published atlas.</summary>
        void ConfigureDebugSpheres(bool enabled);
        /// <summary>Reports whether the explicit diagnostic pipeline is currently enabled.</summary>
        bool IsDebugSpheresEnabled() const;
        /// <summary>Returns the enabled diagnostic's mask pass without adding scene or core concepts.</summary>
        Win32SceneGiUsage* GetDebugUsage() const;
        /// <summary>Begins a freshly cleared usage capture against the exact current published field.</summary>
        void BeginDebugUsage(ID3D11DeviceContext* context);
        /// <summary>Identifies the complete field generation, including zero before first publication.</summary>
        std::uint64_t GetPublishedGeneration() const;
        /// <summary>Returns the authoritative scene update clock used to share receiver work between outputs.</summary>
        double GetSceneUpdateSeconds() const;
        /// <summary>Updates reserved field-buffer padding without rebuilding the solver or its published atlas.</summary>
        void ConfigureIndirectOnly(ID3D11DeviceContext* context,bool enabled);
        /// <summary>Reports whether scene surfaces display only the published diffuse transport.</summary>
        bool IsIndirectOnly() const;
        /// <summary>Draws the last completed generation through the original camera; the unpublished initial atlas draws nothing.</summary>
        void DrawDebugSpheres(ID3D11DeviceContext* context,const float* viewProjection) const;
    };
}
