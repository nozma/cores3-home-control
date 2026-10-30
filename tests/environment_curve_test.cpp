#include "environment_curve.h"
#include <cassert>
#include <iostream>

int main() {
    // Rising, falling, flat, and turning sections must stay within their samples.
    for(const auto& values : {std::initializer_list<float>{20,21,24,24,19,22},
                             {0,100,0,100,0,0}, {100,40,20,10,5,0}}) {
        envhistory::History h;uint32_t slot=100;
        for(float v:values) {
            envhistory::Record r;r.slot=slot++;r.valid=3;
            r.values[0]=v;r.values[1]=100-v;h.restore(r);
        }
        for(int axis=0;axis<2;++axis)for(uint32_t s=100;s+1<slot;++s) {
            envcurve::Segment c;
            assert(envcurve::segment(h,s,axis,100,slot,c));
            assert(c.at(0)==c.first && c.at(1)==c.last);
            float prev=c.first;
            for(int i=1;i<=100;++i) {
                float v=c.at(i/100.f);
                assert(v>=std::min(c.first,c.last) && v<=std::max(c.first,c.last));
                assert(c.first<=c.last ? v>=prev-0.0001f : v<=prev+0.0001f);
                prev=v;
            }
            if(s+2<slot) {
                envcurve::Segment next;assert(envcurve::segment(h,s+1,axis,100,slot,next));
                assert(std::fabs(c.endSlope-next.startSlope)<0.0001f);
            }
        }
    }
    envhistory::History h;
    for(uint32_t s:{100u,101u,103u}) {
        envhistory::Record r;r.slot=s;r.valid=1;r.values[0]=float(s);h.restore(r);
    }
    envcurve::Segment c;
    assert(envcurve::segment(h,100,0,100,104,c));
    assert(c.at(.5f)==100.5f); // Two isolated consecutive points are a straight line.
    assert(!envcurve::segment(h,101,0,100,104,c)); // Gap stays open.
    assert(!envcurve::segment(h,102,0,100,104,c));
    assert(!envcurve::segment(h,103,0,100,104,c)); // Isolated point.
    assert(!envcurve::segment(h,100,1,100,104,c)); // Missing axis.
    assert(!envcurve::segment(h,100,0,100,101,c)); // Display boundary.
    std::cout<<"Environment curve tests passed\n";
}
