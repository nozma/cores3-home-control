#include "auto_model.h"
#include <cassert>
#include <iostream>
using namespace automation;
using namespace light;
int main() {
    int64_t cursor=-1;
    assert(!due(-1,cursor,true,true) && cursor==-1);
    assert(!due(100,cursor,false,true) && cursor==100); // boot skips catch-up
    assert(!due(100,cursor,false,true));
    assert(due(101,cursor,false,true));
    assert(!due(99,cursor,false,true) && cursor==101);
    assert(!due(101,cursor,false,true));
    assert(!due(104,cursor,false,true)); // missed event
    assert(due(104,cursor,true,false)); // enabling applies immediately
    assert(!due(105,cursor,false,false));
    Settings s;
    assert(valid(s) && !s.enabled);
    for(int wake=300;wake<=540;wake+=15) for(int work=900;work<=1140;work+=15)
        for(int bed=1260;bed<=1440;bed+=15) {
            Settings t{true,wake,work,bed}; assert(valid(t));
            assert(phase(t,wake)==Phase::Dawn);
            assert(phase(t,wake+60)==Phase::Day);
            assert(phase(t,bed-120)==Phase::Late);
            assert(phase(t,bed%1440)==Phase::Night);
            assert(phase(t,(bed+15)%1440)==Phase::Off);
            assert(event(t,bed%1440,1,2,0));
        }
    s.wake=299; assert(!valid(s)); s.wake=541; assert(!valid(s)); s.wake=391; assert(!valid(s)); s=Settings{};
    s.work=899; assert(!valid(s)); s.work=1141; assert(!valid(s)); s=Settings{};
    s.bed=1259; assert(!valid(s)); s.bed=1441; assert(!valid(s)); s=Settings{};
    assert(phase(s,389)==Phase::Off && phase(s,1020)==Phase::Evening);
    assert(phase(s,1260)==Phase::Late && phase(s,1380)==Phase::Night && phase(s,1395)==Phase::Off);
    s.work=1140;s.bed=1260;assert(phase(s,1140)==Phase::Late);
    s.bed=1440; assert(phase(s,0)==Phase::Night && phase(s,14)==Phase::Night && phase(s,15)==Phase::Off);
    for(int start=0;start<=5;++start) {
        int changes=0, last=start;
        for(int e=0;e<=60;++e) { int v=ramp(start,5,e); assert(v>=last && v<=5); if(v!=last) ++changes; last=v; }
        assert(changes==5-start && last==5);
    }
    s=Settings{};
    assert(ramp(1,4,19)==1 && ramp(1,4,20)==2 && ramp(1,4,40)==3);
    assert(!event(s,s.wake+1,1,2,0)); assert(event(s,s.wake+12,1,2,0));
    State off; off.power=Power::Off;
    auto morning=plan(Id::Living,off,Power::On,2,0,false,true,2,0);
    assert(morning.valid && morning.count==1 && morning.signals[0]==Signal::Normal);
    assert(morning.syncLevel==2 && morning.syncColor==0);
    State on{Power::On,2,0};
    auto next=plan(Id::Living,on,Power::On,2,1,false,true,2,0);
    assert(next.count==1 && next.signals[0]==Signal::Cool);
    auto same=plan(Id::Living,on,Power::On,2,0,false,true,2,0); assert(same.count==0);
    auto full=plan(Id::Living,on,Power::On,5,5,true,false,2,0);
    assert(full.count==1 && full.signals[0]==Signal::Full);
    applyLiving(on,Signal::Full); assert(on.brightness==5 && on.color==5);
    auto night=plan(Id::Living,on,Power::Night,-1,-1,false,false,2,0);
    assert(night.count==1 && night.signals[0]==Signal::Night);
    auto unknown=plan(Id::Kitchen,State{},Power::On,1,-1,false,false,2,0); assert(!unknown.valid);
    for (int bed=1260;bed<=1440;bed+=15) {
        Settings t; t.bed=bed;
        for (auto id : {Id::Kitchen,Id::Living}) {
            int start=id==Id::Kitchen?1:2, last=start, changes=0;
            for(int e=0;e<120;++e) {
                int m=bed-120+e, level=bedtimeLevel(t,m,id);
                assert(level>=0 && level<=last);
                if(level!=last) { ++changes; assert(event(t,m,1,2,0)); }
                if(e>=105) assert(level==0);
                last=level;
            }
            assert(changes==start);
        }
        assert(bedtimeLevel(t,bed-120,Id::Kitchen)==1);
        assert(bedtimeLevel(t,bed-68,Id::Living)==2);
        assert(bedtimeLevel(t,bed-67,Id::Living)==1);
        assert(!kitchenBedtimeNight(t,bed-16));
        assert(kitchenBedtimeNight(t,bed-15));
        assert(kitchenBedtimeNight(t,bed-1));
        assert(!kitchenBedtimeNight(t,bed));
        assert(event(t,bed-15,1,2,0));
        assert(!event(t,bed-14,1,2,0));
    }
    // First use without a saved living snapshot calibrates once, then uses differences.
    auto initial=plan(Id::Living,off,Power::On,2,0,false,false,2,0);
    assert(initial.count==13 && initial.signals[0]==Signal::Normal);
    for (int actualB=0;actualB<=5;++actualB) for (int actualC=0;actualC<=5;++actualC)
        for (int targetB=0;targetB<=5;++targetB) for(int targetC=0;targetC<=5;++targetC) {
            State physical; physical.power=Power::On; physical.brightness=actualB; physical.color=actualC;
            auto adjustment=plan(Id::Living,physical,Power::On,targetB,targetC,false,false,2,0);
            assert(adjustment.count<=10);
            for(int i=0;i<adjustment.count;++i) applyLiving(physical,adjustment.signals[i]);
            assert(physical.brightness==targetB && physical.color==targetC);
        }
    std::cout << "automatic lighting model tests passed\n";
}
