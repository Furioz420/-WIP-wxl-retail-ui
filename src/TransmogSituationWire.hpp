#pragma once
#include "TransmogEquipmentWire.hpp"
#include <array>
namespace WXL::TransmogSituationWire {
using TransmogEquipmentWire::Put;using TransmogEquipmentWire::Take;
constexpr uint16_t RequestOpcode=0x54E,ResponseOpcode=0x54F;
// Locations: any, city, outdoors, dungeon, raid, battleground, arena, zone.
// Movement: any, on foot, mounted, flying, swimming. Spec: any, primary, secondary.
struct Rule {uint8_t Slot=0;uint32_t Revision=0;bool Enabled=false;uint8_t Location=0,Movement=0,Spec=0;uint32_t Zone=0;};
struct Request {uint32_t Id=0;uint8_t Op=0;Rule Value;};
struct State {uint32_t Id=0;uint8_t Status=0;std::vector<Rule> Rules;};
inline bool Valid(Rule const&r){return r.Slot>=1&&r.Slot<=25&&r.Revision!=UINT32_MAX&&r.Location<=7&&r.Movement<=4&&r.Spec<=2&&((r.Location==7)==(r.Zone!=0));}
inline void Write(std::vector<uint8_t>&d,Rule const&r){Put(d,r.Slot,1);Put(d,r.Revision,4);Put(d,r.Enabled,1);Put(d,r.Location,1);Put(d,r.Movement,1);Put(d,r.Spec,1);Put(d,r.Zone,4);}
inline bool Read(std::span<const uint8_t>d,size_t&c,Rule&r){if(c+13>d.size())return false;r.Slot=Take(d,c,1);r.Revision=Take(d,c,4);auto enabled=Take(d,c,1);r.Enabled=enabled;r.Location=Take(d,c,1);r.Movement=Take(d,c,1);r.Spec=Take(d,c,1);r.Zone=Take(d,c,4);return enabled<=1;}
inline std::vector<uint8_t> Encode(Request const&r){std::vector<uint8_t>d;Put(d,1,1);Put(d,r.Id,4);Put(d,r.Op,1);Write(d,r.Value);return d;}
inline bool Decode(std::span<const uint8_t>d,Request&r){if(d.size()!=19||d[0]!=1)return false;size_t c=1;Request v;v.Id=Take(d,c,4);v.Op=Take(d,c,1);if(!v.Id||v.Op>1||!Read(d,c,v.Value))return false;if(v.Op){if(!Valid(v.Value))return false;}else {auto const&x=v.Value;if(x.Slot||x.Revision||x.Enabled||x.Location||x.Movement||x.Spec||x.Zone)return false;}r=v;return true;}
inline std::vector<uint8_t> Encode(State const&s){std::vector<uint8_t>d;Put(d,1,1);Put(d,s.Id,4);Put(d,s.Status,1);Put(d,uint32_t(s.Rules.size()),1);for(auto const&r:s.Rules)Write(d,r);return d;}
inline bool Decode(std::span<const uint8_t>d,State&s){if(d.size()<7||d[0]!=1)return false;size_t c=1;State v;v.Id=Take(d,c,4);v.Status=Take(d,c,1);auto count=Take(d,c,1);if(!v.Id||v.Status>5||count>25||d.size()!=7+13*count)return false;uint32_t seen=0;for(uint32_t i=0;i<count;++i){Rule r;if(!Read(d,c,r)||!Valid(r)||!r.Revision||(seen&(1u<<r.Slot)))return false;seen|=1u<<r.Slot;v.Rules.push_back(r);}s=std::move(v);return true;}
struct Context {uint8_t Location=0,Spec=0;uint32_t Zone=0;bool Mounted=false,Flying=false,Swimming=false;};
inline bool Matches(Rule const&r,Context const&c){
 if(!r.Enabled)return false;
 if(r.Location==7){if(r.Zone!=c.Zone)return false;}else if(r.Location&&r.Location!=c.Location)return false;
 if(r.Spec&&r.Spec!=c.Spec+1)return false;
 if(r.Movement==1&&(c.Mounted||c.Flying||c.Swimming))return false;
 if(r.Movement==2&&!c.Mounted)return false;
 if(r.Movement==3&&!c.Flying)return false;
 if(r.Movement==4&&!c.Swimming)return false;
 return true;
}
}
