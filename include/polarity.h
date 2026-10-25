#pragma once
#include "model.h"

namespace ts {
constexpr int PolarityTestPwm=1500;
constexpr float PolarityTestTravel=5.0f;
constexpr uint32_t PolarityTestTimeoutMs=5000, PolarityTestSettleMs=50;

// A bounded open-loop identification move, not a PID move: an incorrectly
// signed PID would accelerate away from its target. Use the existing drive
// transform, observe toward/away movement, then correct only output polarity.
struct PolarityTest {
  enum class State { Idle, Driving, Settling, Passed, TimedOut, Aborted };
  State state=State::Idle;
  float initial=0,target=0,delta=0;
  uint32_t started=0,stopped=0;
  bool reverseOutput=false;
  int drive=0;
  bool begin(float angle,uint32_t now,const Calibration &c) {
    *this=PolarityTest{};
    if(!std::isfinite(angle) || std::fabs(angle)>BenchLimit) return false;
    initial=angle;
    target=angle+PolarityTestTravel<=BenchLimit ? angle+PolarityTestTravel : angle-PolarityTestTravel;
    drive=physicalPwm(target>initial ? PolarityTestPwm : -PolarityTestPwm,c);
    reverseOutput=bool(c.reverseOutput); started=now; state=State::Driving;
    return true;
  }
  bool finished() const { return state==State::Passed || state==State::TimedOut || state==State::Aborted; }
  int pwm() const { return state==State::Driving ? drive : 0; }
  void update(float angle,uint32_t now,bool allowed) {
    if(state==State::Idle || finished()) return;
    if(!allowed || !std::isfinite(angle)) {state=State::Aborted; return;}
    if(state==State::Driving) {
      delta=angle-initial;
      if(std::fabs(delta)>=PolarityTestTravel) {
        // Decide at the travel threshold; settling/backlash cannot undo it.
        if(delta*(target-initial)<0) reverseOutput=!reverseOutput;
        stopped=now; state=State::Settling;
      } else if(uint32_t(now-started)>=PolarityTestTimeoutMs) state=State::TimedOut;
    } else if(uint32_t(now-stopped)>=PolarityTestSettleMs) state=State::Passed;
  }
};
}
