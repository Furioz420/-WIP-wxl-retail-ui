#pragma once
#include <cstdint>
namespace WXL {
// A source being an exact Retail variant does not imply its display is DB2-only.
template<class NativeLookup,class RetailLookup>
bool PreviewDisplayReady(std::uint32_t display, NativeLookup native, RetailLookup retail) {
    return display && (native(display) || retail(display));
}
}
