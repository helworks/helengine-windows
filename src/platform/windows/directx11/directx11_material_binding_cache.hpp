#pragma once
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace helengine::windows {
/// <summary>Retains backend-owned material values while exact inherited property revisions and mutable layout arrays remain current.</summary>
template<class TMaterial,class TValue> class DirectX11MaterialBindingCache final {
    /// <summary>Owns binding metadata rather than dereferencing a possibly released previous layout.</summary>
    struct Binding {
        /// <summary>Distinguishes absent bindings from an authored slot zero.</summary>
        bool Present=false;
        /// <summary>Distinguishes a null generated string from empty text.</summary>
        bool NullName=false;
        /// <summary>Owns the original authored binding name.</summary>
        std::string Name;
        /// <summary>Preserves the complete resource class and register contract.</summary>
        int Type=0,Set=0,Slot=0,Size=0;
        /// <summary>Compares immutable metadata without relying on recycled object addresses.</summary>
        bool operator==(const Binding&)const=default;
    };
    /// <summary>Captures each binding in its exact order, including null arrays and null entries.</summary>
    struct Bindings {
        /// <summary>Preserves null independently from an empty generated array.</summary>
        bool Null=false;
        /// <summary>Owns ordered records for allocation-free hit validation.</summary>
        std::vector<Binding> Items;
    };
    /// <summary>Captures one material dependency independently of generated/native allocator address reuse.</summary>
    struct Dependency {
        /// <summary>Borrows only identity comparisons for material, parent, layout and property block.</summary>
        const void* Material;const void* Parent;const void* Layout;const void* Properties;
        /// <summary>Tracks property writes and replacement/disposal of layout associations.</summary>
        uint64_t PropertyRevision,LayoutRevision;
        /// <summary>Owns both lookup configurations that determine inherited value resolution.</summary>
        Bindings Constants,Textures;
    };
    /// <summary>Publishes dependencies and completed backend resources as one transaction.</summary>
    struct Entry {
        /// <summary>Owns the full shader-material ancestry used to resolve constants and textures.</summary>
        std::vector<Dependency> Dependencies;
        /// <summary>Owns the completed backend preparation until invalidation or release.</summary>
        TValue Value;
    };
    /// <summary>Owns at most the latest successful preparation for each live material used by the renderer.</summary>
    std::unordered_map<TMaterial*,std::unique_ptr<Entry>> Entries;
    /// <summary>Copies one live immutable binding without borrowing its string storage.</summary>
    template<class TBinding> static Binding CaptureBinding(TBinding* current) {
        if(!current)return {};
        const auto& name=current->get_Name();
        return {true,name.is_null(),name.raw(),static_cast<int>(current->get_ResourceType()),current->get_Set(),current->get_Slot(),current->get_Size()};
    }
    /// <summary>Captures a mutable generated array before publishing its dependent backend value.</summary>
    template<class TArray> static Bindings CaptureBindings(TArray* current) {
        Bindings result;result.Null=current==nullptr;if(!current)return result;
        if(current->Length<0)throw std::invalid_argument("Material binding array has negative length.");
        result.Items.reserve(static_cast<size_t>(current->Length));
        for(int32_t i=0;i<current->Length;++i)result.Items.push_back(CaptureBinding((*current)[i]));
        return result;
    }
    /// <summary>Compares current binding fields without allocating copied names on a cache hit.</summary>
    template<class TArray> static bool Matches(const Bindings& expected,TArray* current) {
        if(!current)return expected.Null;
        if(expected.Null || current->Length<0 || static_cast<size_t>(current->Length)!=expected.Items.size())return false;
        for(int32_t i=0;i<current->Length;++i){
            auto* binding=(*current)[i];const auto& item=expected.Items[static_cast<size_t>(i)];
            if(!binding){if(item.Present)return false;continue;}
            if(!item.Present)return false;
            const auto& name=binding->get_Name();
            if(name.is_null()!=item.NullName || name.raw()!=item.Name || static_cast<int>(binding->get_ResourceType())!=item.Type || binding->get_Set()!=item.Set || binding->get_Slot()!=item.Slot || binding->get_Size()!=item.Size)return false;
        }
        return true;
    }
    /// <summary>Follows only shader-backed parents, matching the existing material resolver's inheritance rule.</summary>
    static TMaterial* Parent(TMaterial* material){return dynamic_cast<TMaterial*>(material->get_ParentMaterial());}
    /// <summary>Builds a complete dependency chain before publication and rejects broken/disposed state or cycles.</summary>
    static std::vector<Dependency> Capture(TMaterial* material){
        if(!material)throw std::invalid_argument("Prepared material requires a live material.");
        std::vector<Dependency> result;
        for(auto* current=material;current;current=Parent(current)){
            for(const auto& item:result)if(item.Material==current)throw std::invalid_argument("Material inheritance cycle.");
            auto* layout=current->get_Layout();auto* properties=current->get_Properties();
            if(!layout || !properties)throw std::invalid_argument("Prepared material requires live layout and properties.");
            result.push_back({current,current->get_ParentMaterial(),layout,properties,properties->get_Revision(),current->get_LayoutRevision(),CaptureBindings(layout->get_ConstantBufferBindings()),CaptureBindings(layout->get_TextureBindings())});
        }
        return result;
    }
public:
    /// <summary>Returns a retained immutable value only when every ancestor and binding configuration still matches.</summary>
    const TValue* Read(TMaterial* material)const {
        const auto found=Entries.find(material);if(found==Entries.end())return nullptr;
        auto* current=material;
        for(const auto& expected:found->second->Dependencies){
            if(!current || current!=expected.Material || current->get_ParentMaterial()!=expected.Parent)return nullptr;
            auto* layout=current->get_Layout();auto* properties=current->get_Properties();
            if(!layout || !properties || layout!=expected.Layout || properties!=expected.Properties || properties->get_Revision()!=expected.PropertyRevision || current->get_LayoutRevision()!=expected.LayoutRevision)return nullptr;
            if(!Matches(expected.Constants,layout->get_ConstantBufferBindings()) || !Matches(expected.Textures,layout->get_TextureBindings()))return nullptr;
            current=Parent(current);
        }
        return current?nullptr:&found->second->Value;
    }
    /// <summary>Publishes only a fully built value and complete dependency chain; failures preserve the previous entry.</summary>
    const TValue& Publish(TMaterial* material,TValue value){
        auto replacement=std::make_unique<Entry>(Entry{Capture(material),std::move(value)});
        auto* result=&replacement->Value;Entries.insert_or_assign(material,std::move(replacement));return *result;
    }
    /// <summary>Releases material-owned preparation and every inherited dependent before the material itself retires.</summary>
    void Invalidate(const void* material){
        for(auto it=Entries.begin();it!=Entries.end();){
            bool depends=false;for(const auto& item:it->second->Dependencies)if(item.Material==material || item.Parent==material)depends=true;
            if(depends)it=Entries.erase(it);else ++it;
        }
    }
    /// <summary>Releases all retained backend values at renderer shutdown or an explicit asset flush.</summary>
    void Clear(){Entries.clear();}
    /// <summary>Reports the number of live prepared material associations.</summary>
    size_t GetCount()const{return Entries.size();}
};
}
