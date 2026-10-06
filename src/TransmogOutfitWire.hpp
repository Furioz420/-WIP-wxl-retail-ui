#pragma once
#include "TransmogEquipmentWire.hpp"
#include <string>
#include <array>
namespace WXL::TransmogOutfitWire {
using namespace TransmogEquipmentWire;
constexpr uint16_t RequestOpcode=0x54A,ResponseOpcode=0x54B;
constexpr std::array<uint32_t,25> Prices={10000,15000,20000,30000,40000,50000,65000,80000,100000,125000,150000,180000,220000,260000,300000,350000,400000,450000,500000,550000,625000,700000,800000,900000,1000000};
struct Request {uint32_t Id=0,Revision=0;uint8_t Op=0,Slot=0,Owned=0;std::string Name,Payload;};
struct Row {uint8_t Slot=0;uint32_t Revision=0;bool Deleted=false;std::string Name,Payload;};
struct State {uint32_t Id=0,Price=0;uint8_t Status=0,Owned=0;std::vector<Row> Rows;};
inline bool NameValid(std::string const& s) {if(s.empty()||s.size()>80)return false;bool visible=false;for(unsigned char c:s){if(c<32||c==127||c=='|')return false;if(c!=' ')visible=true;}return visible;}
inline bool PayloadValid(std::string const& s) {
 if(s.empty()||s.size()>600)return false;
 uint32_t seen=0;size_t pos=0;
 auto take=[&](char end,uint32_t& out) {size_t start=pos;uint64_t n=0;while(pos<s.size()&&s[pos]>='0'&&s[pos]<='9'){n=n*10+unsigned(s[pos++]-'0');if(n>UINT32_MAX)return false;}if(pos==start||pos>=s.size()||s[pos++]!=end||(pos-start>2&&s[start]=='0'))return false;out=uint32_t(n);return true;};
 while(pos<s.size()){uint32_t slot,kind,id;if(!take(':',slot)||!take(':',kind)||!take(';',id)||!slot||slot>19||kind>3||((kind==0||kind==3)!=(id==0))||(seen&(1u<<slot)))return false;seen|=1u<<slot;}return true;
}
inline void String(std::vector<uint8_t>& d,std::string const& s){d.insert(d.end(),s.begin(),s.end());}
inline std::vector<uint8_t> Encode(Request const& r){std::vector<uint8_t>d;Put(d,1,1);Put(d,r.Id,4);Put(d,r.Op,1);Put(d,r.Slot,1);Put(d,r.Revision,4);Put(d,r.Owned,1);Put(d,uint32_t(r.Name.size()),1);Put(d,uint32_t(r.Payload.size()),2);String(d,r.Name);String(d,r.Payload);return d;}
inline bool Decode(std::span<const uint8_t>d,Request& r) {
 if(d.size()<15||d.size()>695||d[0]!=1)return false;size_t c=1;Request v;v.Id=Take(d,c,4);v.Op=Take(d,c,1);v.Slot=Take(d,c,1);v.Revision=Take(d,c,4);v.Owned=Take(d,c,1);auto n=Take(d,c,1),p=Take(d,c,2);
 if(!v.Id||v.Op>3||n>80||p>600||c+n+p!=d.size())return false;
 v.Name.assign(reinterpret_cast<char const*>(d.data()+c),n);c+=n;v.Payload.assign(reinterpret_cast<char const*>(d.data()+c),p);
 if(v.Op==1||v.Op==2){if(!v.Slot||v.Slot>25||v.Revision==UINT32_MAX||v.Owned)return false;if(v.Op==1&&(!NameValid(v.Name)||!PayloadValid(v.Payload)))return false;if(v.Op==2&&(n||p))return false;}
 else if(v.Slot||v.Revision||n||p||(v.Op==0&&v.Owned)||v.Owned>24)return false;
 r=std::move(v);return true;
}
inline std::vector<uint8_t> Encode(State const& s){std::vector<uint8_t>d;Put(d,1,1);Put(d,s.Id,4);Put(d,s.Status,1);Put(d,s.Owned,1);Put(d,s.Price,4);Put(d,uint32_t(s.Rows.size()),1);for(auto const&r:s.Rows){Put(d,r.Slot,1);Put(d,r.Revision,4);Put(d,r.Deleted,1);Put(d,uint32_t(r.Name.size()),1);Put(d,uint32_t(r.Payload.size()),2);String(d,r.Name);String(d,r.Payload);}return d;}
inline bool Decode(std::span<const uint8_t>d,State& s){
 if(d.size()<12||d.size()>17500||d[0]!=1)return false;size_t c=1;State v;v.Id=Take(d,c,4);v.Status=Take(d,c,1);v.Owned=Take(d,c,1);v.Price=Take(d,c,4);auto count=Take(d,c,1);if(!v.Id||v.Status>7||v.Owned>25||count>25)return false;uint32_t seen=0;
 for(uint32_t i=0;i<count;++i){if(c+9>d.size())return false;Row r;r.Slot=Take(d,c,1);r.Revision=Take(d,c,4);auto deleted=Take(d,c,1),n=Take(d,c,1),p=Take(d,c,2);if(!r.Slot||r.Slot>25||(seen&(1u<<r.Slot))||deleted>1||n>80||p>600||c+n+p>d.size())return false;seen|=1u<<r.Slot;r.Deleted=deleted;r.Name.assign(reinterpret_cast<char const*>(d.data()+c),n);c+=n;r.Payload.assign(reinterpret_cast<char const*>(d.data()+c),p);c+=p;if(r.Name.find('\0')!=std::string::npos||r.Payload.find('\0')!=std::string::npos)return false;v.Rows.push_back(std::move(r));}
 if(c!=d.size())return false;s=std::move(v);return true;
}
}
