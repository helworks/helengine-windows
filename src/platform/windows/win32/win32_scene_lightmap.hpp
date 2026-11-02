#pragma once
#include "hellm/Lightmap.h"
#include "platform/windows/win32/win32_scene_reflection_probe.hpp"
#include "platform/windows/win32/win32_scene_lightmap_draw.hpp"
#include <memory>
#include <unordered_map>
class Core;
class DirectionalLightComponent;
class ICamera;
class IDrawable3D;
class ModelAsset;
namespace helengine::windows {
    class Win32RenderManager2D;
    class Win32RenderManager3D;
    /// <summary>Exports an initialized immutable snapshot or loads its matching irradiance; never invokes a baker.</summary>
    class Win32SceneLightmap {
        /// <summary>Borrows the initialized scene until explicit teardown.</summary>
        Core* Owner;
        /// <summary>Borrows its frozen directional transport source.</summary>
        DirectionalLightComponent* Sun;
        /// <summary>Limits irradiance consumption to the two authored scene outputs.</summary>
        ICamera* MainCamera;
        /// <summary>Shares exactly the same atlas with the second scene output.</summary>
        ICamera* GlassCamera;
        /// <summary>Resolves retained uploaded diffuse pixels independently from GPU readback.</summary>
        Win32RenderManager2D& Textures;
        /// <summary>Stores the explicitly configured snapshot or lightmap asset path.</summary>
        std::string Path;
        /// <summary>Resolves immutable registered metallic values consistently across both scene cameras.</summary>
        Win32RenderManager3D& Materials;
        /// <summary>Captures the explicitly authored room volume without deriving bounds from decorative geometry.</summary>
        hellm::ProbeSettings ProbeSettings;
        /// <summary>Owns one shared camera-independent HDR reflection cube and integrated BRDF lookup.</summary>
        std::unique_ptr<Win32SceneReflectionProbe> Probe;
        /// <summary>Selects snapshot export, with no bake or implicit load fallback.</summary>
        bool ExportMode;
        /// <summary>Arms capture after the first draw and waits for one complete scene update.</summary>
        double FirstDrawSeconds=-1;
        /// <summary>Prevents repeated scans, loads or exports after successful initialization.</summary>
        bool Initialized=false;
        /// <summary>Owns one immutable camera-independent RGBA32F irradiance texture.</summary>
        Microsoft::WRL::ComPtr<ID3D11Texture2D> Atlas;
        /// <summary>Exposes irradiance only in dedicated pixel texture slot twenty-three.</summary>
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> View;
        /// <summary>Enables baked diffuse replacement only during a bound baked draw.</summary>
        Microsoft::WRL::ComPtr<ID3D11Buffer> Parameters;
        /// <summary>Filters within padded charts without wrapping around the atlas.</summary>
        Microsoft::WRL::ComPtr<ID3D11SamplerState> Sampler;
        /// <summary>Indexes immutable GPU sidecars by drawable identity and original submesh ordinal.</summary>
        std::unordered_map<IDrawable3D*,std::unordered_map<int,Win32SceneLightmapDraw>> Draws;
    public:
        /// <summary>Validates borrowed dependencies without scanning an incompletely initialized scene.</summary>
        Win32SceneLightmap(Core* owner,DirectionalLightComponent* sun,ICamera* mainCamera,ICamera* glassCamera,Win32RenderManager2D& textures,Win32RenderManager3D& materials,const std::string& path,bool exportMode,const hellm::ProbeSettings& probe);
        /// <summary>Copies all geometry attributes independently from the optional voxel module.</summary>
        static std::shared_ptr<hellm::Mesh> CaptureMesh(ModelAsset* asset);
        /// <summary>Exports or loads once, after one complete update following the first draw.</summary>
        void Update(ID3D11Device* device);
        /// <summary>Binds a per-instance expanded triangle stream and its shared atlas, returning false for unbaked surfaces.</summary>
        const Win32SceneLightmapDraw* Bind(ID3D11DeviceContext* context,ICamera* camera,IDrawable3D* drawable,int submesh) const;
        /// <summary>Clears every dedicated binding and UV1 stream before ordinary geometry or UI resumes.</summary>
        static void Unbind(ID3D11DeviceContext* context) {
            if(!context) return;
            Win32SceneReflectionProbe::Unbind(context);
            ID3D11Buffer* buffer=nullptr;ID3D11ShaderResourceView* view=nullptr;ID3D11SamplerState* sampler=nullptr;UINT zero=0;
            context->PSSetConstantBuffers(11,1,&buffer);context->PSSetShaderResources(23,1,&view);context->PSSetSamplers(9,1,&sampler);context->IASetVertexBuffers(1,1,&buffer,&zero,&zero);
        }
    };
}
