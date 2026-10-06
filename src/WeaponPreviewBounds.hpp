#pragma once
#include <cmath>
#include <cstring>
#include <cstddef>
#include <cstdint>
namespace WXL {
inline bool WeaponPreviewBounds(const uint8_t* bytes,size_t size,float* output,float* projected=nullptr) {
    size_t offset=0;
    if(size>=8 && std::memcmp(bytes,"MD21",4)==0)offset=8;
    if(size<offset+188 || std::memcmp(bytes+offset,"MD20",4)!=0)return false;
    float box[6];std::memcpy(box,bytes+offset+160,sizeof(box));
    double squared=0;
    for(int i=0;i<3;++i) {
        if(!std::isfinite(box[i])||!std::isfinite(box[i+3])||box[i]>box[i+3]||std::abs(box[i])>10000||std::abs(box[i+3])>10000)return false;
        output[i]=(box[i]+box[i+3])*.5f;
        const double half=(box[i+3]-box[i])*.5;squared+=half*half;
    }
    output[3]=static_cast<float>(std::sqrt(squared));
    if(projected) {
        const float x=(box[3]-box[0])*.5f,y=(box[4]-box[1])*.5f;
        projected[0]=std::abs(std::cos(.65f))*x+std::abs(std::sin(.65f))*y;
        projected[1]=std::abs(std::sin(.65f))*x+std::abs(std::cos(.65f))*y;
    }
    return output[3]>.001f && output[3]<10000;
}
}
