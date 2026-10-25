#pragma once
#include <Arduino.h>
enum {WIFI_AP};
struct FakeWiFi {
  void mode(int){}
  void setSleep(bool){}
  bool softAP(const char*,const char*){return true;}
  String softAPIP(){return "192.168.4.1";}
};
extern FakeWiFi WiFi;
