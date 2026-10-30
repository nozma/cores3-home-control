#pragma once
#include "environment_history.h"

namespace envcurve {
// PCHIP slopes for equally spaced, consecutive five-minute records.
inline float interior(float a,float b) {
    if(a==0 || b==0 || (a>0)!=(b>0))return 0;
    return 2*a*b/(a+b);
}
inline float endpoint(float a,float b) {
    float m=(3*a-b)/2;
    if(a==0 || (m>0)!=(a>0))return 0;
    if((a>0)!=(b>0) && std::fabs(m)>3*std::fabs(a))return 3*a;
    return m;
}
struct Segment {
    float first,last,startSlope,endSlope;
    float at(float t) const {
        float t2=t*t,t3=t2*t;
        float v=(2*t3-3*t2+1)*first+(t3-2*t2+t)*startSlope
            +(-2*t3+3*t2)*last+(t3-t2)*endSlope;
        return std::max(std::min(first,last),std::min(std::max(first,last),v));
    }
};
inline bool value(const envhistory::History& h,uint32_t slot,int axis,float& out) {
    const auto* r=h.get(slot);
    if(!r || !(r->valid&(1<<axis)))return false;
    out=r->values[axis];return true;
}
// Never use neighbours across missing data or outside the displayed interval.
inline bool segment(const envhistory::History& h,uint32_t slot,int axis,
                    uint32_t begin,uint32_t end,Segment& out) {
    if(slot<begin || slot+1>=end || !value(h,slot,axis,out.first)
       || !value(h,slot+1,axis,out.last))return false;
    float before=0,after=0;
    bool hasBefore=slot>begin && value(h,slot-1,axis,before);
    bool hasAfter=slot+2<end && value(h,slot+2,axis,after);
    float delta=out.last-out.first;
    out.startSlope=hasBefore?interior(out.first-before,delta)
        :hasAfter?endpoint(delta,after-out.last):delta;
    out.endSlope=hasAfter?interior(delta,after-out.last)
        :hasBefore?endpoint(delta,out.first-before):delta;
    return true;
}
}
