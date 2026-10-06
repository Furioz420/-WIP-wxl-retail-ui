#pragma once
#include "TransmogEquipmentWire.hpp"
#include <limits>
#include <cmath>
namespace WXL::TransmogApplyWire
{
using namespace TransmogEquipmentWire;
constexpr uint16_t Request=0x548, Response=0x549;
struct Change { uint8_t Slot=0, Kind=0, OldKind=0; uint32_t Guid=0, Source=0, OldSource=0; };
struct Batch { uint32_t Id=0, Money=0, Token=0, Amount=0; uint8_t Apply=0; std::vector<Change> Changes; };
struct Result { uint32_t Id=0, Money=0, Token=0, Amount=0; uint8_t Status=0; };
inline bool EditorSlot(uint32_t slot) { return slot==0 || (slot>=2 && slot<=9) || (slot>=14 && slot<=18); }
inline bool DecodeBatch(std::span<const uint8_t> data,Batch& output)
{
    if(data.size()<19 || data[0]!=1 || data[5]>1 || !data[18] || data[18]>19 || data.size()!=19+data[18]*15u) return false;
    Batch b; size_t c=1; b.Id=Take(data,c,4); b.Apply=Take(data,c,1);
    b.Money=Take(data,c,4); b.Token=Take(data,c,4); b.Amount=Take(data,c,4); c++;
    if(!b.Id) return false;
    uint32_t seen=0;
    while(c<data.size()) {
        Change x; x.Slot=Take(data,c,1); x.Guid=Take(data,c,4); x.Kind=Take(data,c,1);
        x.Source=Take(data,c,4); x.OldKind=Take(data,c,1); x.OldSource=Take(data,c,4);
        if(!EditorSlot(x.Slot) || !x.Guid || (seen&(1u<<x.Slot)) || x.Kind>3 || ((x.Kind==0 || x.Kind==3)!=(x.Source==0)) ||
            x.OldKind>3 || ((x.OldKind==0 || x.OldKind==3)!=(x.OldSource==0))) return false;
        seen|=1u<<x.Slot; b.Changes.push_back(x);
    }
    output=std::move(b); return true;
}
inline std::vector<uint8_t> EncodeBatch(Batch const& b)
{
    std::vector<uint8_t> d; Put(d,1,1); Put(d,b.Id,4); Put(d,b.Apply,1);
    Put(d,b.Money,4); Put(d,b.Token,4); Put(d,b.Amount,4); Put(d,uint32_t(b.Changes.size()),1);
    for(auto const& x:b.Changes) { Put(d,x.Slot,1);Put(d,x.Guid,4);Put(d,x.Kind,1);Put(d,x.Source,4);Put(d,x.OldKind,1);Put(d,x.OldSource,4); }
    return d;
}
inline std::vector<uint8_t> EncodeResult(Result const& r)
{
    std::vector<uint8_t>d;Put(d,1,1);Put(d,r.Id,4);Put(d,r.Status,1);Put(d,r.Money,4);Put(d,r.Token,4);Put(d,r.Amount,4);return d;
}
inline bool DecodeResult(std::span<const uint8_t>d,Result& r)
{
    if(d.size()!=18 || d[0]!=1 || d[5]>11) return false;
    size_t c=1;Result x;x.Id=Take(d,c,4);x.Status=Take(d,c,1);x.Money=Take(d,c,4);x.Token=Take(d,c,4);x.Amount=Take(d,c,4);
    if(!x.Id) return false;r=x;return true;
}
inline bool Price(uint32_t base,double scale,int32_t copper,uint32_t& out)
{
    double price=std::trunc(base*scale)+copper;
    if(!std::isfinite(scale)||scale<0||!std::isfinite(price)||price<0||price>INT32_MAX)return false;
    out=uint32_t(price);return true;
}
}
