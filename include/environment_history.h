#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>

namespace envhistory {
constexpr uint32_t kInterval = 300, kCapacity = 288, kBlockSize = 6;
struct Record {
    uint32_t slot = 0;
    float values[3]{};
    uint8_t valid = 0;
    uint8_t motion = 0; // 0: incomplete observations, 1: no recorded detection, 2: detected.
    uint8_t minutes = 0;
    uint8_t reserved = 0;
};
static_assert(sizeof(Record) == 20, "Stable history layout");
inline bool validRecord(const Record& r) {
    if (r.valid > 7 || r.motion > 2 || r.minutes > 31 || r.reserved) return false;
    for (int i=0;i<3;++i) if ((r.valid & (1<<i)) && !std::isfinite(r.values[i])) return false;
    return true;
}
class History {
    Record records_[kCapacity]{};
    Record current_{};
public:
    uint32_t lastMotion = 0;
    const Record* get(uint32_t slot) const {
        const auto& r=records_[slot%kCapacity];
        return slot && r.slot==slot ? &r : nullptr;
    }
    void restore(const Record& r) {
        auto& old=records_[r.slot%kCapacity];
        if (r.slot && r.slot>=old.slot && validRecord(r)) old=r;
    }
    bool advance(uint32_t now) {
        const uint32_t slot=now/kInterval;
        if (!current_.slot) { current_.slot=slot; return false; }
        if (slot<=current_.slot) return false;
        if (current_.motion!=2) current_.motion=current_.minutes==31 ? 1 : 0;
        restore(current_);
        for (uint32_t s=std::max(current_.slot+1,slot>kCapacity?slot-kCapacity:1u);s<slot;++s) {
            Record gap; gap.slot=s; restore(gap);
        }
        current_=Record{}; current_.slot=slot;
        return true;
    }
    void observe(uint32_t now,const float* values,const bool* available,uint32_t motionTime,bool motionAvailable) {
        advance(now);
        if (now/kInterval!=current_.slot) return; // Ignore clock rollback.
        for (int i=0;i<3;++i) if (available[i] && std::isfinite(values[i])) {
            current_.values[i]=values[i]; current_.valid|=1<<i;
        }
        if (motionAvailable) current_.minutes|=1<<((now%kInterval)/60);
        if (motionTime && motionTime<=now) {
            lastMotion=std::max(lastMotion,motionTime);
            const uint32_t eventSlot=motionTime/kInterval;
            if (eventSlot==current_.slot) current_.motion=2;
            else if (eventSlot<current_.slot && current_.slot-eventSlot<=kCapacity) {
                auto& r=records_[eventSlot%kCapacity];
                if (r.slot<=eventSlot) { if(r.slot!=eventSlot) {r=Record{};r.slot=eventSlot;} r.motion=2; }
            }
        }
    }
};
struct Block {
    uint32_t magic = 0x454e5631;
    uint32_t number = 0;
    uint32_t lastMotion = 0;
    Record rows[kBlockSize]{};
    uint32_t checksum = 0;
};
static_assert(sizeof(Block)==136, "Stable block layout");
inline uint32_t checksum(const Block& b) {
    uint32_t h=2166136261u;
    auto p=reinterpret_cast<const uint8_t*>(&b);
    for(size_t i=0;i<offsetof(Block,checksum);++i) h=(h^p[i])*16777619u;
    return h;
}
inline bool validBlock(const Block& b) {
    if(b.magic!=0x454e5631 || b.checksum!=checksum(b) || !b.number) return false;
    for(unsigned i=0;i<kBlockSize;++i)
        if(b.rows[i].slot!=b.number*kBlockSize+i || !validRecord(b.rows[i])) return false;
    return true;
}
inline Block block(const History& h,uint32_t number) {
    Block b; b.number=number;b.lastMotion=h.lastMotion;
    for(unsigned i=0;i<kBlockSize;++i) {
        uint32_t slot=number*kBlockSize+i;
        const auto* r=h.get(slot);
        if(r)b.rows[i]=*r;else b.rows[i].slot=slot;
    }
    b.checksum=checksum(b);return b;
}
}
