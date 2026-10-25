#pragma once
#include <cstdint>
struct TwoWire {
  explicit TwoWire(int){}
  bool begin(int,int,unsigned){return true;}
  void setTimeOut(unsigned){}
  void beginTransmission(uint8_t){}
  void write(uint8_t){}
  int endTransmission(bool=true){return 0;}
  uint8_t requestFrom(uint8_t,uint8_t count){return count;}
  int read(){return 0;}
};
extern TwoWire Wire;
