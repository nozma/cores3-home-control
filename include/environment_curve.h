#pragma once
#include "environment_history.h"

namespace envcurve {
inline bool value(const envhistory::History& h,uint32_t slot,int axis,float& out) {
    const auto* r=h.get(slot);
    if(!r || !(r->valid&(1<<axis)))return false;
    out=r->values[axis];return true;
}
// Centred seven-point mean, truncated at display edges and missing records.
// Only rendering uses this value; stored samples remain unchanged.
inline bool average(const envhistory::History& h,uint32_t slot,int axis,
                    uint32_t begin,uint32_t end,float& out) {
    if(slot<begin || slot>=end || !value(h,slot,axis,out))return false;
    float sum=out;unsigned count=1;
    for(int direction:{-1,1})for(unsigned distance=1;distance<=3;++distance) {
        if(direction<0 && slot-begin<distance)break;
        if(direction>0 && end-1-slot<distance)break;
        float v;
        uint32_t neighbour=direction<0?slot-distance:slot+distance;
        if(!value(h,neighbour,axis,v))break;
        sum+=v;++count;
    }
    out=sum/count;return true;
}

// Sample once per screen column, in time coordinates before any pixel rounding.
// Start a new path for every contiguous run, even if a gap fits inside one pixel.
template<class Plot>
void columns(const envhistory::History& h,int axis,uint32_t begin,uint32_t end,
             int left,int width,Plot plot) {
    const float scale=float(width)/envhistory::kCapacity;
    uint32_t slot=begin;
    float unused;
    while(slot<end) {
        if(!value(h,slot,axis,unused)){++slot;continue;}
        const uint32_t first=slot;
        while(slot+1<end && value(h,slot+1,axis,unused))++slot;
        const uint32_t last=slot++;
        const int firstX=left+int(std::lround((first-begin)*scale));
        const int lastX=left+int(std::lround((last-begin)*scale));
        for(int x=firstX;x<=lastX;++x) {
            // Use offsets to avoid losing fractional precision in epoch slots.
            float offset=std::max(float(first-begin),
                std::min(float(last-begin),float(x-left)/scale));
            uint32_t s=begin+uint32_t(std::floor(offset));
            float v=0;
            average(h,s,axis,first,last+1,v);
            if(s<last) {
                float next=0;average(h,s+1,axis,first,last+1,next);
                v+=(next-v)*(offset-float(s-begin));
            }
            plot(x,v,x!=firstX);
        }
    }
}
}
