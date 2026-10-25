#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>
#include <esp_system.h>
#include "hardware.h"
#include "protocol.h"

using namespace ts;
namespace {
HardwareSerial uart(2);
TwoWire buses[2]={TwoWire(0),TwoWire(1)};
Preferences preferences;
portMUX_TYPE stateMux=portMUX_INITIALIZER_UNLOCKED;
struct Lock { Lock(){portENTER_CRITICAL(&stateMux);} ~Lock(){portEXIT_CRITICAL(&stateMux);} };
DriveConfig config;
SafetyGate gate;
Telemetry telemetry{};
float targets[Motors]={};
Mode mode=Mode::Automatic;
uint32_t bootId,txSequence=0,revision=1;
bool dirty=false,saving=false;
struct Pending {
  bool active=false;
  Action action{};
  uint32_t request=0,sender=0;
  uint8_t done=0;
  Result result=Result::Ok;
} pending;
Ack cachedAck{};
uint32_t lastAction=0,lastActionSession=0;
bool haveAction=false;
Parser parser;

void sendFrame(const Frame &frame) {
  uint8_t bytes[MaxEncoded]; const size_t n=encode(frame,bytes);
  uart.write(bytes,n);
}
void sendAck(uint32_t request,ActionKind kind,Result result) {
  uint32_t epoch; {Lock lock; epoch=gate.epoch;}
  const Ack out={request,epoch,uint8_t(result),uint8_t(kind),{0,0}};
  if(haveAction && lastAction==request) cachedAck=out;
  sendFrame(packet(Message::Ack,++txSequence,bootId,out));
}
void fail(uint32_t fault) {
  // Caller holds stateMux. A persistent failed sensor must not advance the
  // epoch continuously; recovery still requires an explicit Arm action.
  if(gate.armed || !(gate.faults&fault)) gate.stop(fault);
  if(pending.active && pending.action.kind==uint8_t(ActionKind::Test)) pending.result=Result::Unhealthy;
}
void writeMotor(unsigned motor,int pwm) {
  pwm=clamp(pwm,-MaxPwm,MaxPwm);
  // Clear the inactive half first so reversing never momentarily drives both.
  if(pwm>=0) { ledcWrite(motor*2+1,0); ledcWrite(motor*2,pwm); }
  else { ledcWrite(motor*2,0); ledcWrite(motor*2+1,-pwm); }
}
bool readEncoder(TwoWire &bus,uint8_t address,uint16_t &raw) {
  bus.beginTransmission(address); bus.write(0x03);
  if(bus.endTransmission(false)!=0 || bus.requestFrom(address,uint8_t(2))!=2) return false;
  const uint8_t hi=bus.read(),lo=bus.read();
  raw=(uint16_t(hi)<<6)|(lo>>2); return true;
}

void motorPairTask(void *argument) {
  const unsigned pair=uintptr_t(argument),first=pair*2;
  const uint8_t pairMask=uint8_t(3<<first);
  TwoWire &bus=buses[pair];
  bool busReady=bus.begin(EncoderSda[pair],EncoderScl[pair],400000);
  bus.setTimeOut(2);
  for(unsigned i=first;i<first+2;++i) {
    ledcSetup(i*2,PwmFrequency,PwmResolution); ledcSetup(i*2+1,PwmFrequency,PwmResolution);
    ledcAttachPin(PwmPins[i][0],i*2); ledcAttachPin(PwmPins[i][1],i*2+1); writeMotor(i,0);
  }
  Encoder encoder[2]; Pid pid[2]; Calibration applied[2];
  uint32_t localRevision=0,lastUs=micros(),testRequest=0;
  int testMotor=-1;
  PolarityTest test;
  TickType_t wake=xTaskGetTickCount();
  for(;;) {
    vTaskDelayUntil(&wake,pdMS_TO_TICKS(1));
    const uint32_t started=micros(),elapsed=started-lastUs; lastUs=started;
    DriveConfig cfg; float wanted[Motors]; uint32_t rev; Pending job; bool armed;
    {Lock lock;
      gate.poll(millis()); cfg=config; rev=revision; job=pending; armed=gate.armed;
      std::memcpy(wanted,targets,sizeof(wanted));
      if(elapsed>1500) ++telemetry.missed[pair];
    }
    uint16_t raw[2]={}; bool valid[2]={}; float measured[2]={};
    for(unsigned j=0;j<2;++j) {
      const unsigned i=first+j;
      valid[j]=busReady && readEncoder(bus,EncoderAddress[j],raw[j]);
      if(!valid[j]) {
        writeMotor(i,0);
        {Lock lock;
          telemetry.validMask&=~(1<<i); telemetry.pwm[i]=0; ++telemetry.sensorErrors[i]; fail(EncoderFault);
          if(pending.active && pending.action.kind==uint8_t(ActionKind::Test)) pending.done|=pending.action.mask&pairMask;
        }
        continue;
      }
      if(!encoder[j].initialized || applied[j].zero!=cfg.motor[i].zero || applied[j].hasZero!=cfg.motor[i].hasZero)
        encoder[j].reset(raw[j],cfg.motor[i]);
      else encoder[j].update(raw[j]);
      applied[j]=cfg.motor[i]; measured[j]=encoder[j].angle(cfg.motor[i]);
      if(localRevision!=rev) pid[j].reset(measured[j]);
    }
    localRevision=rev;

    const uint8_t selected=job.action.mask&pairMask;
    if(job.active && selected && (job.done&selected)!=selected) {
      const auto kind=ActionKind(job.action.kind);
      if(kind==ActionKind::Zero) {
        {Lock lock;
          if(pending.active && pending.request==job.request) {
            for(unsigned j=0;j<2;++j) if(selected&(1<<(first+j))) {
              if(!valid[j]) {pending.result=Result::Unhealthy; continue;}
              const unsigned i=first+j;
              config.motor[i].zero=raw[j]; config.motor[i].hasZero=1;
              encoder[j].zero=encoder[j].continuous;
              cfg.motor[i]=config.motor[i]; applied[j]=cfg.motor[i]; measured[j]=0; pid[j].reset(0);
            }
            ++revision; dirty=true; pending.done|=selected;
          }
        }
      } else if(kind==ActionKind::Test) {
        if(testRequest!=job.request) {
          testRequest=job.request;
          for(unsigned j=0;j<2;++j) if(selected&(1<<(first+j))) {
            const unsigned i=first+j;
            if(valid[j] && test.begin(measured[j],millis(),cfg.motor[i])) {
              testMotor=int(i);
              Lock lock;
              if(pending.active && pending.request==testRequest) targets[i]=test.target;
            } else {
              testMotor=-1;
              Lock lock;
              if(pending.active && pending.request==testRequest) {
                pending.result=valid[j] ? Result::Invalid : Result::Unhealthy;
                pending.done|=uint8_t(1<<i);
              }
            }
          }
        }
      } else {Lock lock; if(pending.active && pending.request==job.request) pending.done|=selected;}
    }

    for(unsigned j=0;j<2;++j) {
      const unsigned i=first+j; int physical=0;
      if(valid[j]) {
        bool enabled=false,testAllowed=false;
        {Lock lock;
          enabled=armed && gate.armed && gate.fresh(millis()) && !gate.faults && telemetry.validMask==15;
          testAllowed=pending.active && pending.request==testRequest && pending.result==Result::Ok &&
            gate.fresh(millis()) && telemetry.validMask==15 && !gate.armed;
        }
        const int logical=pid[j].step(wanted[i],measured[j],elapsed*0.000001f,enabled,cfg.pid);
        physical=physicalPwm(logical,cfg.motor[i]);
        if(int(i)==testMotor) {
          test.update(measured[j],millis(),testAllowed);
          physical=test.pwm();
          if(test.finished()) {
            {Lock lock;
              if(pending.active && pending.request==testRequest) {
                if(test.state==PolarityTest::State::Aborted) pending.result=Result::Unhealthy;
                else if(test.state==PolarityTest::State::TimedOut) pending.result=Result::NoMovement;
                else { config.motor[i].reverseOutput=test.reverseOutput; ++revision; dirty=true; }
                targets[i]=0;
                pending.done|=uint8_t(1<<i);
              }
            }
            testMotor=-1;
          }
        }
      }
      // Recheck the shared fault immediately before each ordinary drive write.
      {Lock lock;
        if((!gate.armed || gate.faults) && int(i)!=testMotor) physical=0;
        if(int(i)==testMotor && (!pending.active || pending.request!=testRequest ||
           !gate.fresh(millis()) || pending.result!=Result::Ok)) physical=0;
      }
      writeMotor(i,physical);
      {Lock lock;
        if(valid[j]) {telemetry.angle[i]=measured[j]; telemetry.raw[i]=raw[j]*(360.0f/16384); telemetry.validMask|=1<<i;}
        telemetry.pwm[i]=physical;
      }
    }
    const uint32_t duration=micros()-started;
    {Lock lock; if(duration>telemetry.maxCycleUs[pair]) telemetry.maxCycleUs[pair]=duration;}
    if(duration>=1000) wake=xTaskGetTickCount(); // no burst of catch-up reads after an I2C timeout
  }
}

void handleAction(const Frame &frame,const Action &a) {
  if(haveAction && lastActionSession==frame.session && frame.sequence==lastAction) {
    bool busy; {Lock lock; busy=pending.active;}
    if(!busy) sendFrame(packet(Message::Ack,++txSequence,bootId,cachedAck));
    return;
  }
  const auto kind=ActionKind(a.kind);
  Result result=Result::Ok; bool immediate=true;
  {Lock lock;
    if(a.receiverBoot!=bootId || frame.session!=gate.session || (a.epoch!=gate.epoch && kind!=ActionKind::Stop) ||
       (haveAction && lastActionSession==frame.session && !newer(frame.sequence,lastAction))) result=Result::Stale;
    else if(saving || (pending.active && kind!=ActionKind::Stop)) result=Result::Busy;
    else if(kind==ActionKind::Stop) {
      gate.stop(); pending.active=false;
      std::memset(targets,0,sizeof(targets));
    } else if(kind==ActionKind::Arm) {
      bool calibrated=true; for(const auto &m:config.motor) calibrated &= bool(m.hasZero);
      if(!gate.arm(a.epoch,millis(),telemetry.validMask==15 && calibrated)) result=Result::Unhealthy;
    } else if(kind<ActionKind::Zero || kind>ActionKind::Pid || !a.mask || a.mask>15 ||
              ((kind==ActionKind::Direction || kind==ActionKind::Polarity) && (a.value&~a.mask)) ||
              (kind==ActionKind::Pid && !validPid(a.pid))) result=Result::Invalid;
    else if(!gate.fresh(millis()) || telemetry.validMask!=15) result=Result::Unhealthy;
    else if(kind==ActionKind::Test && (mode!=Mode::Bench || (a.mask&(a.mask-1)))) result=Result::Invalid;
    else {
      gate.stop();
      // This explicit, valid action starts a bounded disarmed operation; new
      // controls must use the new epoch to keep its heartbeat alive.
      gate.haveControl=true; gate.lastControl=millis();
      std::memset(targets,0,sizeof(targets));
      if(kind==ActionKind::Pid) config.pid=a.pid;
      for(unsigned i=0;i<Motors;++i) if(a.mask&(1<<i)) {
        if(kind==ActionKind::Direction) config.motor[i].reverseAngle=bool(a.value&(1<<i));
        if(kind==ActionKind::Polarity) config.motor[i].reverseOutput=bool(a.value&(1<<i));
      }
      if(kind!=ActionKind::Test && kind!=ActionKind::Zero) {++revision; dirty=true;}
      pending=Pending{}; pending.active=true; pending.action=a;
      pending.request=frame.sequence; pending.sender=frame.session;
      immediate=false;
    }
    if(result!=Result::Stale && result!=Result::Busy) {
      haveAction=true; lastAction=frame.sequence; lastActionSession=frame.session;
    }
  }
  if(immediate) sendAck(frame.sequence,kind,result);
}
void receive(const Frame &frame) {
  if(!frame.session) return;
  if(frame.type==Message::Control) {
    Control c;
    if(!unpack(frame,c) || c.receiverBoot!=bootId || !validTargets(Mode(c.mode),c.target)) return;
    Lock lock;
    if(!gate.accept(frame.session,frame.sequence,c.epoch,millis())) return;
    if(mode!=Mode(c.mode)) {gate.stop(); mode=Mode(c.mode);}
    if(!pending.active) std::memcpy(targets,c.target,sizeof(targets));
  } else if(frame.type==Message::Action) {
    Action a; if(unpack(frame,a)) handleAction(frame,a);
  }
}
} // namespace

void setup() {
  Serial.begin(115200);
  // Radios remain uninitialized on N1; the entire Wi-Fi interface is on N2.
  bootId=esp_random(); if(!bootId) bootId=1;
  gate.epoch=esp_random(); if(!gate.epoch) gate.epoch=1;
  preferences.begin("ts-drive",false);
  DriveConfig stored;
  if(preferences.getBytesLength("config")==sizeof(stored)) {
    preferences.getBytes("config",&stored,sizeof(stored)); if(validConfig(stored)) config=stored;
  }
  // Drive all H-bridge inputs low before starting either controller task.
  for(const auto &pins:PwmPins) for(int pin:pins) {pinMode(pin,OUTPUT); digitalWrite(pin,LOW);}
  if(xTaskCreatePinnedToCore(motorPairTask,"left-pid",6144,reinterpret_cast<void*>(0),4,nullptr,1)!=pdPASS ||
     xTaskCreatePinnedToCore(motorPairTask,"right-pid",6144,reinterpret_cast<void*>(1),4,nullptr,0)!=pdPASS) {
    Lock lock; fail(ControllerFault);
  }
  Serial.println("Tsintsadze N1: dual-bus / dual-core, disarmed");
}
void loop() {
  // Arduino's loop is core 1 by default; UART is explicitly moved to core 0
  // below on its first entry, leaving core 1's pair task isolated.
  static bool launched=false;
  if(!launched) {
    launched=true;
    if(xTaskCreatePinnedToCore([](void*) {
      // Initialize on the UART owner's core, including its driver interrupt.
      uart.setRxBufferSize(2048); uart.setTxBufferSize(1024);
      uart.begin(UartBaud,SERIAL_8N1,UartRx,UartTx);
      uint32_t lastTelemetry=0,lastConfig=0,sentRevision=0;
      for(;;) {
        Frame frame; unsigned budget=512;
        while(budget-- && uart.available()) if(parser.feed(uint8_t(uart.read()),frame)) receive(frame);
        Pending complete; bool done=false;
        {Lock lock;
          gate.poll(millis());
          if(pending.active && (pending.done&pending.action.mask)==pending.action.mask) {
            complete=pending; pending.active=false; done=true;
          }
        }
        if(done) sendAck(complete.request,ActionKind(complete.action.kind),complete.result);
        const uint32_t now=millis();
        if(uint32_t(now-lastTelemetry)>=20) {
          lastTelemetry=now; Telemetry out;
          {Lock lock;
            out=telemetry; out.epoch=gate.epoch; out.faults=gate.faults; out.armed=gate.armed;
            out.uartErrors=parser.errors;
            out.mode=uint8_t(mode); out.configRevision=revision; out.pendingSave=dirty||saving; out.busy=pending.active||saving;
            std::memcpy(out.target,targets,sizeof(targets));
          }
          sendFrame(packet(Message::Telemetry,++txSequence,bootId,out));
        }
        DriveConfig save; bool doSave=false;
        {Lock lock;
          if(dirty && !gate.armed && !pending.active && !saving) {saving=true; save=config; doSave=true;}
        }
        if(doSave) {
          const bool ok=preferences.putBytes("config",&save,sizeof(save))==sizeof(save);
          Lock lock; if(ok) dirty=false; saving=false;
        }
        uint32_t rev; {Lock lock; rev=revision;}
        if(rev!=sentRevision || uint32_t(now-lastConfig)>=1000) {
          DriveConfig out; {Lock lock; out=config;}
          sentRevision=rev; lastConfig=now;
          sendFrame(packet(Message::Config,++txSequence,bootId,out));
        }
        vTaskDelay(pdMS_TO_TICKS(1));
      }
    },"uart-n1",6144,nullptr,2,nullptr,0)!=pdPASS) {Lock lock; fail(ControllerFault);}
  }
  delay(1000);
}
