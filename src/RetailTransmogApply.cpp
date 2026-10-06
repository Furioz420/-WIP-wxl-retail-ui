#include "ExtensionApi.hpp"
#include "TransmogApplyWire.hpp"
#include "game/Binding.hpp"
#include "offsets/engine/Lua.hpp"
#include <mutex>
namespace {
namespace W=WXL::TransmogApplyWire; namespace L=wxl::offsets::engine::lua;
std::mutex lock;W::Result result;uint32_t requested=0;bool ready=false;
void __cdecl Receive(const uint8_t* data,uint32_t size,void*) {
    W::Result incoming;if(!W::DecodeResult({data,size},incoming))return;
    std::lock_guard guard(lock);if(incoming.Id!=requested||ready)return;result=incoming;ready=true;
}
int __cdecl Send(void* state) {
    size_t size=0;auto text=wxl::game::Native<L::LuaToStringFn>(L::kLuaToString)(state,1,&size);
    W::Batch batch;bool sent=false;
    if(text&&W::DecodeBatch({reinterpret_cast<const uint8_t*>(text),size},batch)) {
        {std::lock_guard guard(lock);if(!++requested)++requested;batch.Id=requested;ready=false;}
        auto bytes=W::EncodeBatch(batch);
        sent=wxl_retail_ui::Network()->Send(W::Request,bytes.data(),uint32_t(bytes.size()))!=0;
    }
    if(sent)wxl::game::Native<L::LuaPushNumberFn>(L::kLuaPushNumber)(state,batch.Id);
    else wxl::game::Native<L::LuaPushNilFn>(L::kLuaPushNil)(state);
    return 1;
}
int __cdecl Read(void* state) {
    std::lock_guard guard(lock);if(!ready){wxl::game::Native<L::LuaPushNilFn>(L::kLuaPushNil)(state);return 1;}
    auto push=wxl::game::Native<L::LuaPushNumberFn>(L::kLuaPushNumber);
    push(state,result.Id);push(state,result.Status);push(state,result.Money);push(state,result.Token);push(state,result.Amount);return 5;
}
}
bool wxl_retail_ui::InstallRetailTransmogApply() {
    auto n=Network();auto s=FrameScript();
    bool ok=n->RegisterClientOpcode(W::Request,"CMSG_WXL_TRANSMOG_EDITOR_REQUEST")!=0;
    ok&=n->RegisterServerOpcode(W::Response,"SMSG_WXL_TRANSMOG_EDITOR_RESULT",Receive,nullptr)!=0;
    ok&=s->RegisterFunction("_WXL_TRANSMOG_EDITOR_SEND",Send)!=0;
    ok&=s->RegisterFunction("_WXL_TRANSMOG_EDITOR_RESULT",Read)!=0;return ok;
}
