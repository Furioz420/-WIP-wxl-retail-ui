#include "ExtensionApi.hpp"
#include "TransmogCustomSetWire.hpp"
#include "game/Binding.hpp"
#include "offsets/engine/Lua.hpp"
#include <mutex>
#include <cmath>
namespace {
namespace W=WXL::TransmogCustomSetWire;namespace L=wxl::offsets::engine::lua;
std::mutex lock;uint32_t requested=0;bool ready=false;W::State result;
void __cdecl Receive(const uint8_t* data,uint32_t size,void*){W::State v;if(!W::Decode({data,size},v))return;std::lock_guard guard(lock);if(v.Id!=requested||ready)return;result=std::move(v);ready=true;}
int __cdecl Send(void* state){size_t size=0;auto text=wxl::game::Native<L::LuaToStringFn>(L::kLuaToString)(state,1,&size);W::Request r;bool sent=false;
 if(text&&W::Decode({reinterpret_cast<const uint8_t*>(text),size},r)){{std::lock_guard guard(lock);if(!++requested)++requested;r.Id=requested;ready=false;}auto bytes=W::Encode(r);sent=wxl_retail_ui::Network()->Send(W::RequestOpcode,bytes.data(),uint32_t(bytes.size()))!=0;}
 if(sent)wxl::game::Native<L::LuaPushNumberFn>(L::kLuaPushNumber)(state,r.Id);else wxl::game::Native<L::LuaPushNilFn>(L::kLuaPushNil)(state);return 1;}
int __cdecl Read(void* state){std::lock_guard guard(lock);if(!ready){wxl::game::Native<L::LuaPushNilFn>(L::kLuaPushNil)(state);return 1;}auto push=wxl::game::Native<L::LuaPushNumberFn>(L::kLuaPushNumber);push(state,result.Id);push(state,result.Status);push(state,result.Changed);push(state,result.Used);push(state,result.Rows.size());push(state,result.Icons.size());return 6;}
int __cdecl Row(void* state){double i=wxl::game::Native<L::LuaToNumberFn>(L::kLuaToNumber)(state,1);std::lock_guard guard(lock);if(!ready||!std::isfinite(i)||i!=std::floor(i)||i<1||i>result.Rows.size()){wxl::game::Native<L::LuaPushNilFn>(L::kLuaPushNil)(state);return 1;}auto const&r=result.Rows[size_t(i)-1];auto n=wxl::game::Native<L::LuaPushNumberFn>(L::kLuaPushNumber);auto s=wxl::game::Native<L::LuaPushStringFn>(L::kLuaPushString);n(state,r.Entry);n(state,r.Revision);s(state,r.Name.c_str());s(state,r.Payload.c_str());auto b=wxl::game::Native<L::LuaPushBooleanFn>(L::kLuaPushBoolean);b(state,r.Shared);b(state,r.Editable);return 6;}
int __cdecl Icon(void* state){double i=wxl::game::Native<L::LuaToNumberFn>(L::kLuaToNumber)(state,1);std::lock_guard guard(lock);if(!ready||!std::isfinite(i)||i!=std::floor(i)||i<1||i>result.Icons.size()){wxl::game::Native<L::LuaPushNilFn>(L::kLuaPushNil)(state);return 1;}auto const&r=result.Icons[size_t(i)-1];auto n=wxl::game::Native<L::LuaPushNumberFn>(L::kLuaPushNumber);n(state,r.Slot);n(state,r.Icon);n(state,r.Revision);return 3;}
}
bool wxl_retail_ui::InstallRetailTransmogCustomSets(){auto n=Network();auto s=FrameScript();bool ok=n->RegisterClientOpcode(W::RequestOpcode,"CMSG_WXL_TRANSMOG_CUSTOM_SET_REQUEST")!=0;ok&=n->RegisterServerOpcode(W::ResponseOpcode,"SMSG_WXL_TRANSMOG_CUSTOM_SET_STATE",Receive,nullptr)!=0;ok&=s->RegisterFunction("_WXL_TRANSMOG_CUSTOM_SET_SEND",Send)!=0;ok&=s->RegisterFunction("_WXL_TRANSMOG_CUSTOM_SET_STATE",Read)!=0;ok&=s->RegisterFunction("_WXL_TRANSMOG_CUSTOM_SET_ROW",Row)!=0;ok&=s->RegisterFunction("_WXL_TRANSMOG_CUSTOM_SET_ICON",Icon)!=0;return ok;}
