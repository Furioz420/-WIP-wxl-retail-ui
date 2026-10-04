#pragma once
#include "TransmogOutfitWire.hpp"
#include <set>

namespace WXL::TransmogCustomSetWire {
using TransmogEquipmentWire::Put;
using TransmogEquipmentWire::Take;
constexpr uint16_t RequestOpcode = 0x54C, ResponseOpcode = 0x54D;
constexpr uint8_t MaxSets = 50, MaxIcon = 16;
// The owner/account is always derived from the authenticated session.
enum Operation : uint8_t { List, Save, Delete, SetOutfitIcon };
enum Status : uint8_t { Ok, SchemaMissing, Conflict, Capacity, DatabaseError, Forbidden, Invalid, Disabled };
struct Request {
    uint32_t Id=0, Revision=0;
    uint8_t Op=List, Entry=0, Icon=0;
    bool Shared=false;
    std::string Name, Payload;
};
struct Row {
    uint8_t Entry=0;
    uint32_t Revision=0;
    bool Shared=false, Editable=false;
    std::string Name, Payload;
};
struct IconRow { uint8_t Slot=0, Icon=0; uint32_t Revision=0; };
struct State {
    uint32_t Id=0;
    uint8_t Status=Ok, Changed=0, Used=0;
    std::vector<Row> Rows;
    std::vector<IconRow> Icons;
};
// slot:sourceKind:sourceID:displayID; preserves render metadata for shared sets.
// Display IDs never authorize an appearance: Quote/Apply validates source IDs.
inline bool PayloadValid(std::string const& s) {
    if(s.empty() || s.size()>900) return false;
    size_t pos=0; uint32_t seen=0;
    auto take=[&](char end,uint32_t& out) {
        size_t start=pos; uint64_t n=0;
        while(pos<s.size() && s[pos]>='0' && s[pos]<='9') {
            n=n*10+unsigned(s[pos++]-'0'); if(n>UINT32_MAX) return false;
        }
        if(pos==start || pos>=s.size() || s[pos++]!=end || (pos-start>2 && s[start]=='0')) return false;
        out=uint32_t(n); return true;
    };
    constexpr uint32_t supported=(1<<1)|(1<<3)|(1<<4)|(1<<5)|(1<<6)|(1<<7)|(1<<8)|(1<<9)|(1<<10)|(1<<15)|(1<<16)|(1<<17)|(1<<18)|(1<<19);
    while(pos<s.size()) {
        uint32_t slot,kind,id,display;
        if(!take(':',slot)||!take(':',kind)||!take(':',id)||!take(';',display)||slot>19||!(supported&(1u<<slot))||(seen&(1u<<slot))) return false;
        if(!((kind==1 || kind==2) && id && display) && !(kind==3 && !id && !display && (slot<16 || slot>18))) return false;
        seen|=1u<<slot;
    }
    return true;
}
inline void String(std::vector<uint8_t>& d,std::string const& s){d.insert(d.end(),s.begin(),s.end());}
inline std::vector<uint8_t> Encode(Request const& r) {
    std::vector<uint8_t>d;Put(d,1,1);Put(d,r.Id,4);Put(d,r.Op,1);Put(d,r.Entry,1);Put(d,r.Revision,4);Put(d,r.Shared,1);Put(d,r.Icon,1);Put(d,uint32_t(r.Name.size()),1);Put(d,uint32_t(r.Payload.size()),2);String(d,r.Name);String(d,r.Payload);return d;
}
inline bool Decode(std::span<const uint8_t>d,Request& out) {
    if(d.size()<16 || d.size()>996 || d[0]!=1) return false;
    size_t c=1;Request r;r.Id=Take(d,c,4);r.Op=Take(d,c,1);r.Entry=Take(d,c,1);r.Revision=Take(d,c,4);auto shared=Take(d,c,1);r.Icon=Take(d,c,1);auto n=Take(d,c,1),p=Take(d,c,2);
    if(!r.Id || r.Op>SetOutfitIcon || shared>1 || r.Icon>MaxIcon || n>80 || p>900 || c+n+p!=d.size() || r.Revision==UINT32_MAX) return false;
    r.Shared=shared;r.Name.assign(reinterpret_cast<char const*>(d.data()+c),n);c+=n;r.Payload.assign(reinterpret_cast<char const*>(d.data()+c),p);
    if(r.Op==List) {if(r.Entry||r.Revision||shared||r.Icon||n||p)return false;}
    else if(r.Op==SetOutfitIcon) {if(!r.Entry||r.Entry>25||shared||n||p)return false;}
    else {
        if(r.Entry>MaxSets || r.Icon || (!r.Entry && r.Revision)) return false;
        if(r.Op==Save && (!TransmogOutfitWire::NameValid(r.Name)||!PayloadValid(r.Payload)))return false;
        if(r.Op==Delete && (!r.Entry||!r.Revision||shared||n||p))return false;
    }
    out=std::move(r);return true;
}
inline std::vector<uint8_t> Encode(State const& s) {
    std::vector<uint8_t>d;Put(d,1,1);Put(d,s.Id,4);Put(d,s.Status,1);Put(d,s.Changed,1);Put(d,s.Used,1);Put(d,uint32_t(s.Rows.size()),1);Put(d,uint32_t(s.Icons.size()),1);
    for(auto const&r:s.Rows){Put(d,r.Entry,1);Put(d,r.Revision,4);Put(d,r.Shared,1);Put(d,r.Editable,1);Put(d,uint32_t(r.Name.size()),1);Put(d,uint32_t(r.Payload.size()),2);String(d,r.Name);String(d,r.Payload);}
    for(auto const&r:s.Icons){Put(d,r.Slot,1);Put(d,r.Icon,1);Put(d,r.Revision,4);}return d;
}
inline bool Decode(std::span<const uint8_t>d,State& out) {
    if(d.size()<10 || d.size()>49660 || d[0]!=1)return false;
    size_t c=1;State s;s.Id=Take(d,c,4);s.Status=Take(d,c,1);s.Changed=Take(d,c,1);s.Used=Take(d,c,1);auto count=Take(d,c,1),icons=Take(d,c,1);
    if(!s.Id||s.Status>Disabled||s.Changed>MaxSets||s.Used>MaxSets||count>MaxSets||count>s.Used||icons>25)return false;
    std::set<uint8_t> seen;
    for(uint32_t i=0;i<count;++i){
        if(c+10>d.size())return false;Row r;r.Entry=Take(d,c,1);r.Revision=Take(d,c,4);auto shared=Take(d,c,1),edit=Take(d,c,1),n=Take(d,c,1),p=Take(d,c,2);
        if(!r.Entry||r.Entry>MaxSets||!seen.insert(r.Entry).second||!r.Revision||shared>1||edit>1||n>80||p>900||c+n+p>d.size())return false;
        r.Shared=shared;r.Editable=edit;r.Name.assign(reinterpret_cast<char const*>(d.data()+c),n);c+=n;r.Payload.assign(reinterpret_cast<char const*>(d.data()+c),p);c+=p;
        if(!TransmogOutfitWire::NameValid(r.Name)||!PayloadValid(r.Payload))return false;s.Rows.push_back(std::move(r));
    }
    seen.clear();
    for(uint32_t i=0;i<icons;++i){if(c+6>d.size())return false;IconRow r;r.Slot=Take(d,c,1);r.Icon=Take(d,c,1);r.Revision=Take(d,c,4);if(!r.Slot||r.Slot>25||r.Icon>MaxIcon||!r.Revision||!seen.insert(r.Slot).second)return false;s.Icons.push_back(r);}
    if(c!=d.size())return false;out=std::move(s);return true;
}
}
