#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
using String=std::string;
#define PROGMEM
extern uint32_t fakeMillis;
inline uint32_t millis(){return fakeMillis;}
inline uint32_t micros(){return fakeMillis*1000;}
inline void delay(unsigned n){fakeMillis+=n;}
using TickType_t=uint32_t;
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define pdMS_TO_TICKS(n) (n)
#define pdPASS 1
#define SERIAL_8N1 0
#define OUTPUT 1
#define LOW 0
inline TickType_t xTaskGetTickCount(){return fakeMillis;}
inline void vTaskDelayUntil(TickType_t*,TickType_t){}
inline void vTaskDelay(TickType_t){}
inline int xTaskCreatePinnedToCore(void(*)(void*),const char*,unsigned,void*,unsigned,void*,int){return pdPASS;}
inline void pinMode(int,int){}
inline void digitalWrite(int,int){}
inline double ledcSetup(int,unsigned,unsigned){return 19500;}
inline void ledcAttachPin(int,int){}
inline void ledcWrite(int,int){}
struct HardwareSerial {
  std::vector<uint8_t> sent;
  explicit HardwareSerial(int){}
  void begin(unsigned,int=0,int=0,int=0){}
  void setRxBufferSize(unsigned){}
  void setTxBufferSize(unsigned){}
  int available(){return 0;}
  int read(){return -1;}
  size_t write(const uint8_t*data,size_t n){sent.insert(sent.end(),data,data+n);return n;}
  void println(const char*){}
  template<class T> void println(const T&){}
  template<class T> void print(const T&){}
};
extern HardwareSerial Serial;
