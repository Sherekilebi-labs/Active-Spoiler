// Exercise real N2 HTTP/UART handlers with real JSON parsing and stubbed hardware.
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <cassert>
#include <cstdio>
uint32_t fakeMillis=100;
HardwareSerial Serial(0);
TwoWire Wire(0);
FakeWiFi WiFi;
#include "../src/n2.cpp"
uint32_t rxSequence=0;
Operation operationFor(uint32_t id){for(const auto &entry:operations)if(entry.sequence==id)return entry;assert(false);return Operation{};}
uint32_t post(const char *route,const char *json){
  const uint32_t before=fakeMillis;
  server.arguments["plain"]=json;server.handlers.at(route)();
  assert(fakeMillis==before); // no acknowledgment wait/delay in any HTTP handler
  if(server.status!=202)return 0;
  JsonDocument reply;assert(!deserializeJson(reply,server.response));
  assert(reply["pending"].as<bool>());
  return reply["request"].as<uint32_t>();
}
void pollOperation(uint32_t id){server.arguments["request"]=std::to_string(id);server.handlers.at("/operation")();}
void acknowledge(Result result){
  const Ack ack={request.sequence,drive.epoch,uint8_t(result),request.action.kind,{0,0}};
  receive(packet(Message::Ack,++rxSequence,driveBoot,ack));
}
int main(){
  routes();driveBoot=7;drive.epoch=100;haveTelemetry=true;lastTelemetry=fakeMillis;mode=Mode::Bench;
  const uint32_t first=post("/motor","{\"operation\":\"test\",\"mask\":1}");
  assert(first && request.active && !operationFor(first).done);
  assert(actionTimeoutMs(request.action.kind)>PolarityTestTimeoutMs+PolarityTestSettleMs);
  pollOperation(first);assert(server.status==202);
  server.handlers.at("/status")();assert(server.status==200); // telemetry served while calibration is pending
  assert(!post("/arm","{\"enabled\":true}") && server.status==503);
  const uint32_t stopping=post("/arm","{\"enabled\":false}");
  assert(stopping && operationFor(first).result==Result::Cancelled);
  assert(post("/arm","{\"enabled\":false}")==stopping); // duplicate STOP shares its existing request
  pollOperation(first);assert(server.status==409 && server.response.find("cancelled")!=String::npos);
  acknowledge(Result::Ok);pollOperation(stopping);assert(server.status==200);
  pollOperation(first);assert(server.status==409); // STOP acknowledgment preserves cancellation result

  for(const char *operation:{"zero","polarity","direction"}){
    const String body=String("{\"operation\":\"")+operation+"\",\"mask\":1,\"value\":0}";
    const uint32_t id=post("/motor",body.c_str());assert(id && request.active);
    pollOperation(id);assert(server.status==202);
    acknowledge(Result::Ok);pollOperation(id);assert(server.status==200);
  }
  const uint32_t pid=post("/pid","{\"kp\":35,\"ki\":0.5,\"kd\":2.5,\"db\":0.7,\"minPwm\":350,\"enP\":true,\"enI\":false,\"enD\":true}");
  assert(pid && request.action.kind==uint8_t(ActionKind::Pid));
  const uint32_t stopPid=post("/arm","{\"enabled\":false}");
  assert(stopPid && operationFor(pid).result==Result::Cancelled); // STOP preempts every action, not just tests
  acknowledge(Result::Ok);

  manualMotors[0]=12;
  const uint32_t modeChange=post("/mode","{\"mode\":1}");assert(modeChange && mode==Mode::Bench);
  const uint32_t stopMode=post("/arm","{\"enabled\":false}");assert(stopMode);
  acknowledge(Result::Ok);assert(mode==Mode::Bench && manualMotors[0]==0);
  assert(operationFor(modeChange).result==Result::Cancelled); // cancelled mode changes never apply later
  const uint32_t failedMode=post("/mode","{\"mode\":1}");assert(failedMode);
  acknowledge(Result::Busy);assert(mode==Mode::Bench);
  const uint32_t acceptedMode=post("/mode","{\"mode\":1}");assert(acceptedMode && mode==Mode::Bench);
  acknowledge(Result::Ok);assert(mode==Mode::Wings);
  const uint32_t arm=post("/arm","{\"enabled\":true}");assert(arm);
  acknowledge(Result::Ok);pollOperation(arm);assert(server.status==200);

  mode=Mode::Bench;
  const uint32_t stalled=post("/motor","{\"operation\":\"test\",\"mask\":1}");assert(stalled);
  acknowledge(Result::NoMovement);pollOperation(stalled);
  assert(server.status==409 && server.response.find("5 seconds")!=String::npos);
  const uint32_t rebooted=post("/motor","{\"operation\":\"test\",\"mask\":1}");assert(rebooted);
  Telemetry reboot{};reboot.epoch=200;receive(packet(Message::Telemetry,1,8,reboot));rxSequence=1;
  pollOperation(rebooted);assert(server.status==409 && operationFor(rebooted).result==Result::Stale);
  server.arguments["request"]="1x";server.handlers.at("/operation")();assert(server.status==400);
  server.arguments["request"]="4294967296";server.handlers.at("/operation")();assert(server.status==404);

  for(unsigned i=0;i<OperationHistorySize;++i){assert(post("/arm","{\"enabled\":false}"));acknowledge(Result::Ok);}
  pollOperation(rebooted);assert(server.status==404); // history is bounded
  fakeMillis+=LinkTimeoutMs;assert(!post("/arm","{\"enabled\":true}"));
  assert(post("/arm","{\"enabled\":false}"));acknowledge(Result::Ok); // attempt STOP even with stale feedback

  // Coalesce local flash saves until editing pauses for a full second.
  fakeMillis=2000;lastTelemetry=fakeMillis;markSettingsDirty();
  fakeMillis=2500;markSettingsDirty();
  const size_t writes=preferences.writes;
  fakeMillis=3499;lastTelemetry=fakeMillis;loop();assert(preferences.writes==writes);
  lastTelemetry=fakeMillis;loop();assert(preferences.writes==writes+1 && !dirty);
  puts("PASS N2: all actions nonblocking, confirmation-only effects, STOP priority, bounded history, deferred saves");
}
