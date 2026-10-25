#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

namespace ts {
constexpr unsigned Motors = 4, Points = 21, Presets = 3;
constexpr float WingLimit = 70.0f, BenchLimit = 45.0f;
constexpr int MaxPwm = 4095;
constexpr uint32_t LinkTimeoutMs = 300;
constexpr double Pi = 3.14159265358979323846;
template <class T> inline T clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline bool newer(uint32_t seq, uint32_t last) { return int32_t(seq - last) > 0; }
enum class Mode : uint8_t { Automatic, Wings, Bench };
enum Fault : uint32_t { NoFault = 0, EncoderFault = 1, LinkFault = 2, ControllerFault = 4 };

struct PidConfig {
  float kp = 35, ki = 0.5f, kd = 2.5f, deadband = 0.70f;
  uint16_t minPwm = 350;
  uint8_t enP = 1, enI = 0, enD = 1, reserved[3] = {};
};
inline bool validPid(const PidConfig &p) {
  return std::isfinite(p.kp) && p.kp >= 0 && p.kp <= 1000 &&
    std::isfinite(p.ki) && p.ki >= 0 && p.ki <= 1000 &&
    std::isfinite(p.kd) && p.kd >= 0 && p.kd <= 1000 &&
    std::isfinite(p.deadband) && p.deadband >= 0 && p.deadband <= 10 &&
    p.minPwm <= MaxPwm && p.enP <= 1 && p.enI <= 1 && p.enD <= 1;
}
struct Calibration {
  uint16_t zero = 0;
  uint8_t hasZero = 0, reverseOutput = 0, reverseAngle = 0, reserved[3] = {};
};
struct DriveConfig {
  uint32_t version = 1;
  PidConfig pid;
  Calibration motor[Motors];
};
inline bool validConfig(const DriveConfig &c) {
  if (c.version != 1 || !validPid(c.pid)) return false;
  for (const auto &m : c.motor)
    if (m.zero >= 16384 || m.hasZero > 1 || m.reverseOutput > 1 || m.reverseAngle > 1) return false;
  return true;
}

// Local linkage coordinates: both cranks point right at home. The right
// assembly uses its own mirrored coordinate frame, not negated PID gains.
// A positive UI/curve command means a negative physical coupler angle. Keep
// the +acos branch so both crank angles remain positive (both arms rise).
inline bool inverseKinematics(float wing, float &longArm, float &shortArm) {
  if (!std::isfinite(wing) || wing < 0 || wing > WingLimit) return false;
  if (wing == 0) { longArm = shortArm = 0; return true; }
  const double phi = -wing * Pi / 180;
  const double x = 55 - 30 * std::cos(phi), y = -30 * std::sin(phi);
  const double d = std::hypot(x, y);
  double cosine = (125 * 125 + d*d - 100 * 100) / (250 * d);
  if (cosine < -1.000000001 || cosine > 1.000000001) return false;
  const double a = std::atan2(y, x) + std::acos(clamp(cosine, -1.0, 1.0));
  const double b = std::atan2(125 * std::sin(a) + 30 * std::sin(phi),
                             125 * std::cos(a) + 30 * std::cos(phi) - 55);
  longArm = float(a * 180 / Pi); shortArm = float(b * 180 / Pi);
  return std::isfinite(longArm) && std::isfinite(shortArm);
}
inline bool validTargets(Mode mode, const float *target) {
  if (uint8_t(mode) > uint8_t(Mode::Bench)) return false;
  for (unsigned i = 0; i < Motors; ++i) {
    if (!std::isfinite(target[i])) return false;
    if (mode == Mode::Bench && std::fabs(target[i]) > BenchLimit) return false;
  }
  if (mode == Mode::Bench) return true;
  for (unsigned pair = 0; pair < 2; ++pair) {
    const float a = target[pair*2], b = target[pair*2+1];
    // Rising-arm branch at a -70 degree physical wing angle (rounded outward).
    if (a < 0 || b < 0 || a > 82.647f || b > 106.703f) return false;
    const double ar = a*Pi/180, br = b*Pi/180;
    const double x = 55 + 100*std::cos(br) - 125*std::cos(ar);
    const double y = 100*std::sin(br) - 125*std::sin(ar);
    const double phi = std::atan2(y, x)*180/Pi;
    if (std::fabs(std::hypot(x,y)-30) > 0.02 || phi < -WingLimit-0.002 || phi > 0.002) return false;
  }
  return true;
}

struct Curve { float rollRight[Points] = {}, pitchBoth[Points] = {}; };
inline float interpolate(const float *v, float input) {
  const float position = (clamp(input, -90.0f, 90.0f) + 90) / 9;
  const unsigned i = unsigned(position);
  return i >= Points-1 ? v[Points-1] : v[i] + (v[i+1]-v[i])*(position-i);
}
inline void mapWings(const Curve &c, float roll, float pitch, float &left, float &right) {
  const float common = interpolate(c.pitchBoth, pitch);
  left = clamp(interpolate(c.rollRight, -roll)+common, 0.0f, WingLimit);
  right = clamp(interpolate(c.rollRight, roll)+common, 0.0f, WingLimit);
}
inline Curve defaultCurve() {
  Curve c;
  for (unsigned i=0; i<Points; ++i) c.rollRight[i]=c.pitchBoth[i]=clamp(float(int(i)*9-90),0.0f,WingLimit);
  return c;
}

struct Encoder {
  bool initialized = false;
  uint16_t previous = 0;
  int32_t continuous = 0, zero = 0;
  void reset(uint16_t raw, const Calibration &c) {
    initialized=true; previous=raw; continuous=raw;
    int32_t diff=int32_t(raw)-c.zero;
    if (diff>8192) diff-=16384;
    if (diff< -8192) diff+=16384;
    zero=c.hasZero ? continuous-diff : continuous;
  }
  void update(uint16_t raw) {
    int32_t diff=int32_t(raw)-previous;
    if (diff>8192) diff-=16384;
    if (diff< -8192) diff+=16384;
    continuous+=diff; previous=raw;
  }
  float rawRelative() const { return float(continuous-zero)*(360.0f/16384); }
  float angle(const Calibration &c) const { return (c.reverseAngle ? -1 : 1)*rawRelative(); }
};
inline int physicalPwm(int logical, const Calibration &c) {
  return (c.reverseOutput ? -1 : 1)*(c.reverseAngle ? -1 : 1)*logical;
}
struct Pid {
  float integral=0, derivative=0, previous=0;
  void reset(float angle) { integral=derivative=0; previous=angle; }
  int step(float target, float measured, float dt, bool enabled, const PidConfig &c) {
    dt=clamp(dt,0.0001f,0.02f);
    // Equivalent to the original 0.85/0.15 filter at 1 kHz.
    const float alpha=dt/(0.005666667f+dt);
    derivative += alpha*(-(measured-previous)/dt-derivative);
    previous=measured;
    const float error=target-measured;
    if (!enabled) { reset(measured); return 0; }
    if (std::fabs(error)<=c.deadband) { integral*=std::pow(0.95f,dt/0.001f); return 0; }
    if (c.enI && c.ki>0.0001f) integral=clamp(integral+error*c.ki*dt,-float(MaxPwm),float(MaxPwm));
    else integral=0;
    float output=(c.enP ? c.kp*error : 0)+(c.enI ? integral : 0)+(c.enD ? c.kd*derivative : 0);
    if (output>0.1f) output+=c.minPwm;
    else if (output< -0.1f) output-=c.minPwm;
    else output=0;
    return int(clamp(output,-float(MaxPwm),float(MaxPwm)));
  }
};

// Mutated only under N1's short cross-core state lock. Epochs invalidate any
// queued arm action whenever a fault, stop, or controller restart occurs.
struct SafetyGate {
  uint32_t epoch=1, session=0, lastSequence=0, lastControl=0, faults=0;
  bool armed=false, haveSequence=false, haveControl=false;
  void stop(uint32_t fault=0) { armed=false; faults|=fault; ++epoch; haveControl=false; }
  bool accept(uint32_t sender, uint32_t seq, uint32_t expectedEpoch, uint32_t now) {
    if (session && session!=sender) { stop(LinkFault); haveSequence=false; }
    session=sender;
    if (expectedEpoch!=epoch || (haveSequence && !newer(seq,lastSequence))) return false;
    haveSequence=haveControl=true; lastSequence=seq; lastControl=now; return true;
  }
  bool fresh(uint32_t now) const { return haveControl && uint32_t(now-lastControl)<LinkTimeoutMs; }
  bool arm(uint32_t expectedEpoch, uint32_t now, bool healthy) {
    if (expectedEpoch!=epoch || !fresh(now) || !healthy) return false;
    faults=0; armed=true; return true;
  }
  void poll(uint32_t now) { if (haveControl && !fresh(now)) stop(LinkFault); }
};
} // namespace ts
