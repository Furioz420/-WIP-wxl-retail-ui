// WXL transmog equipped-state protocol v1. Keep client/server copies identical.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace WXL::TransmogEquipmentWire
{
using std::size_t;
constexpr uint16_t RequestOpcode = 0x546;
constexpr uint16_t StateOpcode = 0x547;
constexpr uint8_t Version = 1;
constexpr size_t SlotCount = 19;
struct Slot
{
    uint32_t ItemGuid = 0;
    uint32_t ItemId = 0;
    uint8_t SourceKind = 0;
    uint32_t SourceId = 0;
    uint32_t DisplayId = 0;
};
struct State
{
    uint32_t RequestId = 0;
    bool Enabled = false;
    std::array<Slot, SlotCount> Slots{};
};
inline void Put(std::vector<uint8_t>& out, uint32_t value, size_t bytes)
{
    for (size_t i = 0; i < bytes; ++i)
        out.push_back(static_cast<uint8_t>(value >> (i * 8)));
}
inline uint32_t Take(std::span<const uint8_t> data, size_t& cursor, size_t bytes)
{
    uint32_t value = 0;
    for (size_t i = 0; i < bytes; ++i)
        value |= uint32_t(data[cursor++]) << (i * 8);
    return value;
}
inline bool Canonical(Slot const& slot)
{
    if (!slot.ItemGuid || !slot.ItemId)
        return !slot.ItemGuid && !slot.ItemId && !slot.SourceKind && !slot.SourceId && !slot.DisplayId;
    switch (slot.SourceKind)
    {
        case 0: return !slot.SourceId;
        case 1: case 2: return slot.SourceId != 0;
        case 3: return !slot.SourceId && !slot.DisplayId;
        default: return false;
    }
}
inline std::vector<uint8_t> Encode(State const& state)
{
    std::vector<uint8_t> out;
    Put(out, Version, 1); Put(out, state.RequestId, 4);
    Put(out, state.Enabled ? 1 : 0, 1); Put(out, SlotCount, 1);
    for (size_t i = 0; i < SlotCount; ++i)
    {
        auto const& slot = state.Slots[i];
        Put(out, uint32_t(i), 1); Put(out, slot.ItemGuid, 4); Put(out, slot.ItemId, 4);
        Put(out, slot.SourceKind, 1); Put(out, slot.SourceId, 4); Put(out, slot.DisplayId, 4);
    }
    return out;
}
inline bool Decode(std::span<const uint8_t> data, State& output)
{
    // Parse into a temporary so corrupt packets never partially replace live state.
    if (data.size() != 7 + SlotCount * 18 || data[0] != Version || data[5] > 1 || data[6] != SlotCount)
        return false;
    size_t cursor = 1;
    State state;
    state.RequestId = Take(data, cursor, 4);
    state.Enabled = Take(data, cursor, 1) != 0;
    ++cursor;
    for (size_t i = 0; i < SlotCount; ++i)
    {
        if (Take(data, cursor, 1) != i) return false;
        auto& slot = state.Slots[i];
        slot.ItemGuid = Take(data, cursor, 4); slot.ItemId = Take(data, cursor, 4);
        slot.SourceKind = uint8_t(Take(data, cursor, 1));
        slot.SourceId = Take(data, cursor, 4); slot.DisplayId = Take(data, cursor, 4);
        if (!Canonical(slot)) return false;
    }
    output = state;
    return true;
}
}
