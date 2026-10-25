#pragma once
#include "model.h"
#include "polarity.h"
#include <cstddef>

namespace ts {
constexpr unsigned MaxPayload=224, MaxDecoded=MaxPayload+16, MaxEncoded=MaxDecoded+2;
enum class Message : uint8_t { Control=1, Telemetry, Config, Action, Ack };
enum class ActionKind : uint8_t { Stop=1, Arm, Zero, Polarity, Direction, Test, Pid };
enum class Result : uint8_t { Ok=0, Invalid, Stale, Unhealthy, Busy, NoMovement, Timeout, Cancelled };
inline uint32_t actionTimeoutMs(uint8_t kind) {
  return kind==uint8_t(ActionKind::Test) ? PolarityTestTimeoutMs+PolarityTestSettleMs+1000 : 2500;
}

// Fixed-width little-endian payloads, IEEE-754 float. ESP32 and host tests use
// this layout; change the protocol version if these layouts change.
// Keep all payload objects 4-byte aligned: passing a packed PID member by
// reference is undefined and can fault on Xtensa. Explicit reserved bytes
// make the aligned layout deterministic without implicit wire padding.
struct Control { uint32_t receiverBoot, epoch; float target[Motors]; uint8_t mode, reserved[3]; };
struct Action {
  uint32_t receiverBoot, epoch;
  uint8_t kind, mask, value, reserved=0;
  PidConfig pid;
};
struct Ack { uint32_t request, epoch; uint8_t result, kind, reserved[2]; };
struct Telemetry {
  uint32_t epoch, faults, configRevision, uartErrors;
  float angle[Motors], raw[Motors], target[Motors];
  int16_t pwm[Motors];
  uint32_t maxCycleUs[2], missed[2], sensorErrors[Motors];
  uint8_t validMask, armed, mode, pendingSave, busy, reserved[3];
};
static_assert(sizeof(PidConfig)==24,"PID wire layout");
static_assert(sizeof(Control)==28 && sizeof(Action)==36 && sizeof(Ack)==12 && sizeof(Telemetry)==112,"wire layout");
static_assert(sizeof(DriveConfig)<=MaxPayload && sizeof(Telemetry)<=MaxPayload,"UART payload too large");
struct Frame { Message type{}; uint32_t sequence=0, session=0; uint16_t length=0; uint8_t payload[MaxPayload]{}; };
inline uint16_t crc16(const uint8_t *data,size_t count) {
  uint16_t crc=0xffff;
  while(count--) { crc^=uint16_t(*data++)<<8; for(int i=0;i<8;++i) crc=crc&0x8000 ? uint16_t((crc<<1)^0x1021) : uint16_t(crc<<1); }
  return crc;
}
inline void put16(uint8_t *p,uint16_t n) { p[0]=n; p[1]=n>>8; }
inline void put32(uint8_t *p,uint32_t n) { for(int i=0;i<4;++i) p[i]=n>>(8*i); }
inline uint16_t get16(const uint8_t *p) { return uint16_t(p[0])|(uint16_t(p[1])<<8); }
inline uint32_t get32(const uint8_t *p) { return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24); }
inline size_t encode(const Frame &f,uint8_t *out) {
  if(f.length>MaxPayload) return 0;
  uint8_t data[MaxDecoded];
  data[0]=0x54; data[1]=0x53; data[2]=1; data[3]=uint8_t(f.type);
  put16(data+4,f.length); put32(data+6,f.sequence); put32(data+10,f.session);
  std::memcpy(data+14,f.payload,f.length);
  const size_t len=f.length+16;
  put16(data+len-2,crc16(data,len-2));
  size_t write=1,codeAt=0; uint8_t code=1;
  for(size_t i=0;i<len;++i) {
    if(!data[i]) { out[codeAt]=code; codeAt=write++; code=1; }
    else { out[write++]=data[i]; if(++code==0xff) { out[codeAt]=code; codeAt=write++; code=1; } }
  }
  out[codeAt]=code; out[write++]=0; return write;
}
class Parser {
  uint8_t encoded[MaxEncoded]; size_t used=0; bool overflow=false;
public:
  uint32_t errors=0, packets=0;
  bool feed(uint8_t b,Frame &f) {
    if(b) {
      if(used<sizeof(encoded) && !overflow) encoded[used++]=b;
      else overflow=true;
      return false;
    }
    if(!used && !overflow) return false;
    if(overflow) { ++errors; used=0; overflow=false; return false; }
    uint8_t data[MaxDecoded]; size_t read=0,write=0; bool ok=true;
    while(read<used && ok) {
      const unsigned code=encoded[read++];
      if(!code || read+code-1>used || write+code-1>sizeof(data)) { ok=false; break; }
      for(unsigned i=1;i<code;++i) data[write++]=encoded[read++];
      if(code!=255 && read<used) { if(write==sizeof(data)) { ok=false; break; } data[write++]=0; }
    }
    used=0;
    if(!ok || write<16 || data[0]!=0x54 || data[1]!=0x53 || data[2]!=1 ||
       get16(data+4)>MaxPayload || write!=size_t(get16(data+4))+16 ||
       get16(data+write-2)!=crc16(data,write-2)) { ++errors; return false; }
    f.type=Message(data[3]); f.length=get16(data+4); f.sequence=get32(data+6); f.session=get32(data+10);
    std::memcpy(f.payload,data+14,f.length); ++packets; return true;
  }
};
template<class T> inline Frame packet(Message type,uint32_t seq,uint32_t session,const T &body) {
  static_assert(sizeof(T)<=MaxPayload,"payload overflow");
  Frame f; f.type=type; f.sequence=seq; f.session=session; f.length=sizeof(T); std::memcpy(f.payload,&body,sizeof(T)); return f;
}
template<class T> inline bool unpack(const Frame &f,T &body) {
  if(f.length!=sizeof(T)) return false;
  std::memcpy(&body,f.payload,sizeof(T)); return true;
}
}
