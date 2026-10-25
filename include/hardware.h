#pragma once
#include <cstdint>
namespace ts {
constexpr int UartRx=16, UartTx=17, UartBaud=115200;
constexpr int PwmPins[4][2]={{13,12},{14,27},{26,25},{5,18}};
constexpr int EncoderSda[2]={22,32}, EncoderScl[2]={21,33};
constexpr uint8_t EncoderAddress[2]={0x06,0x46};
constexpr int MpuSda=21, MpuScl=22;
constexpr unsigned PwmFrequency=19500, PwmResolution=12;
}
