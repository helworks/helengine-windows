#include "platform/windows/win32/win32_scene_lightmap.hpp"
#include "platform/windows/win32/win32_render_bridge.hpp"
#include "platform/windows/runtime/runtime_render_diagnostics.hpp"
#include "DirectionalLightComponent.hpp"
#include "Entity.hpp"
#include "Component.hpp"
#include "LightDirectionUtility.hpp"
#include "ObjectManager.hpp"
#include "SceneManager.hpp"
#include "RuntimeSubmesh.hpp"
#include "ModelPrimitiveTopology.hpp"
#include "MaterialPropertyBlock.hpp"
#include "MaterialLayout.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace helengine::windows {
    namespace {
        /// <summary>Propagates GPU allocation errors before partially initialized resources can be published.</summary>
        void CheckLightmap(HRESULT value,const char* operation) {
            if(FAILED(value)) throw std::runtime_error(std::string(operation)+" failed: "+std::to_string(value));
        }
        /// <summary>Transforms retained IEEE32 source geometry through the engine's numeric policy at the world boundary.</summary>
        hellm::Vec3 TransformLightmap(hellm::Vec3 value,const float4x4& matrix,bool position) {
            const Real x(value.X),y(value.Y),z(value.Z);
            const Real tx=x*matrix.M11+y*matrix.M21+z*matrix.M31+(position?matrix.M41:Real(0));
            const Real ty=x*matrix.M12+y*matrix.M22+z*matrix.M32+(position?matrix.M42:Real(0));
            const Real tz=x*matrix.M13+y*matrix.M23+z*matrix.M33+(position?matrix.M43:Real(0));
            return {tx.ToSingle(),ty.ToSingle(),tz.ToSingle()};
        }
        /// <summary>Matches the renderer's exact inherited diffuse binding rather than mistaking roughness or emission for albedo.</summary>
        RuntimeTexture* LightmapDiffuseTexture(ShaderRuntimeMaterial* material) {
            auto* layout=material->get_Layout();
            auto* properties=material->get_Properties();
            if(layout && properties) {
                const int index=layout->FindTextureBindingIndex("DiffuseTexture");
                if(index>=0) {
                    auto* texture=properties->GetTexture(index);
                    if(texture) return texture;
                }
            }
            auto* parent=dynamic_cast<ShaderRuntimeMaterial*>(material->get_ParentMaterial());
            return parent?LightmapDiffuseTexture(parent):nullptr;
        }
        /// <summary>Resolves component order rather than pointer addresses or shared model identity for persistent bindings.</summary>
        int LightmapDrawableOrdinal(IDrawable3D* drawable) {
            auto* components=drawable->get_Parent()->get_Components();int ordinal=0;
            for(int index=0;index<components->Count();++index) {
                auto* candidate=dynamic_cast<IDrawable3D*>((*components)[index]);
                if(candidate==drawable) return ordinal;
                if(candidate) ++ordinal;
            }
            throw std::logic_error("Lightmap drawable is not owned by its declared entity component collection.");
        }

    }
    Win32SceneLightmap::Win32SceneLightmap(Core* owner,DirectionalLightComponent* sun,ICamera* mainCamera,ICamera* glassCamera,Win32RenderManager2D& textures,Win32RenderManager3D& materials,const std::string& path,bool exportMode,const hellm::ProbeSettings& probe)
        :Owner(owner),Sun(sun),MainCamera(mainCamera),GlassCamera(glassCamera),Textures(textures),Path(path),Materials(materials),ProbeSettings(probe),ExportMode(exportMode) {
        if(!owner || !owner->get_ObjectManager() || !sun || !sun->get_Parent() || !mainCamera || !glassCamera || mainCamera==glassCamera || path.empty()) throw std::invalid_argument("Static lightmaps require initialized scene sources, two distinct cameras and an explicit asset path.");
    }
    std::shared_ptr<hellm::Mesh> Win32SceneLightmap::CaptureMesh(ModelAsset* asset) {
        if(!asset || !asset->Positions || !asset->Normals || !asset->TexCoords || asset->Positions->Length!=asset->Normals->Length || asset->Positions->Length!=asset->TexCoords->Length) throw std::invalid_argument("Static lightmap geometry requires matching position, normal and UV0 arrays.");
        auto mesh=std::make_shared<hellm::Mesh>();
        for(int index=0;index<asset->Positions->Length;++index) {
            const auto& p=(*asset->Positions)[index];const auto& n=(*asset->Normals)[index];const auto& uv=(*asset->TexCoords)[index];
            mesh->Positions.push_back({p.X.ToSingle(),p.Y.ToSingle(),p.Z.ToSingle()});mesh->Normals.push_back({n.X.ToSingle(),n.Y.ToSingle(),n.Z.ToSingle()});mesh->TexCoords.push_back({uv.X.ToSingle(),uv.Y.ToSingle()});
        }
        if(asset->Indices32 && asset->Indices32->Length>0) mesh->Indices.assign(asset->Indices32->Data,asset->Indices32->Data+asset->Indices32->Length);
        else if(asset->Indices16 && asset->Indices16->Length>0) mesh->Indices.assign(asset->Indices16->Data,asset->Indices16->Data+asset->Indices16->Length);
        else for(std::size_t index=0;index<mesh->Positions.size();++index) mesh->Indices.push_back(static_cast<uint32_t>(index));
        return mesh;
    }
    void Win32SceneLightmap::Update(ID3D11Device* device) {
        if(Initialized) return;
        if(!device || !Sun->get_Parent()) throw std::logic_error("Static lightmap sources must remain alive until explicit teardown.");
        // Budgeted scene initialization can draw many frames while its roots remain suppressed.
        // Arm only after scene commit, then allow one full initialized Update before capturing.
        auto* scenes=Owner->get_SceneManager();
        if(!scenes || scenes->get_IsSceneTransitionActive() || scenes->get_LoadedScenes()->get_Count()==0) {
            FirstDrawSeconds=-1;
            return;
        }
        const double seconds=Owner->get_TotalElapsedSeconds();
        if(FirstDrawSeconds<0) {FirstDrawSeconds=seconds;return;}
        if(seconds==FirstDrawSeconds) return;
        hellm::Scene scene;scene.Probe=ProbeSettings;const auto direction=LightDirectionUtility::GetEntityForwardDirection(Sun->get_Parent());const auto color=Sun->get_Color();
        const auto intensity=Sun->get_Parent()->get_IsHierarchyEnabled()?Sun->get_Intensity():0;
        scene.Sun={{-direction.X.ToSingle(),-direction.Y.ToSingle(),-direction.Z.ToSingle()},{color.X.ToSingle()*intensity,color.Y.ToSingle()*intensity,color.Z.ToSingle()*intensity}};
        std::unordered_map<uint32_t,Entity*> entities;
        std::unordered_set<std::string> keys;
        std::unordered_map<std::string,IDrawable3D*> drawableByKey;
        std::unordered_map<std::string,int> slotByKey;
        std::unordered_map<std::string,std::vector<float>> verticesByKey;
        auto* drawables=Owner->get_ObjectManager()->get_Drawables3D();
        const auto receiverLayers=MainCamera->get_LayerMask()|GlassCamera->get_LayerMask();
        for(int drawableIndex=0;drawableIndex<drawables->Count();++drawableIndex) {
            auto* drawable=(*drawables)[drawableIndex];
            if(!drawable || !drawable->get_Parent() || !drawable->get_Parent()->get_IsHierarchyEnabled() || (drawable->get_Parent()->get_LayerMask()&receiverLayers)==0 || !drawable->get_Model()) continue;
            auto* entity=drawable->get_Parent();const uint32_t entityId=entity->get_SceneEntityRuntimeId();
            if(entityId==0) throw std::logic_error("Static lightmap geometry requires a nonzero serialized SceneEntityRuntimeId.");
            const auto existing=entities.emplace(entityId,entity);
            if(!existing.second && existing.first->second!=entity) throw std::logic_error("Static lightmap scene entity IDs are ambiguous.");
            auto* model=dynamic_cast<Win32RuntimeModel*>(drawable->get_Model());
            if(!model || !model->LightmapMesh) throw std::logic_error("Static lightmap geometry was not retained at model upload.");
            const auto& original=*model->LightmapMesh;auto* materials=drawable->get_Materials();auto* submeshes=model->get_Submeshes();
            const int slots=submeshes && submeshes->Length>0?submeshes->Length:1;
            const int ordinal=LightmapDrawableOrdinal(drawable);auto world=entity->get_WorldTransformMatrix();float4x4 normalMatrix;float4x4::InverseTranspose__ref0_out1(world,normalMatrix);
            for(int slot=0;slot<slots;++slot) {
                const std::string key=std::to_string(entityId)+":"+std::to_string(ordinal)+":"+std::to_string(slot);
                auto* submesh=submeshes && submeshes->Length>0?(*submeshes)[slot]:nullptr;
                if(submesh && submesh->get_PrimitiveTopology()!=ModelPrimitiveTopology::TriangleList) throw std::invalid_argument("Static lightmap geometry requires triangle lists. Binding="+key);
                if(!materials || materials->Length==0) throw std::logic_error("Static lightmaps require authored standard diffuse materials. Binding="+key);
                auto* material=dynamic_cast<ShaderRuntimeMaterial*>((*materials)[slot<materials->Length?slot:0]);
                if(!material) throw std::logic_error("Static lightmap material must expose BaseColorBuffer. Binding="+key);
                Array<uint8_t>* bytes=nullptr;
                if(!material->TryResolveConstantBufferData__out1("BaseColorBuffer",bytes) || !bytes || bytes->Length!=16) {delete bytes;throw std::logic_error("Static lightmap BaseColorBuffer requires four IEEE32 values. Binding="+key);}
                float albedo[4];std::memcpy(albedo,bytes->Data,sizeof(albedo));delete bytes;
                for(float channel:albedo) if(!std::isfinite(channel) || channel<0 || channel>1) throw std::invalid_argument("Static diffuse reflectance and alpha must be finite in [0,1]. Binding="+key);
                // Glass remains in the ordinary pass and is neither an opaque occluder nor a diffuse reflector.
                if(albedo[3]<0.999f) continue;
                const int start=submesh?submesh->get_IndexStart():0;
                const int count=submesh && submesh->get_IndexCount()>0?submesh->get_IndexCount():static_cast<int>(original.Indices.size());
                if(start<0 || count<=0 || count%3 || static_cast<std::size_t>(start)>original.Indices.size() || static_cast<std::size_t>(count)>original.Indices.size()-start) throw std::invalid_argument("Static lightmap submesh range is invalid. Binding="+key);
                hellm::Mesh mesh;mesh.Key=key;mesh.Albedo={albedo[0],albedo[1],albedo[2]};
                mesh.Metallic=Materials.GetSceneLightmapMetallic(MainCamera,GlassCamera,material);
                bytes=nullptr;
                if(!material->TryResolveConstantBufferData__out1("RoughnessBuffer",bytes) || !bytes || bytes->Length!=16) {delete bytes;throw std::logic_error("Static material RoughnessBuffer requires four IEEE32 values. Binding="+key);}
                std::memcpy(&mesh.Roughness,bytes->Data,sizeof(float));delete bytes;
                if(!std::isfinite(mesh.Roughness) || mesh.Roughness<0 || mesh.Roughness>1) throw std::invalid_argument("Static material roughness must be finite in [0,1]. Binding="+key);
                if(!keys.insert(mesh.Key).second) throw std::logic_error("Static lightmap drawable identity is ambiguous. Binding="+key);
                try {
                    mesh.DiffuseTexture=Textures.GetLightmapTexture(LightmapDiffuseTexture(material));
                } catch(const std::exception& error) {
                    throw std::runtime_error("Static lightmap diffuse texture rejected. Binding="+key+" reason="+error.what());
                }
                auto& vertices=verticesByKey[mesh.Key];vertices.reserve(static_cast<std::size_t>(count)*8);
                for(int element=start;element<start+count;++element) {
                    const auto vertex=original.Indices[element];
                    if(vertex>=original.Positions.size()) throw std::invalid_argument("Static lightmap index is outside its retained source mesh. Binding="+key);
                    const auto p=original.Positions[vertex],n=original.Normals[vertex];const auto uv=original.TexCoords[vertex];
                    mesh.Positions.push_back(TransformLightmap(p,world,true));mesh.Normals.push_back(TransformLightmap(n,normalMatrix,false));mesh.TexCoords.push_back(uv);mesh.Indices.push_back(static_cast<uint32_t>(mesh.Indices.size()));
                    vertices.insert(vertices.end(),{p.X,p.Y,p.Z,n.X,n.Y,n.Z,uv.X,uv.Y});
                }
                drawableByKey.emplace(mesh.Key,drawable);slotByKey.emplace(mesh.Key,slot);scene.Meshes.push_back(std::move(mesh));
            }
        }
        std::sort(scene.Meshes.begin(),scene.Meshes.end(),[](const hellm::Mesh& a,const hellm::Mesh& b){return a.Key<b.Key;});
        if(ExportMode) {
            hellm::SaveScene(scene,Path);Initialized=true;
            RuntimeRenderDiagnostics::WriteHostEvent("lightmap-snapshot","path="+Path+" meshes="+std::to_string(scene.Meshes.size())+" fingerprint="+std::to_string(hellm::ComputeSceneFingerprint(scene)));return;
        }
        const auto lightmap=hellm::LoadLightmap(Path);
        if(lightmap.SourceFingerprint!=hellm::ComputeSceneFingerprint(scene)) throw std::runtime_error("Saved lightmap is stale: initialized scene geometry, material or sun differs from its snapshot.");
        if(lightmap.Bindings.size()!=scene.Meshes.size()) throw std::runtime_error("Saved lightmap submesh binding count differs from the initialized scene.");
        Probe=std::make_unique<Win32SceneReflectionProbe>(device,lightmap.Probe);
        std::unordered_set<std::string> bound;
        for(const auto& binding:lightmap.Bindings) {
            const auto vertices=verticesByKey.find(binding.Key);
            if(vertices==verticesByKey.end() || !bound.insert(binding.Key).second || binding.CornerUvs.size()!=vertices->second.size()/8) throw std::runtime_error("Saved lightmap has missing, duplicate or incompatible triangle-corner bindings.");
            auto draw=Win32SceneLightmapDraw::Create(device,vertices->second,binding.CornerUvs);
            Draws[drawableByKey.at(binding.Key)].emplace(slotByKey.at(binding.Key),std::move(draw));
        }
        D3D11_TEXTURE2D_DESC description{};description.Width=lightmap.Width;description.Height=lightmap.Height;description.MipLevels=description.ArraySize=1;description.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;description.SampleDesc.Count=1;description.BindFlags=D3D11_BIND_SHADER_RESOURCE;description.Usage=D3D11_USAGE_IMMUTABLE;
        D3D11_SUBRESOURCE_DATA initial{lightmap.Rgba.data(),lightmap.Width*16,0};CheckLightmap(device->CreateTexture2D(&description,&initial,&Atlas),"Create saved HDR lightmap");CheckLightmap(device->CreateShaderResourceView(Atlas.Get(),nullptr,&View),"Create saved HDR lightmap view");
        const float enabled[4]={1,0,0,0};D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=16;buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;buffer.Usage=D3D11_USAGE_IMMUTABLE;D3D11_SUBRESOURCE_DATA constants{enabled,0,0};CheckLightmap(device->CreateBuffer(&buffer,&constants,&Parameters),"Create saved lightmap constants");
        D3D11_SAMPLER_DESC sampler{};sampler.Filter=D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sampler.MaxLOD=0;CheckLightmap(device->CreateSamplerState(&sampler,&Sampler),"Create saved lightmap sampler");Initialized=true;
        RuntimeRenderDiagnostics::WriteHostEvent("lightmap-loaded","path="+Path+" meshes="+std::to_string(Draws.size())+" fingerprint="+std::to_string(lightmap.SourceFingerprint));
    }
    const Win32SceneLightmapDraw* Win32SceneLightmap::Bind(ID3D11DeviceContext* context,ICamera* camera,IDrawable3D* drawable,int submesh) const {
        Unbind(context);
        if(!Initialized || ExportMode || (camera!=MainCamera && camera!=GlassCamera)) return nullptr;
        const auto owner=Draws.find(drawable);if(owner==Draws.end()) return nullptr;
        const auto found=owner->second.find(submesh);if(found==owner->second.end()) return nullptr;
        const auto& draw=found->second;draw.Bind(context);Probe->Bind(context);
        auto* view=View.Get();auto* buffer=Parameters.Get();auto* sampler=Sampler.Get();context->PSSetShaderResources(23,1,&view);context->PSSetConstantBuffers(11,1,&buffer);context->PSSetSamplers(9,1,&sampler);return &draw;
    }
}
