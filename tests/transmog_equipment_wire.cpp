#include "../src/TransmogEquipmentWire.hpp"
#include <cassert>
#include <iostream>
int main()
{
    namespace W = WXL::TransmogEquipmentWire;
    W::State input;
    input.RequestId = 0x12345678; input.Enabled = true;
    input.Slots[0] = {100, 82423, 1, 82423, 108655};
    input.Slots[2] = {101, 200, 2, 41833, 108655};
    input.Slots[4] = {102, 201, 3, 0, 0};
    input.Slots[5] = {103, 202, 0, 0, 999};
    auto bytes = W::Encode(input);
    assert(bytes.size() == 349);
    assert(bytes[1] == 0x78 && bytes[4] == 0x12);
    W::State output;
    assert(W::Decode(bytes, output));
    assert(output.Slots[0].SourceId == 82423 && output.Slots[2].SourceKind == 2);
    assert(output.Slots[4].SourceKind == 3 && !output.Slots[1].ItemId);
    for (size_t length = 0; length < bytes.size(); ++length)
    {
        output.RequestId = 77;
        assert(!W::Decode(std::span(bytes).first(length), output));
        assert(output.RequestId == 77);
    }
    auto reject = [&](size_t offset, uint8_t value) {
        auto corrupt = bytes; corrupt[offset] = value;
        output.RequestId = 77;
        assert(!W::Decode(corrupt, output));
        assert(output.RequestId == 77);
    };
    reject(0, 2); reject(5, 2); reject(6, 18); reject(7, 1);
    reject(7+9, 4); // unknown identity kind
    reject(7+18+10, 1); // empty slot with a source ID
    reject(7+4*18+14, 1); // hidden appearance with nonzero display
    bytes.push_back(0); assert(!W::Decode(bytes, output));
    std::cout << "equipment wire: roundtrip, all truncations, canonical identities, corruption passed\n";
}
