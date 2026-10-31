#include "environment_curve.h"
#include <cassert>
#include <iostream>

int main() {
    envhistory::History h;
    constexpr uint32_t begin=5900000;
    for(uint32_t i=0;i<288;++i) {
        envhistory::Record r;r.slot=begin+i;r.valid=3;
        r.values[0]=i<100?0:70;r.values[1]=42;h.restore(r);
    }
    auto mean=[&](uint32_t i,int axis=0) {
        float v=0;assert(envcurve::average(h,begin+i,axis,begin,begin+288,v));return v;
    };
    // A step is spread across seven points without changing stored data.
    for(unsigned i=96;i<=103;++i)assert(mean(i)==float(i-96)*10);
    assert(h.get(begin+99)->values[0]==0);
    assert(h.get(begin+100)->values[0]==70);
    assert(mean(0)==0 && mean(287)==70);
    assert(mean(100,1)==42);
    for(int width:{233,265}) {
        int oldX=-1,count=0;
        envcurve::columns(h,0,begin,begin+288,43,width,[&](int x,float v,bool connect) {
            assert(connect==(count>0));
            if(connect)assert(x==oldX+1);
            assert(v>=0 && v<=70);
            oldX=x;++count;
        });
        assert(count>230 && count<=width+1);
    }
    envhistory::Record gap;gap.slot=begin+100;h.restore(gap);
    assert(mean(99)==0 && mean(101)==70); // No averaging across the gap.
    float v;
    assert(!envcurve::average(h,begin+100,0,begin,begin+288,v));
    assert(!envcurve::average(h,begin,2,begin,begin+288,v));
    int paths=0;
    envcurve::columns(h,0,begin,begin+288,43,233,[&](int,float,bool connect) {
        if(!connect)++paths;
    });
    assert(paths==2);
    // Display boundaries, a short run and a single point use available values.
    assert(envcurve::average(h,begin+99,0,begin+99,begin+100,v) && v==0);
    envhistory::History shortRun;
    for(unsigned i=0;i<2;++i) {
        envhistory::Record r;r.slot=10+i;r.valid=1;r.values[0]=10+20*i;
        shortRun.restore(r);
    }
    assert(envcurve::average(shortRun,10,0,10,12,v) && v==20);
    assert(envcurve::average(shortRun,11,0,10,12,v) && v==20);
    int points=0;
    envcurve::columns(shortRun,0,10,11,43,233,[&](int,float value,bool connect) {
        assert(!connect && value==10);++points;
    });
    assert(points==1);
    std::cout<<"Environment moving average tests passed\n";
}
