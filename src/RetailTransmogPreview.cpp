#include "ExtensionApi.hpp"
#include "wxl/RetailDb2Api.h"
#include "game/Binding.hpp"
#include "offsets/engine/Lua.hpp"
#include <cmath>
#include <string>
#include "game/M2.hpp"
#include "game/Io.hpp"
#include <cstring>
#include "WeaponPreviewBounds.hpp"
#include <windows.h>
#include "offsets/game/DB2.hpp"
#include "TransmogPreviewReadiness.hpp"
namespace {
namespace L=wxl::offsets::engine::lua;
// Verified against build 12340 DressUpModel.TryOn (0x598830): the native
// validator takes the Lua state in ESI and the DressUpModel type on the stack.
void* Frame(void* state,bool baseModel=false) {
    auto& type=*reinterpret_cast<uint32_t*>(baseModel?0xDCE428:0xC0E4F4);
    if(!type)type=++*reinterpret_cast<uint32_t*>(0xD3F778);
    void* frame=nullptr;uintptr_t validate=0x4A81B0;uint32_t tag=type;
    __asm {
        push esi
        mov esi,state
        push tag
        call validate
        add esp,4
        mov frame,eax
        pop esi
    }
    return frame;
}
const uint8_t* NativeDisplayRecord(uint32_t display) noexcept {
    namespace D=wxl::offsets::game::db2::itemdisplayinfo;
    __try {
        auto lo=*reinterpret_cast<const uint32_t*>(D::kMinId);
        auto hi=*reinterpret_cast<const uint32_t*>(D::kMaxId);
        auto rows=*reinterpret_cast<const uint8_t* const* const*>(D::kIdTable);
        if(rows && display>=lo && display<=hi && hi-lo<1000000u && rows[display-lo])return rows[display-lo];
        // Compact storage is also used by this client's sparse/custom DBCs.
        auto count=*reinterpret_cast<const uint32_t*>(0xAD3DE4);
        auto compact=*reinterpret_cast<const uint8_t* const*>(0xAD3DF8);
        if(compact && count<=1000000u)
            for(uint32_t i=0;i<count;++i)
                if(*reinterpret_cast<const uint32_t*>(compact+size_t(i)*100)==display)return compact+size_t(i)*100;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return nullptr;
}
bool NativeDisplay(uint32_t display) { return NativeDisplayRecord(display)!=nullptr; }
bool RetailDisplay(uint32_t display) {
    auto api=static_cast<const WXL_RetailDb2Api*>(wxl_retail_ui::g_api->GetInterface("wxl.retail-db2",WXL_RETAIL_DB2_API_VERSION));
    if(!api)return false;
    api->RequestDisplays(&display,1);
    void* index=api->AcquireIndex();WXL_RetailDisplayRecord record{};
    bool ready=false;
    if(index && api->IndexDisplayRecord(index,display,&record)) {
        // The demand loader publishes complete per-display snapshots. Its old
        // global materialsReady flag remains false and is not a readiness gate.
        const uint32_t count=api->IndexResolvedDisplayCount(index);
        for(uint32_t i=0;i<count;++i) {
            uint32_t resolved=0;
            if(api->IndexResolvedDisplayAt(index,i,&resolved)&&resolved==display){ready=true;break;}
        }
    }
    if(index)api->ReleaseIndex(index);
    return ready;
}
// Clear the validated UI component through the native equipment-slot entry,
// which emits OnItemSlotClear and retires extension-owned attachments as well.
// Mirrors build 12340 DressUpModel::Undress (0x597BA0): held weapons are
// attached to the model, outside the armor component's equipment slots.
void ClearHeldWeapon(uint8_t* frame,uint32_t hand) {
    auto record=frame+0x384+(hand-15)*8;
    if(!record[0])return;
    using Detach=void(__cdecl*)(void*,uint32_t,uint32_t,uint32_t);
    wxl::game::Native<Detach>(0x4EB070)(*reinterpret_cast<void**>(frame+0x2A0),hand,record[5],record[4]==14?1u:0u);
    *reinterpret_cast<uint32_t*>(record)=0;
    *reinterpret_cast<uint32_t*>(record+4)=0;
}
int __cdecl ClearPreview(void* state) {
    auto frame=static_cast<uint8_t*>(Frame(state));
    void* component=frame?*reinterpret_cast<void**>(frame+0x380):nullptr;
    double requested=wxl::game::Native<L::LuaToNumberFn>(L::kLuaToNumber)(state,2);
    bool valid=std::isfinite(requested) && requested==std::floor(requested) && requested>=0 && requested<=19;
    bool ready=valid && component && *reinterpret_cast<void**>(frame+0x2A0);
    if(ready) {
        if(!requested || requested==16 || requested==18)ClearHeldWeapon(frame,15);
        if(!requested || requested==17)ClearHeldWeapon(frame,16);
        using Clear=void(__thiscall*)(void*,uint32_t);
        for(uint32_t slot=requested?uint32_t(requested)-1:0;slot<(requested?uint32_t(requested):19);++slot)
            if(slot<15 || slot>17)wxl::game::Native<Clear>(0x4EE6D0)(component,slot);
    }
    wxl::game::Native<L::LuaPushBooleanFn>(L::kLuaPushBoolean)(state,ready?1:0);
    return 1;
}
// Build 12340 internal TryOn(item, enchantments, explicit equipment slot).
// Ranged weapons are displayed drawn in the main hand, as in native TryOn.
int __cdecl PreviewItem(void* state) {
    auto number=wxl::game::Native<L::LuaToNumberFn>(L::kLuaToNumber);
    double item=number(state,2),slot=number(state,3);
    bool ready=std::isfinite(item)&&item==std::floor(item)&&item>0&&item<=4294967295.0&&
        std::isfinite(slot)&&slot==std::floor(slot)&&slot>=16&&slot<=18;
    auto frame=ready?static_cast<uint8_t*>(Frame(state)):nullptr;
    ready=frame&&*reinterpret_cast<void**>(frame+0x2A0)&&*reinterpret_cast<void**>(frame+0x380);
    if(ready) {
        using TryOn=void(__thiscall*)(void*,uint32_t,void*,int);
        wxl::game::Native<TryOn>(0x597FC0)(frame,uint32_t(item),nullptr,slot==17?16:15);
    }
    wxl::game::Native<L::LuaPushBooleanFn>(L::kLuaPushBoolean)(state,ready?1:0);return 1;
}

// Raw storage may be encoded (client flag 0xC5DEA0). Always use the
// native accessor, which decodes its 100-byte row before exposing strings.
bool ReadWeaponNames(uint32_t display,char* model,char* texture) noexcept {
    namespace D=wxl::offsets::game::db2::itemdisplayinfo;
    __try {
        alignas(4) uint8_t record[256]{};
        if(!wxl::game::Native<D::LookupFn>(D::kLookup)(reinterpret_cast<void*>(D::kStorageObject),nullptr,display,record))return false;
        const size_t offsets[2]={D::kOffModel1,D::kOffTex1};
        char* outputs[2]={model,texture};
        for(size_t field=0;field<2;++field) {
            const char* name=*reinterpret_cast<const char* const*>(record+offsets[field]);
            size_t length=0;
            if(name) {
                for(;length<511 && name[length];++length)outputs[field][length]=name[length];
                if(length==511 && name[length])return false;
            }
            outputs[field][length]=0;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
std::string AssetPath(char const* name,char const* folder,char const* extension) {
    if(!name||!*name)return {};
    std::string path=name;
    if(path.find('\\')==std::string::npos && path.find('/')==std::string::npos)path=std::string(folder)+path;
    if(path.find_last_of('.')==std::string::npos)path+=extension;
    return path;
}
bool WeaponBounds(std::string path,float* bounds) {
    if(path.size()>4 && path.substr(path.size()-4)==".mdx")path.replace(path.size()-4,4,".m2");
    void* file=nullptr;
    if(!wxl::game::io::FileOpen(path.c_str(),wxl::game::io::kOpenWholeFile,&file)||!file)return false;
    uint8_t header[196]{};uint32_t read=0;
    bool ok=wxl::game::io::FileRead(file,header,sizeof(header),&read)!=0;
    wxl::game::io::FileClose(file);
    return ok && WXL::WeaponPreviewBounds(header,read,bounds,bounds+4);
}
int __cdecl WeaponAsset(void* state) {
    auto number=wxl::game::Native<L::LuaToNumberFn>(L::kLuaToNumber);
    double value=number(state,1),inventory=number(state,2);
    std::string model,texture;
    if(std::isfinite(value)&&value>0&&value<=4294967295.0&&value==std::floor(value)) {
        uint32_t display=uint32_t(value);
        char const* folder=inventory==14?"Item\\ObjectComponents\\Shield\\":"Item\\ObjectComponents\\Weapon\\";
        if(NativeDisplay(display)) {
            char modelName[512]{},textureName[512]{};
            if(ReadWeaponNames(display,modelName,textureName)) {
                model=AssetPath(modelName,folder,".m2");
                texture=AssetPath(textureName,folder,".blp");
            }
        } else if(RetailDisplay(display)) {
            auto api=static_cast<const WXL_RetailDb2Api*>(wxl_retail_ui::g_api->GetInterface("wxl.retail-db2",WXL_RETAIL_DB2_API_VERSION));
            void* index=api->AcquireIndex();WXL_RetailDisplayRecord r{};
            if(index && api->IndexDisplayRecord(index,display,&r)) {
                model=AssetPath(r.nativeModelNames[0],folder,".m2");
                texture=AssetPath(r.nativeModelTextures[0],folder,".blp");
                if(model.empty()) {
                    WXL_RetailModelEntry entry{};
                    if(api->IndexModelAt(index,display,0,&entry)&&entry.folder) {
                        std::string full=std::string(entry.folder)+"\\";
                        model=AssetPath(entry.model,full.c_str(),".m2");
                        texture=AssetPath(entry.texture,full.c_str(),".blp");
                    }
                }
            }
            if(index)api->ReleaseIndex(index);
        }
    }
    if(model.empty()){wxl::game::Native<L::LuaPushNilFn>(L::kLuaPushNil)(state);return 1;}
    auto push=wxl::game::Native<L::LuaPushStringFn>(L::kLuaPushString);
    push(state,model.c_str());push(state,texture.c_str());
    float bounds[6]{};
    if(!WeaponBounds(model,bounds))return 2;
    for(float value:bounds)wxl::game::Native<L::LuaPushNumberFn>(L::kLuaPushNumber)(state,value);
    return 8;
}
// The camera-less Model path (0x95FC30 -> 0x4BEE60) is orthographic.
// Its transform uses UI scale at +0x7C and the conversion in 0x95FBA0.
int __cdecl WeaponFit(void* state) {
    auto frame=static_cast<uint8_t*>(Frame(state,true));
    auto number=wxl::game::Native<L::LuaToNumberFn>(L::kLuaToNumber);
    double center[3]={number(state,2),number(state,3),number(state,4)},radius=number(state,5);
    float rect[4]{};
    using GetRect=int(__thiscall*)(void*,float*);
    bool valid=frame && std::isfinite(radius)&&radius>.001&&radius<10000;
    for(double value:center)valid=valid&&std::isfinite(value)&&std::abs(value)<10000;
    valid=valid && wxl::game::Native<GetRect>(0x489230)(frame+0x20,rect)!=0;
    if(!valid){wxl::game::Native<L::LuaPushNilFn>(L::kLuaPushNil)(state);return 1;}
    double width=std::abs(rect[3]-rect[1]),height=std::abs(rect[2]-rect[0]);
    double effective=*reinterpret_cast<float*>(frame+0x7C);
    double conversion=*reinterpret_cast<float*>(0xAC0CB8)*(5.0/3.0);
    double span=width<height?width:height;
    if(!std::isfinite(span)||span<=0||!std::isfinite(effective)||effective<=0||!std::isfinite(conversion)||conversion<=0) {
        wxl::game::Native<L::LuaPushNilFn>(L::kLuaPushNil)(state);return 1;
    }
    double halfWidth=number(state,6),halfHeight=number(state,7);
    if(!std::isfinite(halfWidth)||halfWidth<=.001)halfWidth=radius;
    if(!std::isfinite(halfHeight)||halfHeight<=.001)halfHeight=radius;
    double fitWidth=width/halfWidth,fitHeight=height/halfHeight;
    double scale=(fitWidth<fitHeight?fitWidth:fitHeight)*.40/(effective*conversion);
    auto push=wxl::game::Native<L::LuaPushNumberFn>(L::kLuaPushNumber);
    push(state,scale);
    const double yaw=.65,c=std::cos(yaw),s=std::sin(yaw);
    // SetPosition is in UI units, unlike SetModelScale's model-space input.
    push(state,width*.5/effective-scale*conversion*(c*center[0]-s*center[1]));
    push(state,height*.5/effective-scale*conversion*(s*center[0]+c*center[1]));
    push(state,-scale*conversion*center[2]);
    return 4;
}
// Finalize only this thumbnail's sheet; no player/attachment reset.
int __cdecl FinishPreviewSheet(void* state) {
    auto frame=static_cast<uint8_t*>(Frame(state));
    auto component=frame?*reinterpret_cast<uint8_t**>(frame+0x380):nullptr;
    bool ready=component!=nullptr;
    if(component) {
        auto request=wxl::game::Native<L::LuaToNumberFn>(L::kLuaToNumber)(state,2);
        if(request==1)component[8]|=1; // Native sheet rebuild; compositor owns region expansion.
        ready=(component[8]&1)==0;
    }
    wxl::game::Native<L::LuaPushBooleanFn>(L::kLuaPushBoolean)(state,ready);
    return 1;
}
int __cdecl WeaponTexture(void* state) {
    auto frame=static_cast<uint8_t*>(Frame(state,true));
    void* render=frame?*reinterpret_cast<void**>(frame+0x2A0):nullptr;
    auto path=wxl::game::Native<L::LuaToStringFn>(L::kLuaToString)(state,2,nullptr);
    bool ready=render && (*reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(render)+0x10)&1u)!=0;
    if(ready) {
        // Per-instance emission controls; do not modify shared model assets.
        using Toggle=void(__thiscall*)(void*,uint32_t);
        wxl::game::Native<Toggle>(wxl::offsets::game::m2::kSetEmittersEnabled)(render,0);
        wxl::game::Native<Toggle>(wxl::offsets::game::m2::kSetRibbonsEnabled)(render,0);
    }
    if(ready&&path&&*path) {
        auto resource=wxl::game::m2::LoadResource(path);
        ready=resource!=nullptr;
        if(resource) {wxl::game::m2::BindTexSlotType(render,2,resource);wxl::game::m2::ReleaseResource(resource);}
    }
    wxl::game::Native<L::LuaPushBooleanFn>(L::kLuaPushBoolean)(state,ready);return 1;
}

int __cdecl Preview(void* state) {
    auto number=wxl::game::Native<L::LuaToNumberFn>(L::kLuaToNumber);
    double slotValue=number(state,2),displayValue=number(state,3);
    bool ready=false;uint32_t observed=0;
    constexpr int slots[19]={0,-1,1,2,3,4,5,6,7,8,-1,-1,-1,-1,10,-1,-1,-1,9};
    if(std::isfinite(slotValue)&&slotValue==std::floor(slotValue)&&slotValue>=1&&slotValue<=19&&
       std::isfinite(displayValue)&&displayValue==std::floor(displayValue)&&displayValue>0&&displayValue<=4294967295.0&&slots[int(slotValue)-1]>=0) {
        uint32_t display=uint32_t(displayValue);
        ready=WXL::PreviewDisplayReady(display,NativeDisplay,RetailDisplay);
        if(ready && number(state,4)!=0) {
            auto frame=static_cast<uint8_t*>(Frame(state));
            void* component=frame?*reinterpret_cast<void**>(frame+0x380):nullptr;
            ready=component&&*reinterpret_cast<void**>(frame+0x2A0);
            if(ready && number(state,4)==1) {
                // The same remove/apply pair used by native TryOn, on this UI
                // model's component only. No live-player fields or item records.
                int modelSlot=slots[int(slotValue)-1];
                using Remove=void(__thiscall*)(void*,uint32_t);
                using Apply=void(__thiscall*)(void*,uint32_t,uint32_t,uint32_t);
                wxl::game::Native<Remove>(0x4EE460)(component,modelSlot);
                wxl::game::Native<Apply>(0x4F2830)(component,modelSlot,display,0);
            }
            if(component)observed=*reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(component)+0x428+slots[int(slotValue)-1]*4);
        }
    }
    wxl::game::Native<L::LuaPushBooleanFn>(L::kLuaPushBoolean)(state,ready?1:0);
    wxl::game::Native<L::LuaPushNumberFn>(L::kLuaPushNumber)(state,observed);return 2;
}
}
bool wxl_retail_ui::InstallRetailTransmogPreview() {
    bool ok=FrameScript()->RegisterFunction("_WXL_TRANSMOG_PREVIEW_DISPLAY",Preview)!=0;
    ok &= FrameScript()->RegisterFunction("_WXL_TRANSMOG_PREVIEW_CLEAR",ClearPreview)!=0;
    ok &= FrameScript()->RegisterFunction("_WXL_TRANSMOG_PREVIEW_ITEM",PreviewItem)!=0;
    ok &= FrameScript()->RegisterFunction("_WXL_TRANSMOG_WEAPON_ASSET",WeaponAsset)!=0;
    ok &= FrameScript()->RegisterFunction("_WXL_TRANSMOG_WEAPON_TEXTURE",WeaponTexture)!=0;
    ok &= FrameScript()->RegisterFunction("_WXL_TRANSMOG_WEAPON_FIT",WeaponFit)!=0;
    ok &= FrameScript()->RegisterFunction("_WXL_TRANSMOG_PREVIEW_FINISH",FinishPreviewSheet)!=0;
    return ok;
}
