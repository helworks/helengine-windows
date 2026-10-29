#include "platform/windows/directx11/directx11_material_binding_cache.hpp"
#include "platform/windows/directx11/directx11_constant_data_pool.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace helengine::windows;
/// <summary>Exercises production cache templates with CPU-only material contracts, without pretending to execute DirectX.</summary>
class MaterialCacheTests {
    /// <summary>Supplies the generated string access used by immutable binding names.</summary>
    struct Name {std::string Value;const std::string& raw()const{return Value;}bool is_null()const{return false;}};
    /// <summary>Models immutable binding metadata whose containing array remains externally mutable.</summary>
    struct Binding {Name Text{"Color"};int Slot=3;auto get_Name()const{return Text;}int get_Slot()const{return Slot;}int get_Set()const{return 0;}int get_Size()const{return 16;}int get_ResourceType()const{return 0;}};
    /// <summary>Models the generated array container, including null binding elements.</summary>
    struct Array {
        int Length=1;Binding* Value;uint64_t Revision=0;mutable int Reads=0;bool Fail=false;
        uint64_t get_Revision()const{if(Fail)throw std::runtime_error("disposed bindings");return Revision;}
        int get_Length()const{return Length;}
        Binding* get_Item(int)const{++Reads;return Value;}
        Binding* operator[](int index)const{return get_Item(index);}
        void set_Item(int,Binding* value){if(Value!=value){++Revision;Value=value;}}
    };
    /// <summary>Supplies shared constant/texture arrays that may be reordered without a material revision.</summary>
    struct Layout {Array Constants;Array Textures{0,nullptr};auto get_ConstantBufferBindings(){return &Constants;}auto get_TextureBindings(){return &Textures;}};
    /// <summary>Supplies setter-driven property invalidation without exposing mutable payloads.</summary>
    struct Properties {uint64_t Revision=0;uint64_t get_Revision(){return Revision;}};
    /// <summary>Supplies parent chain and layout/property identities to the real dependency checker.</summary>
    struct Material {Layout* Shape;Properties* Values;Material* Parent=nullptr;uint64_t Revision=0;auto get_Layout(){return Shape;}auto get_Properties(){return Values;}auto get_ParentMaterial(){return Parent;}uint64_t get_LayoutRevision(){return Revision;}};
    /// <summary>Fails each behavioral contract with a precise message in release and debug builds.</summary>
    static void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
public:
    /// <summary>Checks revision, layout, inheritance, failure, sharing and deterministic resource eviction.</summary>
    static void Run(){
        Binding binding;Layout layout{{1,&binding}};Properties parentValues,childValues;
        Material parent{&layout,&parentValues},child{&layout,&childValues,&parent};
        DirectX11MaterialBindingCache<Material,int> cache;
        Require(!cache.Read(&child),"unprepared material unexpectedly hit");
        cache.Publish(&child,42);Require(*cache.Read(&child)==42,"unchanged prepared state missed");
        Require(layout.Constants.Reads==0 && layout.Textures.Reads==0,"versioned dependencies enumerated bindings");
        parentValues.Revision++;Require(!cache.Read(&child),"parent mutation did not invalidate child");cache.Publish(&child,43);
        Binding replacement;replacement.Slot=4;layout.Constants.set_Item(0,&replacement);
        Require(!cache.Read(&child),"in-place layout replacement did not invalidate");cache.Publish(&child,44);
        const auto retained=cache.Read(&child);layout.Textures.Fail=true;bool captureFailed=false;
        try{cache.Publish(&child,99);}catch(const std::runtime_error&){captureFailed=true;}
        layout.Textures.Fail=false;
        Require(captureFailed && cache.Read(&child)==retained && *retained==44,"failed revision capture replaced valid entry");
        Layout identical=layout;parent.Shape=&identical;
        Require(!cache.Read(&child),"ancestor collection identity ignored");parent.Shape=&layout;
        layout.Constants.set_Item(0,nullptr);Require(!cache.Read(&child),"null slot revision ignored");
        layout.Constants.set_Item(0,&replacement);cache.Publish(&child,44);
        child.Parent=nullptr;Require(!cache.Read(&child),"parent change did not invalidate");cache.Publish(&child,45);
        cache.Invalidate(&parent);Require(cache.Read(&child)!=nullptr,"unrelated parent release evicted detached child");
        child.Parent=&parent;cache.Publish(&child,46);cache.Invalidate(&parent);Require(!cache.Read(&child),"parent release kept dependent child");
        cache.Publish(&child,47);childValues.Revision++;child.Values=nullptr;bool failed=false;
        try{cache.Publish(&child,48);}catch(const std::invalid_argument&){failed=true;}
        Require(failed && !cache.Read(&child),"failed publication made disposed material valid");child.Values=&childValues;
        cache.Clear();Require(cache.GetCount()==0,"clear retained material entries");
        DirectX11ConstantDataPool<int> pool;std::vector<uint8_t> bytes(16,7);int creates=0;
        auto first=pool.Get(bytes,[&]{return ++creates;});auto second=pool.Get(bytes,[&]{return ++creates;});
        Require(first==second && creates==1 && pool.GetRetainedBytes()==16,"identical constant contents were duplicated");
        bytes[0]=9;auto changed=pool.Get(bytes,[&]{return ++creates;});Require(changed!=first && creates==2,"changed constants reused old buffer");
        bytes[0]=10;failed=false;try{pool.Get(bytes,[]()->int{throw std::runtime_error("upload failed");});}catch(const std::runtime_error&){failed=true;}
        Require(failed && pool.GetCount()==2,"failed upload published pool state");
        first.reset();Require(pool.GetCount()==2,"shared buffer released before last owner");
        second.reset();Require(pool.GetCount()==1 && pool.GetRetainedBytes()==16,"last owner did not release content key");
        changed.reset();Require(pool.GetCount()==0 && pool.GetRetainedBytes()==0,"old constant versions accumulated");
        auto beforeClear = pool.Get(bytes, [] { return 81; });
        pool.Clear();
        auto afterClear = pool.Get(bytes, [] { return 82; });
        beforeClear.reset();
        Require(pool.GetCount() == 1 && pool.GetRetainedBytes() == 16 && *afterClear == 82,
            "retiring pre-clear owner erased the new generation");
        afterClear.reset();
        Require(pool.GetCount() == 0, "post-clear generation failed to retire");
        std::shared_ptr<const int> survivor;
        {
            DirectX11ConstantDataPool<int> shortLivedPool;
            survivor = shortLivedPool.Get(bytes, [] { return 91; });
        }
        Require(*survivor == 91, "resource did not outlive pool facade");
        survivor.reset();

    }
};
/// <summary>Runs portable CPU contracts and returns failure to the test runner without loading a Windows DLL.</summary>
int main(){try{MaterialCacheTests::Run();std::cout<<"PASS native DX11 material dependencies, sharing, invalidation, failure and release\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
