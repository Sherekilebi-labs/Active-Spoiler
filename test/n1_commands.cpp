// Exercise the actual N1 UART dispatcher with hardware/task creation stubbed.
// Motor tasks are not run: real I2C/PWM timing remains a hardware test.
#include <Arduino.h>
#include <cassert>
#include <cstdio>
uint32_t fakeMillis=100;
HardwareSerial Serial(0);
#include "../src/n1.cpp"

Frame controlFrame(uint32_t sequence,uint32_t epoch,uint32_t session=22) {
  Control c{}; c.receiverBoot=bootId; c.epoch=epoch; c.mode=uint8_t(Mode::Automatic);
  return packet(Message::Control,sequence,session,c);
}
Frame actionFrame(uint32_t sequence,uint32_t epoch,ActionKind kind,uint8_t mask=0,uint8_t value=0) {
  Action a{}; a.receiverBoot=bootId; a.epoch=epoch; a.kind=uint8_t(kind); a.mask=mask; a.value=value;
  return packet(Message::Action,sequence,22,a);
}
Ack latestAck() {
  Parser p; Frame f; Ack ack{}; bool found=false;
  for(uint8_t b:uart.sent) if(p.feed(b,f) && f.type==Message::Ack) {assert(unpack(f,ack)); found=true;}
  assert(found); return ack;
}
int main() {
  bootId=7; gate.epoch=100; telemetry.validMask=15;
  receive(controlFrame(1,100));
  receive(actionFrame(2,100,ActionKind::Arm));
  assert(!gate.armed && Result(latestAck().result)==Result::Unhealthy); // saved zeros are required
  for(auto &m:config.motor) m.hasZero=1;
  const Frame arm=actionFrame(3,100,ActionKind::Arm);
  receive(arm); assert(gate.armed && Result(latestAck().result)==Result::Ok);
  gate.stop(EncoderFault);
  const uint32_t faultEpoch=gate.epoch;
  receive(arm); assert(!gate.armed && gate.epoch==faultEpoch); // duplicate acknowledgment cannot re-arm
  receive(actionFrame(4,100,ActionKind::Arm)); assert(!gate.armed && Result(latestAck().result)==Result::Stale);
  receive(controlFrame(5,gate.epoch)); assert(!gate.armed);
  receive(actionFrame(6,gate.epoch,ActionKind::Arm)); assert(gate.armed && !gate.faults);
  const Frame reverse=actionFrame(7,gate.epoch,ActionKind::Direction,4,4);
  const size_t before=uart.sent.size(); receive(reverse);
  assert(!gate.armed && pending.active && config.motor[2].reverseAngle);
  assert(!config.motor[0].reverseAngle && uart.sent.size()==before); // no success before task acknowledgment
  receive(reverse); assert(uart.sent.size()==before); // duplicate pending operation is not reapplied
  const uint32_t operationEpoch=gate.epoch;
  receive(actionFrame(8,operationEpoch,ActionKind::Polarity,1,1));
  assert(Result(latestAck().result)==Result::Busy && !config.motor[0].reverseOutput);
  receive(actionFrame(9,operationEpoch,ActionKind::Stop));
  assert(!pending.active && !gate.armed && Result(latestAck().result)==Result::Ok);
  receive(reverse); assert(!pending.active && !gate.armed && Result(latestAck().result)==Result::Stale);
  receive(controlFrame(10,gate.epoch));
  receive(actionFrame(11,gate.epoch,ActionKind::Test,1));
  assert(!pending.active && Result(latestAck().result)==Result::Invalid); // motion tests require bench mode
  receive(actionFrame(12,gate.epoch,ActionKind::Arm)); assert(gate.armed);
  fakeMillis+=300; gate.poll(fakeMillis); assert(!gate.armed && (gate.faults&LinkFault));
  const uint32_t stoppedEpoch=gate.epoch;
  receive(controlFrame(13,stoppedEpoch));
  receive(controlFrame(13,stoppedEpoch)); // duplicate targets do not refresh heartbeat
  fakeMillis+=300; gate.poll(fakeMillis); assert(!gate.armed && gate.epoch!=stoppedEpoch);
  receive(controlFrame(14,gate.epoch)); mode=Mode::Bench;
  const uint32_t beforeTest=gate.epoch;
  receive(actionFrame(15,beforeTest,ActionKind::Test,1));
  assert(pending.active && !gate.armed && gate.epoch!=beforeTest);
  receive(actionFrame(16,beforeTest,ActionKind::Stop)); // STOP must work before the browser learns the new epoch
  assert(!pending.active && !gate.armed && Result(latestAck().result)==Result::Ok);
  Control wingControl{}; wingControl.receiverBoot=bootId; wingControl.epoch=gate.epoch; wingControl.mode=uint8_t(Mode::Wings);
  assert(inverseKinematics(40,wingControl.target[0],wingControl.target[1]));
  assert(inverseKinematics(70,wingControl.target[2],wingControl.target[3]));
  receive(packet(Message::Control,17,22,wingControl));
  for(unsigned i=0;i<Motors;++i) assert(targets[i]==wingControl.target[i]); // UART accepts the larger rising-arm targets
  assert(targets[0]>70 && targets[3]>100);
  puts("PASS actual N1 dispatcher: zero requirement, replay, faults, pending calibration, stop, bench restriction");
}
