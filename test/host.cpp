#include "hardware.h"
#include "protocol.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <random>
#include <vector>
using namespace ts;
namespace {
void closeTo(double actual,double expected,double tolerance=0.0001) {assert(std::fabs(actual-expected)<tolerance);}
void geometryTests() {
  float lastA=-1,lastB=-1;
  for(int i=0;i<=7000;++i) {
    const float wing=i/100.0f; float a,b;
    assert(inverseKinematics(wing,a,b)); assert(a>=lastA && b>=lastB);
    lastA=a; lastB=b;
    const double ar=a*Pi/180,br=b*Pi/180;
    const double x=55+100*std::cos(br)-125*std::cos(ar),y=100*std::sin(br)-125*std::sin(ar);
    closeTo(std::hypot(x,y),30,0.00002); closeTo(std::atan2(y,x)*180/Pi,-wing,0.00003);
    float targets[]={a,b,a,b}; assert(validTargets(Mode::Automatic,targets)); assert(validTargets(Mode::Wings,targets));
    assert(a>=0 && b>=0); // both arms rise for the negative physical wing angle
  }
  closeTo(lastA,82.64623825); closeTo(lastB,106.70235412);
  float leftA,leftB,rightA,rightB;
  assert(inverseKinematics(40,leftA,leftB)); closeTo(leftA,72.34018799); closeTo(leftB,86.61647671);
  assert(inverseKinematics(70,rightA,rightB));
  float asymmetric[]={leftA,leftB,rightA,rightB}; assert(validTargets(Mode::Wings,asymmetric));
  assert(!validTargets(Mode::Bench,asymmetric)); // bench's independent-motor limit stays 45 degrees
  const float oldPositiveBranch[]={18.21528998f,42.27140585f,0,0};
  const float oldRightBranch[]={0,0,18.21528998f,42.27140585f};
  assert(!validTargets(Mode::Wings,oldPositiveBranch)); assert(!validTargets(Mode::Wings,oldRightBranch));
  float beyondA,beyondB;
  // Exact linkage closure just beyond the wing limit must still be rejected.
  const double phi=-70.1*Pi/180,dx=55-30*std::cos(phi),dy=-30*std::sin(phi),d=std::hypot(dx,dy);
  const double beyond=std::atan2(dy,dx)+std::acos((125*125+d*d-100*100)/(250*d));
  beyondA=beyond*180/Pi;
  beyondB=std::atan2(125*std::sin(beyond)+30*std::sin(phi),125*std::cos(beyond)+30*std::cos(phi)-55)*180/Pi;
  float excessive[]={beyondA,beyondB,0,0}; assert(!validTargets(Mode::Wings,excessive));
  float a,b; assert(inverseKinematics(0,a,b)); assert(a==0 && b==0);
  for(float tiny:{0.00001f,0.0001f,0.001f}) assert(inverseKinematics(tiny,a,b) && a>=0 && b>=0);
  assert(!inverseKinematics(-0.01f,a,b)); assert(!inverseKinematics(70.01f,a,b));
  assert(!inverseKinematics(std::numeric_limits<float>::quiet_NaN(),a,b));
  float inconsistent[]={18,1,0,0}; assert(!validTargets(Mode::Wings,inconsistent));
  float bench[]={-45,45,20,0}; assert(validTargets(Mode::Bench,bench)); assert(!validTargets(Mode::Automatic,bench));
  bench[0]=-45.01f; assert(!validTargets(Mode::Bench,bench));
  assert(!validTargets(Mode(255),bench));
  puts("PASS linkage: 7001 negative wing angles, rising arms, +40 command, independent wings, closure and limits");
}
void mappingTests() {
  Curve c=defaultCurve(); float left,right;
  mapWings(c,20,0,left,right); closeTo(left,0); closeTo(right,20);
  mapWings(c,-20,0,left,right); closeTo(left,20); closeTo(right,0);
  mapWings(c,0,22.5,left,right); closeTo(left,22.5); closeTo(right,22.5);
  mapWings(c,50,50,left,right); closeTo(left,50); closeTo(right,70);
  for(int i=-900;i<=900;++i) {
    float l2,r2; mapWings(c,i/10.0f,15,left,right); mapWings(c,-i/10.0f,15,l2,r2);
    closeTo(left,r2); closeTo(right,l2); assert(left>=0 && right<=70);
  }
  closeTo(interpolate(c.rollRight,-1000),0); closeTo(interpolate(c.rollRight,1000),70);
  puts("PASS mappings: interpolation, mirrored roll, tied pitch, saturation");
}
void encoderPidTests() {
  Calibration c; c.zero=16380; c.hasZero=1;
  Encoder e; e.reset(2,c); closeTo(e.rawRelative(),6*360.0/16384);
  e.update(16382); closeTo(e.rawRelative(),2*360.0/16384);
  e.update(3); closeTo(e.rawRelative(),7*360.0/16384);
  c.reverseAngle=1; closeTo(e.angle(c),-7*360.0/16384);
  for(int output=0;output<2;++output) for(int angle=0;angle<2;++angle) {
    c.reverseOutput=output; c.reverseAngle=angle;
    const int physical=physicalPwm(500,c);
    // A correctly calibrated output maps physical PWM to raw encoder rate.
    const int rawRate=physical*(output?-1:1);
    const int logicalRate=rawRate*(angle?-1:1); assert(logicalRate==500);
  }
  PidConfig cfg; Pid pid; pid.reset(0);
  assert(pid.step(10,0,0.001,true,cfg)>0);
  assert(pid.step(0,10,0.001,true,cfg)<0);
  assert(pid.step(50,0,0.001,false,cfg)==0 && pid.integral==0);
  assert(pid.step(0,0.2,0.001,true,cfg)==0);
  cfg.enI=1; cfg.ki=1000;
  for(int i=0;i<10000;++i) assert(std::abs(pid.step(10000,0,0.001,true,cfg))<=MaxPwm);
  assert(pid.integral<=MaxPwm);
  assert(validPid(cfg)); cfg.kd=std::numeric_limits<float>::infinity(); assert(!validPid(cfg));
  DriveConfig config; assert(validConfig(config)); config.motor[2].zero=16384; assert(!validConfig(config));
  puts("PASS encoders/PID: reboot zero, wraparound, direction transforms, deadband, anti-windup");
}
void safetyTests() {
  SafetyGate g; g.epoch=100;
  assert(!g.armed && !g.arm(100,0,true));
  assert(g.accept(11,1,100,10)); assert(g.arm(100,11,true));
  assert(!g.accept(11,1,100,20)); assert(!g.accept(11,0,100,20));
  g.poll(309); assert(g.armed); g.poll(310); assert(!g.armed && (g.faults&LinkFault));
  assert(!g.arm(100,311,true));
  assert(g.accept(11,2,g.epoch,312)); assert(!g.armed); // traffic recovery is not re-arming
  assert(!g.arm(g.epoch,313,false)); assert(g.arm(g.epoch,313,true));
  const uint32_t oldEpoch=g.epoch; g.stop(EncoderFault); assert(!g.armed);
  assert(!g.arm(oldEpoch,314,true));
  assert(g.accept(11,3,g.epoch,315)); assert(g.arm(g.epoch,316,true));
  const uint32_t epoch=g.epoch;
  assert(!g.accept(22,1,epoch,317)); assert(!g.armed && g.epoch!=epoch);
  assert(g.accept(22,2,g.epoch,318)); assert(!g.armed);
  assert(g.arm(g.epoch,319,true));
  g.stop(); assert(!g.armed && !g.arm(g.epoch,320,true));
  SafetyGate wrap; wrap.epoch=1;
  assert(wrap.accept(33,0xfffffffe,1,0xfffffff0));
  assert(wrap.accept(33,0xffffffff,1,0xfffffff1));
  assert(wrap.accept(33,0,1,0xfffffff2));
  assert(wrap.fresh(0x20)); assert(!wrap.fresh(0x200));
  puts("PASS safety: boot, stale/replayed commands, timeout, explicit re-arm, session and clock wrap");
}
void polarityTests() {
  for(int hardwarePolarity=0;hardwarePolarity<2;++hardwarePolarity)
    for(int configuredPolarity=0;configuredPolarity<2;++configuredPolarity)
      for(int angleDirection=0;angleDirection<2;++angleDirection)
        for(float initial:{0.0f,44.0f,-44.0f}) {
          Calibration c; c.reverseOutput=configuredPolarity; c.reverseAngle=angleDirection;
          PolarityTest test; assert(test.begin(initial,100,c));
          assert(std::fabs(test.target-initial)==5 && std::fabs(test.target)<=BenchLimit);
          const float goalDirection=test.target>initial ? 1 : -1;
          float angle=initial; uint32_t ms=100;
          // This motor cannot start in 80 ms: model a 1.2-second breakaway delay.
          for(;ms<1300;++ms) {test.update(angle,ms,true); assert(std::abs(test.pwm())==1500);}
          for(;ms<5100 && test.state==PolarityTest::State::Driving;++ms) {
            const float rawRate=(hardwarePolarity ? -1 : 1)*test.pwm()/1500.0f;
            angle+=(angleDirection ? -1 : 1)*rawRate*0.01f;
            test.update(angle,ms,true);
          }
          assert(test.state==PolarityTest::State::Settling && test.pwm()==0);
          assert(std::fabs(angle-initial)>=5 && std::fabs(angle-initial)<5.02f);
          const uint32_t stopped=test.stopped;
          test.update(angle,stopped+49,true); assert(!test.finished() && !test.pwm());
          test.update(angle,stopped+50,true); assert(test.state==PolarityTest::State::Passed);
          assert(test.reverseOutput==bool(hardwarePolarity));
          // The corrected PID output now moves toward its target for either encoder direction.
          c.reverseOutput=test.reverseOutput;
          const int physical=physicalPwm(int(goalDirection)*1500,c);
          const int logicalRate=(angleDirection ? -1 : 1)*(hardwarePolarity ? -1 : 1)*physical;
          assert(logicalRate*goalDirection>0);
        }
  Calibration c; c.reverseOutput=1; PolarityTest stalled;
  assert(stalled.begin(0,0,c)); stalled.update(0.1f,4999,true); assert(stalled.pwm()!=0);
  stalled.update(0.2f,5000,true); assert(stalled.state==PolarityTest::State::TimedOut && stalled.pwm()==0);
  assert(stalled.reverseOutput); // a stalled/partial move cannot change calibration
  for(bool duringSettle:{false,true}) {
    PolarityTest cancelled; assert(cancelled.begin(0,0,c));
    if(duringSettle) cancelled.update(5,100,true);
    cancelled.update(0,101,false); assert(cancelled.state==PolarityTest::State::Aborted && cancelled.pwm()==0);
  }
  PolarityTest noisy; assert(noisy.begin(0,0,c));
  for(unsigned i=0;i<4999;++i) {noisy.update(i%2 ? 0.1f : -0.1f,i,true); assert(!noisy.finished());}
  noisy.update(0,5000,true); assert(noisy.state==PolarityTest::State::TimedOut);
  PolarityTest wrap; assert(wrap.begin(0,0xfffffff0,c));
  wrap.update(0,uint32_t(0xfffffff0u+5000u),true); assert(wrap.state==PolarityTest::State::TimedOut);
  PolarityTest invalid;
  assert(!invalid.begin(46,0,c)); assert(!invalid.begin(std::numeric_limits<float>::quiet_NaN(),0,c));
  puts("PASS polarity: delayed start, 5-degree toward/away motion, all sign combinations, stall, noise, cancel, settle");
}
std::vector<uint8_t> bytes(const Frame &f) {
  std::vector<uint8_t> out(MaxEncoded); const size_t n=encode(f,out.data()); assert(n && n<=MaxEncoded); out.resize(n); return out;
}
void protocolTests() {
  closeTo(crc16(reinterpret_cast<const uint8_t*>("123456789"),9),0x29b1);
  std::mt19937 rng(7654321);
  for(unsigned run=0;run<10000;++run) {
    Frame f; f.type=Message::Telemetry; f.sequence=run; f.session=0xabcdef01; f.length=run%(MaxPayload+1);
    for(unsigned i=0;i<f.length;++i) f.payload[i]=run%3 ? uint8_t(rng()) : 0;
    auto data=bytes(f); Parser parser; Frame decoded; unsigned count=0;
    for(uint8_t b:data) if(parser.feed(b,decoded)) ++count;
    assert(count==1 && parser.errors==0 && decoded.sequence==f.sequence && decoded.session==f.session && decoded.length==f.length);
    assert(std::memcmp(decoded.payload,f.payload,f.length)==0);
  }
  Control control{}; control.receiverBoot=9; control.epoch=19; control.mode=uint8_t(Mode::Wings);
  inverseKinematics(70,control.target[0],control.target[1]);
  Frame f=packet(Message::Control,100,200,control),decoded;
  const auto good=bytes(f);
  // Every single-bit corruption must be rejected; subsequent complete frames recover.
  for(unsigned position=0;position<good.size()-1;++position) for(unsigned bit=0;bit<8;++bit) {
    auto damaged=good; damaged[position]^=1<<bit; Parser parser; unsigned accepted=0;
    for(uint8_t b:damaged) if(parser.feed(b,decoded)) ++accepted;
    assert(accepted==0);
    for(uint8_t b:good) if(parser.feed(b,decoded)) ++accepted;
    assert(accepted==1);
  }
  Parser parser;
  for(unsigned i=0;i<MaxEncoded+100;++i) assert(!parser.feed(1,decoded));
  assert(!parser.feed(0,decoded)); assert(parser.errors==1);
  for(uint8_t b:good) parser.feed(b,decoded);
  Control unpacked; assert(unpack(decoded,unpacked)); assert(unpacked.epoch==19);
  Ack wrong; assert(!unpack(decoded,wrong));
  // Truncation and interleaved random bytes cannot prevent delimiter recovery.
  for(unsigned i=0;i<good.size()/2;++i) parser.feed(good[i],decoded);
  parser.feed(0,decoded);
  unsigned accepted=0;
  for(uint8_t b:good) if(parser.feed(b,decoded)) ++accepted;
  assert(accepted==1);
  for(unsigned i=0;i<200000;++i) parser.feed(uint8_t(rng()),decoded);
  parser.feed(0,decoded);
  accepted=0; for(uint8_t b:good) if(parser.feed(b,decoded)) ++accepted;
  assert(accepted==1);
  puts("PASS UART: 10000 round trips, bit corruption, fragmentation, oversize, fuzz/resynchronization");
}
}
int main() {geometryTests();mappingTests();encoderPidTests();safetyTests();polarityTests();protocolTests();puts("All host tests passed.");}
