#include "platform/windows/win32/win32_scene_gi.hpp"
#include "platform/windows/win32/win32_render_bridge.hpp"
#include "platform/windows/runtime/runtime_render_diagnostics.hpp"
#include "DirectionalLightComponent.hpp"
#include "Entity.hpp"
#include "LightDirectionUtility.hpp"
#include "ObjectManager.hpp"
#include "RuntimeSubmesh.hpp"
#include "ModelPrimitiveTopology.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <chrono>
#include <stdexcept>
#include <string>
#include <sstream>

namespace helengine::windows {
    namespace {
        /// <summary>Preserves Direct3D failures rather than publishing a partially initialized transport resource.</summary>
        void CheckGi(HRESULT result, const char* operation) {
            if(FAILED(result)) throw std::runtime_error(std::string(operation)+" failed: "+std::to_string(result));
        }
        /// <summary>Converts an engine world matrix once at the fixed IEEE32 transport boundary.</summary>
        std::vector<double> GiMatrix(const float4x4& matrix) {
            return {matrix.M11.ToSingle(),matrix.M12.ToSingle(),matrix.M13.ToSingle(),matrix.M14.ToSingle(),
                matrix.M21.ToSingle(),matrix.M22.ToSingle(),matrix.M23.ToSingle(),matrix.M24.ToSingle(),
                matrix.M31.ToSingle(),matrix.M32.ToSingle(),matrix.M33.ToSingle(),matrix.M34.ToSingle(),
                matrix.M41.ToSingle(),matrix.M42.ToSingle(),matrix.M43.ToSingle(),matrix.M44.ToSingle()};
        }
        /// <summary>Applies the engine's row-vector affine world transform to one retained source position.</summary>
        helgi::Vec3 GiTransform(helgi::Vec3 p, const std::vector<double>& matrix) {
            return {static_cast<float>(p.X*matrix[0]+p.Y*matrix[4]+p.Z*matrix[8]+matrix[12]),
                static_cast<float>(p.X*matrix[1]+p.Y*matrix[5]+p.Z*matrix[9]+matrix[13]),
                static_cast<float>(p.X*matrix[2]+p.Y*matrix[6]+p.Z*matrix[10]+matrix[14])};
        }
        /// <summary>Omits physically zero-area surfaces, including valid meshes temporarily collapsed by animation.</summary>
        bool GiHasArea(const helgi::Triangle& triangle) {
            const double ax=triangle.B.X-triangle.A.X,ay=triangle.B.Y-triangle.A.Y,az=triangle.B.Z-triangle.A.Z;
            const double bx=triangle.C.X-triangle.A.X,by=triangle.C.Y-triangle.A.Y,bz=triangle.C.Z-triangle.A.Z;
            const double x=ay*bz-az*by,y=az*bx-ax*bz,z=ax*by-ay*bx;
            return !(x*x+y*y+z*z<1e-24);
        }
        /// <summary>Validates explicit borrowed scene sources and a positive per-update work allowance.</summary>
        void ValidateGiSources(Core* owner, DirectionalLightComponent* sun, std::uint32_t workItems) {
            if(owner==nullptr || sun==nullptr || workItems==0) throw std::invalid_argument("Scene GI requires an owner, an authored directional sun and a positive work budget.");
            if(owner->get_ObjectManager()==nullptr || sun->get_Parent()==nullptr) throw std::invalid_argument("Scene GI requires initialized scene sources.");
        }
    }

    Win32SceneGi::Win32SceneGi(ID3D11Device* device, Core* owner, DirectionalLightComponent* sun, const helgi::Config& config, std::uint32_t workItems)
        : Owner(owner),Sun(sun),Settings(config),WorkItems(workItems),Solver(config) {
        ValidateGiSources(owner,sun,workItems);
        if(device==nullptr) throw std::invalid_argument("Scene GI requires a graphics device.");
        const helgi::Field& field=Solver.GetField();
        D3D11_TEXTURE2D_DESC texture{};
        texture.Width=field.Width;texture.Height=field.Height;texture.MipLevels=texture.ArraySize=1;
        texture.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;texture.SampleDesc.Count=1;texture.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initial{field.AtlasRgba.data(),field.Width*4*sizeof(float),0};
        CheckGi(device->CreateTexture2D(&texture,&initial,&Atlas),"Create scene GI atlas");
        CheckGi(device->CreateShaderResourceView(Atlas.Get(),nullptr,&AtlasView),"Create scene GI atlas view");
        const float parameters[12]={config.Minimum.X,config.Minimum.Y,config.Minimum.Z,1,
            (config.Maximum.X-config.Minimum.X)/config.CountX,(config.Maximum.Y-config.Minimum.Y)/config.CountY,(config.Maximum.Z-config.Minimum.Z)/config.CountZ,0,
            static_cast<float>(config.CountX),static_cast<float>(config.CountY),static_cast<float>(config.CountZ),0};
        D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=sizeof(parameters);buffer.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA constants{parameters,0,0};
        CheckGi(device->CreateBuffer(&buffer,&constants,&Parameters),"Create scene GI parameters");
    }

    bool Win32SceneGi::Matches(const helgi::Config& config) const {
        return Settings.Minimum.X==config.Minimum.X && Settings.Minimum.Y==config.Minimum.Y && Settings.Minimum.Z==config.Minimum.Z
            && Settings.Maximum.X==config.Maximum.X && Settings.Maximum.Y==config.Maximum.Y && Settings.Maximum.Z==config.Maximum.Z
            && Settings.CountX==config.CountX && Settings.CountY==config.CountY && Settings.CountZ==config.CountZ && Settings.RaysPerLobe==config.RaysPerLobe;
    }

    void Win32SceneGi::Reconfigure(Core* owner, DirectionalLightComponent* sun, std::uint32_t workItems) {
        ValidateGiSources(owner,sun,workItems);
        if(Owner!=owner) throw std::logic_error("ClearSceneGi must release the previous scene before configuring a different owner.");
        Owner=owner;Sun=sun;WorkItems=workItems;
    }

    std::shared_ptr<Win32SceneGiMesh> Win32SceneGi::CaptureMesh(ModelAsset* asset) {
        if(asset==nullptr || asset->Positions==nullptr) throw std::invalid_argument("Scene GI mesh upload requires raw positions.");
        auto mesh=std::make_shared<Win32SceneGiMesh>();
        mesh->Positions.reserve(asset->Positions->Length);
        for(int index=0;index<asset->Positions->Length;++index) {
            const auto& position=(*asset->Positions)[index];
            mesh->Positions.push_back({position.X.ToSingle(),position.Y.ToSingle(),position.Z.ToSingle()});
        }
        if(asset->Indices32!=nullptr && asset->Indices32->Length>0) mesh->Indices.assign(asset->Indices32->Data,asset->Indices32->Data+asset->Indices32->Length);
        else if(asset->Indices16!=nullptr && asset->Indices16->Length>0) mesh->Indices.assign(asset->Indices16->Data,asset->Indices16->Data+asset->Indices16->Length);
        return mesh;
    }

    void Win32SceneGi::SubmitDrawable(IDrawable3D* drawable) {
        if(drawable==nullptr || drawable->get_Parent()==nullptr || !drawable->get_Parent()->get_IsHierarchyEnabled() || drawable->get_Model()==nullptr) return;
        auto* model=dynamic_cast<Win32RuntimeModel*>(drawable->get_Model());
        if(model==nullptr || !model->GiMesh) throw std::logic_error("Scene GI drawable requires CPU geometry retained during Windows mesh upload.");
        auto* materials=drawable->get_Materials();
        auto* submeshes=model->get_Submeshes();
        int submeshCount=submeshes!=nullptr && submeshes->Length>0?submeshes->Length:1;
        std::vector<double> signature=GiMatrix(drawable->get_Parent()->get_WorldTransformMatrix());
        signature.reserve(16+submeshCount*6);
        for(int slot=0;slot<submeshCount;++slot) {
            auto* submesh=submeshes!=nullptr && submeshes->Length>0?(*submeshes)[slot]:nullptr;
            const int start=submesh!=nullptr?submesh->get_IndexStart():0;
            const int elements=submesh!=nullptr && submesh->get_IndexCount()>0?submesh->get_IndexCount():static_cast<int>(model->GiMesh->Indices.empty()?model->GiMesh->Positions.size():model->GiMesh->Indices.size());
            const bool triangles=submesh==nullptr || submesh->get_PrimitiveTopology()==ModelPrimitiveTopology::TriangleList;
            if(!triangles) {signature.insert(signature.end(),{0,0,0,0,0,0});continue;}
            if(start<0 || elements<0) throw std::invalid_argument("Scene GI triangle ranges cannot be negative.");
            if(materials==nullptr || materials->Length==0) throw std::logic_error("Scene GI requires authored standard material colors.");
            auto* material=dynamic_cast<ShaderRuntimeMaterial*>((*materials)[slot<materials->Length?slot:0]);
            if(material==nullptr) throw std::logic_error("Scene GI requires a shader material exposing BaseColorBuffer.");
            Array<std::uint8_t>* colorData=nullptr;
            if(!material->TryResolveConstantBufferData__out1("BaseColorBuffer",colorData) || colorData==nullptr || colorData->Length!=16) {
                delete colorData;throw std::logic_error("Scene GI BaseColorBuffer must contain four IEEE32 values.");
            }
            float color[4];std::memcpy(color,colorData->Data,sizeof(color));delete colorData;
            for(float channel:color) if(!std::isfinite(channel) || channel<0 || channel>1) throw std::invalid_argument("Scene GI albedo and opacity must be finite from zero to one.");
            // The first adapter models opaque diffuse surfaces; transparent surfaces neither occlude nor reflect transport.
            signature.insert(signature.end(),{static_cast<double>(start),color[3]>=0.999f?static_cast<double>(elements):0,color[0],color[1],color[2],color[3]});
        }
        auto found=Contributors.find(drawable);
        if(found!=Contributors.end() && found->second.Mesh==model->GiMesh && found->second.Signature==signature) {found->second.Seen=true;return;}
        std::vector<helgi::Triangle> triangles;
        const auto& mesh=*model->GiMesh;
        for(std::size_t range=16;range<signature.size();range+=6) {
            const std::size_t start=static_cast<std::size_t>(signature[range]),count=static_cast<std::size_t>(signature[range+1]);
            const std::size_t available=mesh.Indices.empty()?mesh.Positions.size():mesh.Indices.size();
            if(signature[range]<0 || count%3!=0 || start>available || count>available-start) throw std::invalid_argument("Scene GI triangle range is outside its retained mesh.");
            for(std::size_t element=start;element<start+count;element+=3) {
                std::uint32_t indices[3];
                for(int corner=0;corner<3;++corner) {indices[corner]=mesh.Indices.empty()?static_cast<std::uint32_t>(element+corner):mesh.Indices[element+corner];if(indices[corner]>=mesh.Positions.size()) throw std::invalid_argument("Scene GI triangle index is outside its retained mesh.");}
                helgi::Triangle triangle{GiTransform(mesh.Positions[indices[0]],signature),GiTransform(mesh.Positions[indices[1]],signature),GiTransform(mesh.Positions[indices[2]],signature),{static_cast<float>(signature[range+2]),static_cast<float>(signature[range+3]),static_cast<float>(signature[range+4])}};
                if(GiHasArea(triangle)) triangles.push_back(triangle);
            }
        }
        if(found==Contributors.end()) found=Contributors.emplace(drawable,Win32SceneGiContributor{NextIdentity++,0,{}, {},true}).first;
        auto& contributor=found->second;
        Solver.SetContributor(contributor.Identity,contributor.Revision+1,triangles);
        ++contributor.Revision;contributor.Mesh=model->GiMesh;contributor.Signature=std::move(signature);contributor.Seen=true;
    }

    void Win32SceneGi::Update(ID3D11DeviceContext* context) {
        const double seconds=Owner->get_TotalElapsedSeconds();
        if(seconds==LastUpdateSeconds) return;
        const auto started=std::chrono::steady_clock::now();
        if(Sun->get_Parent()==nullptr) throw std::logic_error("Scene GI sun must remain alive until its explicit teardown.");
        float3 direction=LightDirectionUtility::GetEntityForwardDirection(Sun->get_Parent());
        auto color=Sun->get_Color();const float intensity=Sun->get_Parent()->get_IsHierarchyEnabled()?Sun->get_Intensity():0;
        Solver.SetSun({{-direction.X.ToSingle(),-direction.Y.ToSingle(),-direction.Z.ToSingle()},{color.X.ToSingle()*intensity,color.Y.ToSingle()*intensity,color.Z.ToSingle()*intensity}});
        for(auto& entry:Contributors) entry.second.Seen=false;
        auto* drawables=Owner->get_ObjectManager()->get_Drawables3D();
        for(int index=0;index<drawables->Count();++index) SubmitDrawable((*drawables)[index]);
        for(auto iterator=Contributors.begin();iterator!=Contributors.end();) {
            if(!iterator->second.Seen) {Solver.RemoveContributor(iterator->second.Identity);iterator=Contributors.erase(iterator);} else ++iterator;
        }
        const auto submitted=std::chrono::steady_clock::now();
        if(Solver.AdvanceUpdate(WorkItems)) {
            const auto& field=Solver.GetField();
            ID3D11ShaderResourceView* none=nullptr;context->PSSetShaderResources(17,1,&none);
            context->UpdateSubresource(Atlas.Get(),0,nullptr,field.AtlasRgba.data(),field.Width*4*sizeof(float),0);
            const auto stats=Solver.GetStats();
            const double scanMs=std::chrono::duration<double,std::milli>(submitted-started).count();
            const double solveUploadMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-submitted).count();
            if(DebugSpheres || stats.Publications<=4 || stats.Publications%60==0) {
                const std::size_t cellCount=static_cast<std::size_t>(Settings.CountX)*Settings.CountY*Settings.CountZ;
                std::size_t emptyProbes=0,nonzeroEmptyProbes=0;
                float peakIrradiance=0;
                for(std::size_t cell=0;cell<cellCount;++cell) {
                    if(field.AtlasRgba[cell*4+3]<=0.5f) continue;
                    ++emptyProbes;
                    bool nonzero=false;
                    for(std::size_t lobe=0;lobe<6;++lobe) {
                        const std::size_t index=(cell+lobe*cellCount)*4;
                        if(field.AtlasRgba[index+3]<=0.5f) continue;
                        const float peak=std::max({field.AtlasRgba[index],field.AtlasRgba[index+1],field.AtlasRgba[index+2]});
                        peakIrradiance=std::max(peakIrradiance,peak);
                        nonzero=nonzero || peak>0;
                    }
                    if(nonzero) ++nonzeroEmptyProbes;
                }
                std::ostringstream message;
                message<<"generation="<<field.Generation<<" contributors="<<Contributors.size()
                    <<" rays="<<stats.RaysTraced<<" voxel_tests="<<stats.TriangleVoxelTests
                    <<" grid="<<Settings.CountX<<","<<Settings.CountY<<","<<Settings.CountZ
                    <<" empty_probes="<<emptyProbes<<" nonzero_empty_probes="<<nonzeroEmptyProbes
                    <<" peak_irradiance="<<std::scientific<<peakIrradiance
                    <<" scan_ms="<<std::fixed<<scanMs<<" solve_upload_ms="<<solveUploadMs;
                RuntimeRenderDiagnostics::WriteHostEvent("scene-gi-publication",message.str());
                if(DebugSpheres) RuntimeRenderDiagnostics::WriteHostEvent("scene-gi-debug-spheres","mode=1 state=published generation="+std::to_string(field.Generation)+" free_probes="+std::to_string(emptyProbes)+" nonzero_probes="+std::to_string(nonzeroEmptyProbes));
            }
        }
        LastUpdateSeconds=seconds;
    }

    void Win32SceneGi::Bind(ID3D11DeviceContext* context) const {
        auto* buffer=Parameters.Get();auto* view=AtlasView.Get();
        context->PSSetConstantBuffers(9,1,&buffer);context->PSSetShaderResources(17,1,&view);
    }

    void Win32SceneGi::ConfigureDebugSpheres(bool enabled) {
        if(enabled && !DebugSpheres) {
            Microsoft::WRL::ComPtr<ID3D11Device> device;Atlas->GetDevice(&device);
            auto spheres=std::make_unique<Win32SceneGiSpheres>(device.Get());
            auto usage=std::make_unique<Win32SceneGiUsage>(device.Get(),Settings.CountX*Settings.CountY*Settings.CountZ);
            DebugSpheres=std::move(spheres);DebugUsage=std::move(usage);
        } else if(!enabled) {DebugSpheres.reset();DebugUsage.reset();}
    }

    bool Win32SceneGi::IsDebugSpheresEnabled() const {return DebugSpheres!=nullptr;}
    Win32SceneGiUsage* Win32SceneGi::GetDebugUsage() const {return DebugUsage.get();}
    void Win32SceneGi::BeginDebugUsage(ID3D11DeviceContext* context) {if(!DebugUsage) throw std::logic_error("Receiver capture requires explicit sphere diagnostics.");DebugUsage->Begin(context,AtlasView.Get(),Parameters.Get());}
    std::uint64_t Win32SceneGi::GetPublishedGeneration() const {return Solver.GetField().Generation;}
    double Win32SceneGi::GetSceneUpdateSeconds() const {return Owner->get_TotalElapsedSeconds();}

    void Win32SceneGi::ConfigureIndirectOnly(ID3D11DeviceContext* context,bool enabled) {
        if(enabled==IndirectOnly) return;
        if(!context) throw std::invalid_argument("Voxel indirect-only configuration requires the rendering context.");
        const float parameters[12]={Settings.Minimum.X,Settings.Minimum.Y,Settings.Minimum.Z,1,
            (Settings.Maximum.X-Settings.Minimum.X)/Settings.CountX,(Settings.Maximum.Y-Settings.Minimum.Y)/Settings.CountY,(Settings.Maximum.Z-Settings.Minimum.Z)/Settings.CountZ,enabled?1.0f:0.0f,
            static_cast<float>(Settings.CountX),static_cast<float>(Settings.CountY),static_cast<float>(Settings.CountZ),0};
        context->UpdateSubresource(Parameters.Get(),0,nullptr,parameters,0,0);IndirectOnly=enabled;
    }

    bool Win32SceneGi::IsIndirectOnly() const {return IndirectOnly;}

    void Win32SceneGi::DrawDebugSpheres(ID3D11DeviceContext* context,const float* viewProjection) const {
        if(DebugSpheres && Solver.GetField().Generation>0) DebugSpheres->Draw(context,viewProjection,AtlasView.Get(),Parameters.Get(),Settings.CountX*Settings.CountY*Settings.CountZ,DebugUsage->GetView());
    }
}
