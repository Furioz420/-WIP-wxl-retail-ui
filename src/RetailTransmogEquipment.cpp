// Read-only equipped/applied state bridge for the Retail transmog editor.
#include "ExtensionApi.hpp"
#include "TransmogEquipmentWire.hpp"
#include "game/Binding.hpp"
#include "offsets/engine/Lua.hpp"
#include <mutex>

namespace
{
namespace Wire = WXL::TransmogEquipmentWire;
namespace luaoff = wxl::offsets::engine::lua;
std::mutex mutex;
Wire::State current;
uint32_t requested = 0, generation = 0;
bool valid = false;

void __cdecl OnState(const uint8_t* data, uint32_t size, void*)
{
    Wire::State incoming;
    if (!Wire::Decode({data, size}, incoming)) return;
    std::lock_guard lock(mutex);
    if (!requested || incoming.RequestId != requested || valid) return;
    current = incoming; valid = true; ++generation;
}
int __cdecl Request(void* state)
{
    std::vector<uint8_t> bytes;
    {
        std::lock_guard lock(mutex);
        if (!++requested) ++requested;
        valid = false;
        Wire::Put(bytes, Wire::Version, 1); Wire::Put(bytes, requested, 4);
    }
    bool sent = wxl_retail_ui::Network()->Send(Wire::RequestOpcode, bytes.data(), uint32_t(bytes.size())) != 0;
    wxl::game::Native<luaoff::LuaPushBooleanFn>(luaoff::kLuaPushBoolean)(state, sent);
    return 1;
}
int __cdecl Reset(void*)
{
    std::lock_guard lock(mutex);
    // Advance rather than reset the sequence: old replies cannot become fresh after relog.
    if (!++requested) ++requested;
    current = {}; valid = false; ++generation;
    return 0;
}
int __cdecl Status(void* state)
{
    std::lock_guard lock(mutex);
    wxl::game::Native<luaoff::LuaPushBooleanFn>(luaoff::kLuaPushBoolean)(state, valid);
    wxl::game::Native<luaoff::LuaPushNumberFn>(luaoff::kLuaPushNumber)(state, generation);
    wxl::game::Native<luaoff::LuaPushBooleanFn>(luaoff::kLuaPushBoolean)(state, valid && current.Enabled);
    return 3;
}
int __cdecl ReadSlot(void* state)
{
    double index = wxl::game::Native<luaoff::LuaToNumberFn>(luaoff::kLuaToNumber)(state, 1);
    std::lock_guard lock(mutex);
    if (!valid || !(index >= 1 && index <= Wire::SlotCount) || index != int(index))
    {
        wxl::game::Native<luaoff::LuaPushNilFn>(luaoff::kLuaPushNil)(state);
        return 1;
    }
    auto const& slot = current.Slots[size_t(index - 1)];
    auto push = wxl::game::Native<luaoff::LuaPushNumberFn>(luaoff::kLuaPushNumber);
    push(state, slot.ItemGuid); push(state, slot.ItemId); push(state, slot.SourceKind);
    push(state, slot.SourceId); push(state, slot.DisplayId);
    return 5;
}
const char* const bootstrap =
#include "RetailTransmogEquipmentLua.inc"
;
}
bool wxl_retail_ui::InstallRetailTransmogEquipment()
{
    auto network = Network(); auto script = FrameScript();
    bool ok = network->RegisterClientOpcode(Wire::RequestOpcode, "CMSG_WXL_TRANSMOG_EQUIPMENT_REQUEST") != 0;
    ok &= network->RegisterServerOpcode(Wire::StateOpcode, "SMSG_WXL_TRANSMOG_EQUIPMENT_STATE", &OnState, nullptr) != 0;
    ok &= script->RegisterFunction("_WXL_TRANSMOG_EQUIPMENT_REQUEST", &Request) != 0;
    ok &= script->RegisterFunction("_WXL_TRANSMOG_EQUIPMENT_RESET", &Reset) != 0;
    ok &= script->RegisterFunction("_WXL_TRANSMOG_EQUIPMENT_STATUS", &Status) != 0;
    ok &= script->RegisterFunction("_WXL_TRANSMOG_EQUIPMENT_SLOT", &ReadSlot) != 0;
    ok &= script->RegisterScript("retail-transmog-equipment", bootstrap) != 0;
    return ok;
}
