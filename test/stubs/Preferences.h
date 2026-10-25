#pragma once
#include <cstddef>
struct Preferences {
  size_t writes=0;
  void begin(const char*,bool){}
  size_t getBytesLength(const char*){return 0;}
  size_t getBytes(const char*,void*,size_t){return 0;}
  size_t putBytes(const char*,const void*,size_t n){++writes;return n;}
};
