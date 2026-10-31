#include "environment_control.h"
#include "environment_curve.h"
#include <LittleFS.h>
#include <esp_partition.h>
#include <atomic>
#include <memory>
#include <ctime>

namespace {
envhistory::History history;
QueueHandle_t saves=nullptr;
std::atomic<bool> storageReady{false},storageFailed{false};
uint32_t saveBlock=0;
uint32_t utcStamp(const char* stamp) {
    tm parsed{};
    if(!strptime(stamp,"%Y-%m-%dT%H:%M:%SZ",&parsed))return 0;
    // main.cpp configures the system timezone as UTC.
    auto t=mktime(&parsed);return t>=1704067200 ? uint32_t(t) : 0;
}
bool erasedPartition() {
    const auto* p=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,"spiffs");
    if(!p)return false;
    std::unique_ptr<uint8_t[]> bytes(new(std::nothrow) uint8_t[4096]);
    if(!bytes)return false;
    for(size_t offset=0;offset<p->size;offset+=4096) {
        size_t n=std::min(size_t(4096),p->size-offset);
        if(esp_partition_read(p,offset,bytes.get(),n)!=ESP_OK)return false;
        for(size_t i=0;i<n;++i)if(bytes[i]!=0xff)return false;
    }
    return true;
}
void path(char* out,unsigned slot,unsigned bank) {snprintf(out,24,"/env-%02u-%u.bin",slot,bank);}
}
void beginEnvironmentHistory() {
    // Do not change the partition table or format existing/unrecognised data.
    bool mounted=LittleFS.begin(false,"/env-history",4,"spiffs");
    if(!mounted && erasedPartition() && LittleFS.format())
        mounted=LittleFS.begin(false,"/env-history",4,"spiffs");
    storageReady=mounted;
    unsigned restored=0;
    if(mounted)for(unsigned slot=0;slot<48;++slot)for(unsigned bank=0;bank<2;++bank) {
        char file[24];path(file,slot,bank);
        if(!LittleFS.exists(file))continue;
        File f=LittleFS.open(file,"r");
        if(!f)continue;
        envhistory::Block b;
        bool ok=f.size()==sizeof(b)&&f.read(reinterpret_cast<uint8_t*>(&b),sizeof(b))==sizeof(b);
        f.close();
        if(!ok||!envhistory::validBlock(b)||b.number%48!=slot||(b.number/48)%2!=bank)continue;
        for(const auto& r:b.rows)history.restore(r);
        history.lastMotion=std::max(history.lastMotion,b.lastMotion);
        ++restored;
    }
    saves=xQueueCreate(1,sizeof(envhistory::Block));
    if(!saves)storageReady=false;
    Serial.printf("ENV_HISTORY_READY: storage=%d restored_blocks=%u\n",int(storageReady.load()),restored);
}
void acceptEnvironmentSample(const EnvironmentSample& sample,uint32_t now) {
    if(now<1704067200 || sample.code!=200)return;
    const uint32_t motion=sample.available[3]&&sample.values[3]>0?utcStamp(sample.timestamps[3]):0;
    history.observe(now,sample.values,sample.available,motion,sample.available[3]);
}
bool updateEnvironmentHistory(uint32_t now) {
    if(now<1704067200)return false;
    bool changed=history.advance(now);
    const uint32_t blockNow=now/1800;
    if(!saveBlock)saveBlock=blockNow;
    if(blockNow>saveBlock) {
        if(storageReady && saves) {
            const auto b=envhistory::block(history,blockNow-1);
            if(xQueueSend(saves,&b,0)!=pdTRUE)return changed;
        }
        saveBlock=blockNow;
    }
    return changed;
}
void serviceEnvironmentStorage() {
    envhistory::Block b;
    if(!saves||xQueueReceive(saves,&b,0)!=pdTRUE)return;
    char name[24];path(name,b.number%48,(b.number/48)%2);
    // The other bank is retained; a partially written block fails validation on boot.
    File f=LittleFS.open(name,"w");
    bool ok=f && f.write(reinterpret_cast<const uint8_t*>(&b),sizeof(b))==sizeof(b);
    if(f){f.flush();f.close();}
    if(ok) {
        f=LittleFS.open(name,"r");envhistory::Block check;
        ok=f&&f.size()==sizeof(check)&&f.read(reinterpret_cast<uint8_t*>(&check),sizeof(check))==sizeof(check)&&envhistory::validBlock(check);
        if(f)f.close();
    }
    storageFailed=!ok;
    Serial.printf("ENV_HISTORY_SAVE: success=%d\n",ok);
}

String drawEnvironmentGraph(lgfx::LGFXBase& d,const EnvironmentSample& sample,int code,bool connected,bool brightness) {
    constexpr uint32_t bg=0x111e1c,text=0xe9f0e9,muted=0xa0b5aa;
    constexpr uint32_t colors[]={0xead09c,0x9fc9d6,0xd0dda5,0xa1cbb4};
    auto value=[&](int i){return sample.available[i]?String(sample.values[i],i==0?1:0)+(i==0?" ℃":i==1?" %":""):String("--");};
    String motion="--:--";
    uint32_t last=history.lastMotion;
    if(sample.available[3] && sample.values[3]>0)last=std::max(last,utcStamp(sample.timestamps[3]));
    if(last){time_t t=last+9*3600;tm japan;gmtime_r(&t,&japan);char buf[6];strftime(buf,sizeof(buf),"%H:%M",&japan);motion=buf;}
    d.setTextDatum(middle_left);
    for(int group=0;group<2;++group) {
        int x=8+group*155;uint32_t fill=(brightness==(group==1))?0x30434c:0x1d2d28;
        d.fillRoundRect(x,42,149,66,5,fill);
        d.setFont(&fonts::efontJA_12);d.setTextColor(muted,fill);
        d.drawString(group==0?"温湿度":"明るさ・人感",x+8,54);
        for(int row=0;row<2;++row) {
            int y=74+row*21;int axis=group*2+row;
            d.fillRoundRect(x+8,y-8,3,16,1,colors[axis]);
            d.setFont(&fonts::efontJA_16);d.setTextColor(text,fill);
            d.drawString(axis==3?motion:value(axis),x+18,y);
        }
    }
    const uint32_t now=time(nullptr);
    if(now<1704067200)return "時刻同期待ち";
    constexpr int left=43,top=134;
    const int right=brightness?308:276,bottom=brightness?170:190;
    const uint32_t endSlot=now/300,startSlot=endSlot>288?endSlot-288:1;
    auto px=[&](uint32_t slot){return left+int((int64_t(slot)-startSlot)*(right-left)/288);};
    d.setFont(&fonts::efontJA_12);
    d.drawRect(left,top,right-left+1,bottom-top+1,uint32_t{0x34453e});
    bool any=false;
    for(int line=0;line<(brightness?1:2);++line) {
        const int axis=brightness?2:line;
        float low=INFINITY,high=-INFINITY;
        for(uint32_t slot=startSlot;slot<endSlot;++slot) {
            const auto* r=history.get(slot);
            if(r&&(r->valid&(1<<axis))){low=std::min(low,r->values[axis]);high=std::max(high,r->values[axis]);}
        }
        if(!std::isfinite(low)){low=axis==0?20:0;high=axis==0?30:axis==1?100:300;}
        else {
            float pad=std::max((high-low)*.15f,axis==0?.5f:axis==1?2.f:10.f);
            low=std::floor((low-pad)/(axis==0?1:axis==1?5:10))*(axis==0?1:axis==1?5:10);
            high=std::ceil((high+pad)/(axis==0?1:axis==1?5:10))*(axis==0?1:axis==1?5:10);
            if(axis>0)low=std::max(0.f,low);
        }
        auto py=[&](float v){return bottom-2-int((v-low)/(high-low)*(bottom-top-4));};
        d.setTextColor(colors[axis],bg);
        d.setTextDatum(line==1?middle_right:middle_left);
        d.drawString(brightness?"センサー値":line==0?"温度 ℃":"湿度 %",line==1?312:8,125);
        for(int tick=0;tick<3;++tick) {
            float n=low+(high-low)*tick/2;int y=py(n);
            d.setTextDatum(line==1?middle_left:middle_right);
            d.drawString(String(n,axis==0||std::floor(n)!=n?1:0),line==1?right+5:left-5,y);
            if(line==0)d.drawFastHLine(left+1,y,right-left-1,uint32_t{0x283c32});
        }
        bool previous=false;int oldX=0,oldY=0;
        envcurve::columns(history,axis,startSlot,endSlot,left,right-left,
            [&](int x,float v,bool connect) {
                const int y=py(v);
                const bool visible=line==0 || ((x-left)%5)<3;
                if(connect && visible && (line==0 || ((oldX-left)%5)<3))
                    d.drawLine(oldX,oldY,x,y,colors[axis]);
                else if(visible)d.drawPixel(x,y,colors[axis]);
                oldX=x;oldY=y;any=true;
            });
        // Only mark the newest completed slot, as before.
        float latest;
        previous=envcurve::average(history,endSlot-1,axis,startSlot,endSlot,latest);
        if(previous) {
            oldX=left+int(std::lround(float(endSlot-1-startSlot)*(right-left)/288));
            oldY=py(latest);
        }
        if(previous)d.fillCircle(oldX,oldY,2,colors[axis]);
    }
    if(brightness) {
        constexpr int bandY=187,bandH=8;
        d.fillRect(left,bandY,right-left,bandH,uint32_t{0x1d2d28});
        d.setTextDatum(middle_right);d.setTextColor(muted,bg);d.drawString("人感",left-5,bandY+4);
        for(uint32_t slot=startSlot;slot<endSlot;++slot) {
            int x=px(slot),w=std::max(1,px(slot+1)-x);
            const auto* r=history.get(slot);
            if(r&&r->motion==2)d.fillRect(x,bandY,w,bandH,colors[3]);
            else if(!r||r->motion==0)for(int y=0;y<bandH;++y)if((slot+y)%4==0)d.drawPixel(x,bandY+y,muted);
        }
    }
    d.setTextColor(muted,bg);
    for(int i=0;i<4;++i) {
        uint32_t slot=startSlot+i*96;time_t t=time_t(slot)*300+9*3600;tm j;gmtime_r(&t,&j);
        char hour[3];strftime(hour,sizeof(hour),"%H",&j);
        d.setTextDatum(i==0?middle_left:i==3?middle_right:middle_center);
        d.drawString(hour,i==0?left:i==3?right:px(slot),207);
    }
    if(!any){d.setTextDatum(middle_center);d.setTextColor(muted,bg);d.drawString("履歴を記録しています",(left+right)/2,(top+bottom)/2);}
    if(!storageReady || storageFailed)return "履歴保存不可：画面の記録は継続";
    if(!connected)return "Wi-Fi未接続：前回値を表示";
    if(code==429)return "API制限：取得を保留中";
    if(code && code!=200)return "取得失敗：前回値を表示";
    if(!code)return "環境情報を取得しています";
    return "";
}
