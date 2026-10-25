#pragma once
#include <Arduino.h>
#include <functional>
#include <map>
enum {HTTP_GET,HTTP_POST};
struct WebServer {
  std::map<String,std::function<void()>> handlers;
  std::map<String,String> arguments;
  int status=0;
  String response;
  explicit WebServer(int){}
  void on(const String &path,int,std::function<void()> handler){handlers[path]=handler;}
  void onNotFound(std::function<void()>){ }
  bool hasArg(const String &key){return arguments.count(key);}
  String arg(const String &key){return arguments[key];}
  void send(int code,const char*,const String &data){status=code;response=data;}
  void send_P(int code,const char*,const char *data){status=code;response=data;}
  void begin(){}
  void handleClient(){}
};
