#include "environment_history.h"
#include <cassert>
#include <iostream>
using namespace envhistory;
int main() {
    constexpr uint32_t base=1800000000; // Aligned to both 5 and 30 minutes.
    const uint32_t slot=base/300;
    History h;
    const float values[]={24,44,178};const bool all[]={true,true,true,true};
    h.observe(base+3,values,all,base+1,true);
    assert(!h.get(slot));
    h.advance(base+300);
    assert(h.get(slot)->motion==2 && h.get(slot)->valid==7);
    assert(h.lastMotion==base+1);
    for(unsigned m=0;m<5;++m)h.observe(base+300+m*60+3,values,all,base+1,true);
    h.advance(base+600);
    assert(h.get(slot+1)->motion==1);
    h.observe(base+603,values,all,base+599,true); // Late observation changes prior bin.
    assert(h.get(slot+1)->motion==2);
    h.advance(base+900);
    assert(h.get(slot+2)->motion==0); // Partial polling must not imply no detection.
    h.advance(base+1800);
    assert(h.get(slot+3)->valid==0 && h.get(slot+5)->motion==0);
    auto b=block(h,base/1800);
    assert(validBlock(b));
    History restored;
    for(const auto& r:b.rows)restored.restore(r);
    assert(restored.get(slot)->values[0]==24 && restored.get(slot+1)->motion==2);
    b.rows[0].values[0]=99;
    assert(!validBlock(b));
    h.observe(base-300,values,all,base-300,true);
    assert(!h.get(slot-1));
    h.advance(base+300*(kCapacity+20));
    assert(!h.get(slot));
    assert(h.get(slot+kCapacity+19)->valid==0);
    Record invalid;invalid.slot=slot;invalid.motion=3;
    restored.restore(invalid);assert(restored.get(slot)->motion==2);
    auto next=block(restored,base/1800+1);assert(validBlock(next));
    std::cout << "environment_history tests passed\n";
}
