#include "TransmogApplyWire.hpp"
#include <cassert>
int main(){namespace W=WXL::TransmogApplyWire;
 W::Batch b;b.Id=1;b.Changes={{4,1,0,123,82423,0},{2,0,1,456,0,12345}};
 auto d=W::EncodeBatch(b);W::Batch out;assert(W::DecodeBatch(d,out)&&out.Changes.size()==2);
 for(size_t n=0;n<d.size();++n)assert(!W::DecodeBatch({d.data(),n},out));
 auto bad=d;bad.push_back(0);assert(!W::DecodeBatch(bad,out));
 bad=d;bad[34]=4;assert(!W::DecodeBatch(bad,out)); // duplicate slot
 bad=d;bad[24]=2;assert(!W::DecodeBatch(bad,out)); // unsupported exact selection
 bad=d;bad[19]=10;assert(!W::DecodeBatch(bad,out)); // nonappearance slot
 W::Result r;r.Id=1;auto result=W::EncodeResult(r);W::Result decoded;
 assert(W::DecodeResult(result,decoded));for(size_t n=0;n<result.size();++n)assert(!W::DecodeResult({result.data(),n},decoded));
 uint32_t price;assert(W::Price(100,.5,10,price)&&price==60);
 assert(!W::Price(100,-1,0,price));assert(!W::Price(100,1,-101,price));assert(!W::Price(UINT32_MAX,2,0,price));
 assert(!W::Price(100,std::numeric_limits<double>::infinity(),0,price));
}
