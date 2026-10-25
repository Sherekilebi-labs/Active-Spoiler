#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_system.h>
#include "hardware.h"
#include "protocol.h"
#include "dashboard.h"

using namespace ts;
namespace {
HardwareSerial uart(2);
WebServer server(80);
Preferences preferences;
portMUX_TYPE stateMux=portMUX_INITIALIZER_UNLOCKED;
struct Lock { Lock(){portENTER_CRITICAL(&stateMux);} ~Lock(){portEXIT_CRITICAL(&stateMux);} };
struct Settings {
  uint32_t version=1;
  Curve curves[Presets];
  float rollOffset=0,pitchOffset=0;
  uint8_t preset=0,reserved[3]={};
} settings;
struct Sensor {
  float accel[3]={},gyro[3]={},temperature=0,roll=0,pitch=0;
  bool healthy=false;
  uint32_t errors=0,sampled=0;
} sensor;
struct Request {
  bool active=false,done=false;
  Action action{};
  uint32_t sequence=0,lastSent=0,started=0;
  Result result=Result::Timeout;
  int8_t nextMode=-1;
  bool clearManual=false;
} request;
struct Operation {
  uint32_t sequence=0;
  bool done=false;
  Result result=Result::Timeout;
  uint8_t kind=0;
};
constexpr unsigned OperationHistorySize=8;
Operation operations[OperationHistorySize];
unsigned nextOperation=0;
Telemetry drive{};
DriveConfig driveConfig;
bool haveConfig=false,haveTelemetry=false,dirty=true,internalStop=false;
Mode mode=Mode::Automatic;
float manualWings[2]={},manualMotors[Motors]={},computedWings[2]={},computedTargets[Motors]={};
uint32_t bootId,driveBoot=0,lastTelemetry=0,lastRx=0,txSequence=0,lastMutation=0;
uint32_t lastSettingsChange=0;
bool haveRx=false;
uint32_t controlMaxUs=0,controlMissed=0,uartErrors=0;
Parser parser;
uint8_t mpuAddress=0x68;

// Caller holds stateMux. Completion is handled on the UART task; HTTP never
// waits for the other board. Retain results when STOP supersedes a request.
void finishRequest(Result result) {
  request.result=result; request.done=true; request.active=false;
  for(auto &operation:operations) if(operation.sequence==request.sequence) {
    operation.result=result; operation.done=true;
  }
  if(result!=Result::Ok) return;
  if(request.nextMode>=0) mode=Mode(request.nextMode);
  if(request.clearManual) {
    std::memset(manualWings,0,sizeof(manualWings)); std::memset(manualMotors,0,sizeof(manualMotors));
  }
  lastMutation=millis();
}
void markSettingsDirty() {dirty=true; lastSettingsChange=millis();} // caller holds stateMux

bool linkHealthy(uint32_t now);
uint32_t queueRemoteLocked(Action action,int nextMode=-1,bool clearManual=false) {
  const bool stop=action.kind==uint8_t(ActionKind::Stop);
  if(!linkHealthy(millis()) && !(stop && driveBoot)) return 0;
  if(request.active) {
    if(!stop) return 0;
    if(request.action.kind==uint8_t(ActionKind::Stop) && request.nextMode==nextMode && request.clearManual==clearManual)
      return request.sequence; // repeated STOP clicks share one operation
    finishRequest(Result::Cancelled);
  }
  request=Request{}; request.active=true; request.sequence=++txSequence; request.started=millis();
  if(!request.sequence) request.sequence=++txSequence;
  action.receiverBoot=driveBoot; action.epoch=drive.epoch; request.action=action;
  request.nextMode=nextMode; request.clearManual=clearManual;
  Operation &operation=operations[nextOperation]; operation=Operation{};
  operation.sequence=request.sequence; operation.kind=action.kind;
  nextOperation=(nextOperation+1)%OperationHistorySize;
  return request.sequence;
}

bool validSettings(const Settings &s) {
  if(s.version!=1 || s.preset>=Presets || !std::isfinite(s.rollOffset) || !std::isfinite(s.pitchOffset) ||
     std::fabs(s.rollOffset)>180 || std::fabs(s.pitchOffset)>180) return false;
  for(const auto &c:s.curves) for(unsigned i=0;i<Points;++i)
    if(!std::isfinite(c.rollRight[i]) || c.rollRight[i]<0 || c.rollRight[i]>WingLimit ||
       !std::isfinite(c.pitchBoth[i]) || c.pitchBoth[i]<0 || c.pitchBoth[i]>WingLimit) return false;
  return true;
}
bool linkHealthy(uint32_t now) { return haveTelemetry && uint32_t(now-lastTelemetry)<LinkTimeoutMs; }
void sendFrame(const Frame &f) {uint8_t bytes[MaxEncoded]; const size_t n=encode(f,bytes); uart.write(bytes,n);}
bool readRegister(uint8_t reg,uint8_t *bytes,unsigned count) {
  Wire.beginTransmission(mpuAddress); Wire.write(reg);
  if(Wire.endTransmission(false)!=0 || Wire.requestFrom(mpuAddress,uint8_t(count))!=count) return false;
  for(unsigned i=0;i<count;++i) bytes[i]=Wire.read();
  return true;
}
bool writeRegister(uint8_t reg,uint8_t value) {
  Wire.beginTransmission(mpuAddress); Wire.write(reg); Wire.write(value); return Wire.endTransmission()==0;
}
bool initializeMpu() {
  uint8_t id=0;
  bool found=false;
  for(uint8_t address:{uint8_t(0x68),uint8_t(0x69)}) {
    mpuAddress=address;
    if(readRegister(0x75,&id,1) && (id&0x7e)==0x68) {found=true; break;}
  }
  if(!found) return false;
  // Same ranges/filter as Dynamic spoiler: 8 g, 500 deg/s, 5 Hz DLPF.
  if(!writeRegister(0x6b,0x01)) return false;
  vTaskDelay(pdMS_TO_TICKS(20));
  return writeRegister(0x1c,0x10) && writeRegister(0x1b,0x08) &&
         writeRegister(0x1a,0x06) && writeRegister(0x19,9);
}
bool sampleMpu(Sensor &s) {
  uint8_t bytes[14];
  if(!readRegister(0x3b,bytes,sizeof(bytes))) return false;
  auto signed16=[&](unsigned offset) {return int16_t((uint16_t(bytes[offset])<<8)|bytes[offset+1]);};
  for(unsigned i=0;i<3;++i) {
    s.accel[i]=signed16(i*2)*(9.80665f/4096);
    s.gyro[i]=signed16(8+i*2)/65.5f;
  }
  s.temperature=signed16(6)/340.0f+36.53f;
  s.roll=std::atan2(-s.accel[0],std::hypot(s.accel[1],s.accel[2]))*180/Pi;
  s.pitch=std::atan2(s.accel[1],s.accel[2])*180/Pi;
  s.sampled=millis(); s.healthy=true;
  return std::isfinite(s.roll) && std::isfinite(s.pitch);
}
void receive(const Frame &frame) {
  if(!frame.session || (frame.type!=Message::Telemetry && frame.type!=Message::Config && frame.type!=Message::Ack)) return;
  Lock lock;
  if(frame.session!=driveBoot) {
    driveBoot=frame.session; haveRx=false; haveConfig=false; haveTelemetry=false;
    if(request.active) finishRequest(Result::Stale);
  }
  if(haveRx && !newer(frame.sequence,lastRx)) return;
  lastRx=frame.sequence; haveRx=true;
  if(frame.type==Message::Telemetry) {
    Telemetry t; if(!unpack(frame,t)) return;
    drive=t; lastTelemetry=millis(); haveTelemetry=true;
    if(t.armed && (uint8_t(mode)!=t.mode || (mode==Mode::Automatic && !sensor.healthy))) internalStop=true;
  } else if(frame.type==Message::Config) {
    DriveConfig c; if(unpack(frame,c) && validConfig(c)) {driveConfig=c; haveConfig=true;}
  } else {
    Ack ack;
    if(unpack(frame,ack) && request.active && ack.request==request.sequence && ack.kind==request.action.kind) {
      drive.epoch=ack.epoch; finishRequest(Result(ack.result));
    }
  }
}
void controlTask(void*) {
  Wire.begin(MpuSda,MpuScl,400000); Wire.setTimeOut(2);
  bool initialized=initializeMpu();
  uint32_t lastSample=0,lastSend=0,lastInit=millis(),previous=micros();
  TickType_t wake=xTaskGetTickCount();
  for(;;) {
    vTaskDelayUntil(&wake,pdMS_TO_TICKS(1));
    const uint32_t start=micros(),now=millis();
    Frame frame; unsigned budget=768;
    while(budget-- && uart.available()) if(parser.feed(uint8_t(uart.read()),frame)) receive(frame);
    if(uint32_t(now-lastSample)>=10) {
      lastSample=now; Sensor next; {Lock lock; next=sensor;}
      if(!initialized || !sampleMpu(next)) {
        next.healthy=false; ++next.errors;
        if(initialized) initialized=false;
      }
      {Lock lock; sensor=next; if(!next.healthy && mode==Mode::Automatic && drive.armed) internalStop=true;}
    }
    if(!initialized && uint32_t(now-lastInit)>=1000) {lastInit=now; initialized=initializeMpu();}
    if(uint32_t(now-lastSend)>=20) {
      lastSend=now;
      Curve curve; Sensor sensed; Mode activeMode; float wings[2],motors[Motors],offsetRoll,offsetPitch;
      uint32_t receiver,epoch;
      {Lock lock;
        curve=settings.curves[settings.preset]; sensed=sensor; activeMode=mode;
        offsetRoll=settings.rollOffset; offsetPitch=settings.pitchOffset;
        std::memcpy(wings,manualWings,sizeof(wings)); std::memcpy(motors,manualMotors,sizeof(motors));
        receiver=driveBoot; epoch=drive.epoch;
      }
      bool valid=true;
      if(activeMode==Mode::Automatic) {
        valid=sensed.healthy;
        mapWings(curve,sensed.roll-offsetRoll,sensed.pitch-offsetPitch,wings[0],wings[1]);
      }
      if(activeMode!=Mode::Bench) for(unsigned i=0;i<2;++i)
        valid=inverseKinematics(wings[i],motors[i*2],motors[i*2+1]) && valid;
      valid=valid && validTargets(activeMode,motors);
      if(!valid) {std::memset(motors,0,sizeof(motors)); std::memset(wings,0,sizeof(wings));}
      uint32_t seq;
      {Lock lock;
        std::memcpy(computedTargets,motors,sizeof(motors)); std::memcpy(computedWings,wings,sizeof(wings));
        if(!valid && drive.armed) internalStop=true;
        seq=++txSequence;
      }
      if(receiver) {
        Control c{}; c.receiverBoot=receiver; c.epoch=epoch; c.mode=uint8_t(activeMode);
        std::memcpy(c.target,motors,sizeof(motors));
        sendFrame(packet(Message::Control,seq,bootId,c));
      }
    }
    Request outgoing; bool send=false;
    {Lock lock;
      if(internalStop && driveBoot) {
        Action stop{}; stop.kind=uint8_t(ActionKind::Stop);
        queueRemoteLocked(stop,-1,true); internalStop=false;
      }
      if(request.active && uint32_t(now-request.started)>actionTimeoutMs(request.action.kind)) {
        if(request.action.kind==uint8_t(ActionKind::Arm) || request.action.kind==uint8_t(ActionKind::Test)) internalStop=true;
        finishRequest(Result::Timeout);
      }
      if(request.active && (!request.lastSent || uint32_t(now-request.lastSent)>=100)) {
        request.lastSent=now; outgoing=request; send=true;
      }
      uartErrors=parser.errors;
      const uint32_t duration=micros()-start;
      if(duration>controlMaxUs) controlMaxUs=duration;
      if(start-previous>1500) ++controlMissed;
    }
    previous=start;
    if(send) sendFrame(packet(Message::Action,outgoing.sequence,bootId,outgoing.action));
    if(micros()-start>=1000) wake=xTaskGetTickCount();
  }
}

void jsonResponse(JsonDocument &doc,int code=200) {
  String body; serializeJson(doc,body); server.send(code,"application/json",body);
}
void error(const char *message,int code=400) {JsonDocument doc; doc["error"]=message; jsonResponse(doc,code);}
bool body(JsonDocument &doc) {
  if(!server.hasArg("plain") || server.arg("plain").length()>8192 || deserializeJson(doc,server.arg("plain")) || !doc.is<JsonObject>()) {
    error("Expected a JSON object (maximum 8192 bytes)"); return false;
  }
  return true;
}
bool integer(JsonVariantConst value,int lo,int hi,int &out) {
  if(!value.is<int>()) return false;
  out=value.as<int>(); return out>=lo && out<=hi;
}
bool number(JsonVariantConst value,float lo,float hi,float &out) {
  if(!value.is<float>()) return false;
  out=value.as<float>(); return std::isfinite(out) && out>=lo && out<=hi;
}
const char *resultName(Result result) {
  switch(result) {
    case Result::Ok:return "Applied by N1";
    case Result::Invalid:return "Invalid operation or mode";
    case Result::Stale:return "Controller state changed; refresh and retry";
    case Result::Unhealthy:return "Healthy encoders, fresh UART, and saved zeros are required to arm";
    case Result::Busy:return "N1 is busy saving or calibrating";
    case Result::NoMovement:return "Motor did not travel 5 degrees within 5 seconds; polarity unchanged. Check power, enables, or friction";
    case Result::Cancelled:return "Action cancelled by STOP ALL";
    default:return "N1 acknowledgment timed out";
  }
}
uint32_t queueRemote(Action action,int nextMode=-1,bool clearManual=false) {
  Lock lock; return queueRemoteLocked(action,nextMode,clearManual);
}
void remote(Action action,int nextMode=-1,bool clearManual=false) {
  const uint32_t sequence=queueRemote(action,nextMode,clearManual);
  if(!sequence) {error("UART unavailable or another command is pending",503); return;}
  JsonDocument out; out["pending"]=true; out["request"]=sequence;
  out["timeoutMs"]=actionTimeoutMs(action.kind); out["kind"]=action.kind;
  jsonResponse(out,202);
}
void ok() {JsonDocument d; d["ok"]=true; jsonResponse(d);}
void pidJson(JsonObject out,const PidConfig &p) {
  out["kp"]=p.kp; out["ki"]=p.ki; out["kd"]=p.kd;
  out["db"]=p.deadband; out["minPwm"]=p.minPwm;
  out["enP"]=bool(p.enP); out["enI"]=bool(p.enI); out["enD"]=bool(p.enD);
}
void configResponse() {
  DriveConfig c; bool available; {Lock lock; c=driveConfig; available=haveConfig;}
  JsonDocument d; d["available"]=available; pidJson(d["pid"].to<JsonObject>(),c.pid);
  JsonArray motors=d["motors"].to<JsonArray>();
  for(const auto &m:c.motor) {
    JsonObject entry=motors.add<JsonObject>(); entry["zeroed"]=bool(m.hasZero);
    entry["outputReversed"]=bool(m.reverseOutput); entry["angleReversed"]=bool(m.reverseAngle);
  }
  jsonResponse(d);
}
void statusResponse() {
  Telemetry t; Sensor s; Settings settingsCopy; Mode current; bool fresh,pendingSave,waiting,available;
  float wings[2],target[Motors]; uint32_t parserErrors,maxUs,missed;
  {Lock lock;
    t=drive; s=sensor; settingsCopy=settings; current=mode; fresh=linkHealthy(millis());
    pendingSave=dirty; waiting=request.active; available=haveConfig;
    std::memcpy(wings,computedWings,sizeof(wings)); std::memcpy(target,computedTargets,sizeof(target));
    parserErrors=uartErrors; maxUs=controlMaxUs; missed=controlMissed;
  }
  JsonDocument d;
  d["roll"]=s.roll; d["pitch"]=s.pitch;
  d["calibratedRoll"]=s.roll-settingsCopy.rollOffset; d["calibratedPitch"]=s.pitch-settingsCopy.pitchOffset;
  d["rollOffset"]=settingsCopy.rollOffset; d["pitchOffset"]=settingsCopy.pitchOffset;
  d["mpuHealthy"]=s.healthy; d["temperature"]=s.temperature; d["mpuErrors"]=s.errors;
  d["sensorAgeMs"]=s.sampled ? uint32_t(millis()-s.sampled) : UINT32_MAX;
  JsonArray accel=d["accel"].to<JsonArray>(),gyro=d["gyro"].to<JsonArray>();
  for(unsigned i=0;i<3;++i) {accel.add(s.accel[i]); gyro.add(s.gyro[i]);}
  d["activePreset"]=settingsCopy.preset; d["mode"]=uint8_t(current);
  d["leftWing"]=wings[0]; d["rightWing"]=wings[1];
  d["linkHealthy"]=fresh; d["armed"]=fresh && bool(t.armed); d["faults"]=t.faults;
  d["pendingSave"]=pendingSave || bool(t.pendingSave); d["busy"]=waiting || bool(t.busy);
  d["configAvailable"]=available; d["configRevision"]=t.configRevision;
  d["uartErrors"]=parserErrors; d["n1UartErrors"]=t.uartErrors; d["controlMaxUs"]=maxUs; d["controlMissed"]=missed;
  JsonArray motors=d["motors"].to<JsonArray>();
  for(unsigned i=0;i<Motors;++i) {
    JsonObject m=motors.add<JsonObject>(); const bool valid=fresh && (t.validMask&(1<<i));
    m["valid"]=valid; m["command"]=target[i];
    if(valid) {m["angle"]=t.angle[i]; m["raw"]=t.raw[i]; m["error"]=t.target[i]-t.angle[i]; m["pwm"]=t.pwm[i]; m["target"]=t.target[i];}
    else {m["angle"]=nullptr; m["raw"]=nullptr; m["error"]=nullptr; m["pwm"]=nullptr; m["target"]=nullptr;}
    m["sensorErrors"]=t.sensorErrors[i];
  }
  JsonArray timings=d["pairMaxUs"].to<JsonArray>(),missedArray=d["pairMissed"].to<JsonArray>();
  for(unsigned i=0;i<2;++i) {timings.add(t.maxCycleUs[i]); missedArray.add(t.missed[i]);}
  jsonResponse(d);
}
void presetResponse(unsigned preset) {
  Curve c; {Lock lock; c=settings.curves[preset];}
  JsonDocument d;
  JsonArray rIn=d["rollInput"].to<JsonArray>(),pIn=d["pitchInput"].to<JsonArray>();
  JsonArray rR=d["rollToRight"].to<JsonArray>(),rL=d["rollToLeft"].to<JsonArray>();
  JsonArray pR=d["pitchToRight"].to<JsonArray>(),pL=d["pitchToLeft"].to<JsonArray>();
  for(unsigned i=0;i<Points;++i) {
    rIn.add(int(i)*9-90); pIn.add(int(i)*9-90);
    rR.add(c.rollRight[i]); rL.add(c.rollRight[Points-1-i]); pR.add(c.pitchBoth[i]); pL.add(c.pitchBoth[i]);
  }
  d["preset"]=preset; jsonResponse(d);
}
void routes() {
  server.on("/",HTTP_GET,[]{server.send_P(200,"text/html; charset=utf-8",DASHBOARD);});
  server.on("/status",HTTP_GET,statusResponse);
  server.on("/config",HTTP_GET,configResponse);
  server.on("/operation",HTTP_GET,[]{
    const String value=server.arg("request"); uint64_t id=0;
    if(!value.length() || value.length()>10) {error("Invalid request ID"); return;}
    for(unsigned i=0;i<value.length();++i) {
      if(value[i]<'0' || value[i]>'9') {error("Invalid request ID"); return;}
      id=id*10+unsigned(value[i]-'0');
    }
    Operation operation;
    {Lock lock; for(const auto &entry:operations) if(entry.sequence==id && id) {operation=entry; break;}}
    if(!id || id>UINT32_MAX || operation.sequence!=id) {error("Unknown or expired action",404); return;}
    if(operation.done && operation.result!=Result::Ok) {
      error(resultName(operation.result),operation.result==Result::Timeout ? 504 : 409); return;
    }
    JsonDocument d; d["request"]=operation.sequence; d["pending"]=!operation.done; d["kind"]=operation.kind;
    if(operation.done) d["ok"]=true;
    jsonResponse(d,operation.done ? 200 : 202);
  });
  server.on("/preset",HTTP_GET,[]{
    const String value=server.arg("p");
    if(value.length()!=1 || value[0]<'0' || value[0]>'2') {error("Preset must be 0, 1, or 2"); return;}
    presetResponse(unsigned(value[0]-'0'));
  });
  server.on("/set",HTTP_POST,[]{
    JsonDocument d; if(!body(d)) return; int p,index; float value;
    if(!integer(d["preset"],0,2,p) || !integer(d["index"],0,20,index) || !number(d["value"],0,70,value) || !d["field"].is<const char*>()) {
      error("Invalid preset, index, field, or wing angle (0–70 degrees)"); return;
    }
    const String field=d["field"].as<const char*>(); value=roundf(value*10)/10;
    if(field!="rollToRight" && field!="rollToLeft" && field!="pitchToRight" && field!="pitchToLeft") {error("Unknown or fixed input field"); return;}
    {Lock lock;
      if(field=="rollToRight") settings.curves[p].rollRight[index]=value;
      else if(field=="rollToLeft") settings.curves[p].rollRight[20-index]=value;
      else settings.curves[p].pitchBoth[index]=value;
      markSettingsDirty();
    }
    JsonDocument out; out["ok"]=true; out["value"]=value; out["pendingSave"]=true; jsonResponse(out);
  });
  for(const char *path:{"/save","/use","/reset"}) {
    const String route=path;
    server.on(path,HTTP_POST,[route]{
      JsonDocument d; if(!body(d)) return; int preset;
      if(!integer(d["preset"],0,2,preset)) {error("Invalid preset"); return;}
      {Lock lock;
        if(route=="/use") settings.preset=preset;
        if(route=="/reset") settings.curves[preset]=defaultCurve();
        markSettingsDirty();
      }
      ok();
    });
  }
  server.on("/calibrate",HTTP_POST,[]{
    bool healthy; {Lock lock;
      healthy=sensor.healthy;
      if(healthy) {settings.rollOffset=sensor.roll; settings.pitchOffset=sensor.pitch; markSettingsDirty();}
    }
    if(!healthy) {error("MPU6050 is unavailable",409); return;} ok();
  });
  server.on("/mode",HTTP_POST,[]{
    JsonDocument d; if(!body(d)) return; int selected;
    if(!integer(d["mode"],0,2,selected)) {error("Mode must be 0, 1, or 2"); return;}
    Action a{}; a.kind=uint8_t(ActionKind::Stop); remote(a,selected,true);
  });
  server.on("/manual",HTTP_POST,[]{
    JsonDocument d; if(!body(d)) return;
    Mode current; {Lock lock; current=mode;}
    if(current==Mode::Automatic) {error("Choose a manual mode first",409); return;}
    float values[Motors]={}; const unsigned count=current==Mode::Wings ? 2 : Motors;
    if(!d["angles"].is<JsonArray>() || d["angles"].size()!=count) {error("Incorrect number of angles"); return;}
    for(unsigned i=0;i<count;++i) if(!number(d["angles"][i],current==Mode::Wings ? 0 : -45,current==Mode::Wings ? 70 : 45,values[i])) {error("Angle outside mode limits"); return;}
    {Lock lock; if(current==Mode::Wings) std::memcpy(manualWings,values,sizeof(manualWings)); else std::memcpy(manualMotors,values,sizeof(manualMotors));}
    ok();
  });
  server.on("/arm",HTTP_POST,[]{
    JsonDocument d; if(!body(d)) return;
    if(!d["enabled"].is<bool>()) {error("enabled must be boolean"); return;}
    const bool enabled=d["enabled"].as<bool>(); bool ready;
    {Lock lock; ready=mode!=Mode::Automatic || sensor.healthy;}
    if(enabled && !ready) {error("Automatic mode requires a healthy MPU6050",409); return;}
    Action a{}; a.kind=uint8_t(enabled ? ActionKind::Arm : ActionKind::Stop);
    remote(a,-1,!enabled);
  });
  server.on("/motor",HTTP_POST,[]{
    JsonDocument d; if(!body(d)) return; int mask;
    if(!integer(d["mask"],1,15,mask) || !d["operation"].is<const char*>()) {error("Invalid motor mask or operation"); return;}
    Action a{}; a.mask=mask; const String operation=d["operation"].as<const char*>();
    if(operation=="zero") a.kind=uint8_t(ActionKind::Zero);
    else if(operation=="test") a.kind=uint8_t(ActionKind::Test);
    else if(operation=="polarity" || operation=="direction") {
      int value; if(!integer(d["value"],0,15,value) || (value&~mask)) {error("Direction value must be a subset of mask"); return;}
      a.kind=uint8_t(operation=="polarity" ? ActionKind::Polarity : ActionKind::Direction); a.value=value;
    } else {error("Unknown motor operation"); return;}
    remote(a,-1,true);
  });
  server.on("/pid",HTTP_POST,[]{
    JsonDocument d; if(!body(d)) return;
    Action a{}; a.kind=uint8_t(ActionKind::Pid); a.mask=15; int minimum;
    if(!number(d["kp"],0,1000,a.pid.kp) || !number(d["ki"],0,1000,a.pid.ki) || !number(d["kd"],0,1000,a.pid.kd) ||
       !number(d["db"],0,10,a.pid.deadband) || !integer(d["minPwm"],0,4095,minimum) ||
       !d["enP"].is<bool>() || !d["enI"].is<bool>() || !d["enD"].is<bool>()) {error("Invalid PID settings"); return;}
    a.pid.minPwm=minimum; a.pid.enP=d["enP"]; a.pid.enI=d["enI"]; a.pid.enD=d["enD"];
    remote(a);
  });
  server.onNotFound([]{error("Not found",404);});
  server.begin();
}
} // namespace

void setup() {
  Serial.begin(115200); bootId=esp_random(); if(!bootId) bootId=1;
  for(auto &curve:settings.curves) curve=defaultCurve();
  preferences.begin("ts-ui",false);
  Settings stored;
  if(preferences.getBytesLength("settings")==sizeof(stored)) {
    preferences.getBytes("settings",&stored,sizeof(stored));
    if(validSettings(stored)) {settings=stored; dirty=false;}
  }
  uart.setRxBufferSize(4096); uart.setTxBufferSize(1024);
  uart.begin(UartBaud,SERIAL_8N1,UartRx,UartTx);
  if(xTaskCreatePinnedToCore(controlTask,"mpu-uart-ik",8192,nullptr,3,nullptr,1)!=pdPASS) {
    Serial.println("Control task creation failed; motor arming unavailable");
  }
  WiFi.mode(WIFI_AP); WiFi.setSleep(false);
  WiFi.softAP("SpoilerTuner","spoiler123");
  routes(); Serial.print("Tsintsadze N2 dashboard: http://"); Serial.println(WiFi.softAPIP());
}
void loop() {
  server.handleClient();
  Settings save; bool doSave=false;
  {Lock lock;
    if(dirty && linkHealthy(millis()) && !drive.armed && !drive.busy && !request.active &&
       uint32_t(millis()-lastMutation)>500 && uint32_t(millis()-lastSettingsChange)>=1000) {
      save=settings; doSave=true;
    }
  }
  if(doSave) {
    const bool saved=preferences.putBytes("settings",&save,sizeof(save))==sizeof(save);
    if(saved) {Lock lock; dirty=false;}
  }
  delay(1);
}
