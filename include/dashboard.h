#pragma once
#include <Arduino.h>
static const char DASHBOARD[] PROGMEM=R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Project Tsintsadze</title><style>
:root{color-scheme:dark;--bg:#0b1220;--card:#152235;--line:#2c4059;--text:#eaf2fc;--muted:#9fb2c9;--blue:#64c7ff;--green:#6de0ad;--red:#ff828f}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:14px system-ui,sans-serif}main{max-width:1250px;margin:auto;padding:24px}header{display:flex;justify-content:space-between;align-items:center;gap:20px;margin-bottom:22px}h1{font-size:26px;margin:0 0 4px}h2{font-size:18px;margin:0 0 16px}h3{font-size:15px;margin:12px 0}p{color:var(--muted);line-height:1.6}.muted,small{color:var(--muted)}.grid{display:grid;grid-template-columns:1fr 1fr;gap:18px}.card{min-width:0;background:var(--card);border:1px solid var(--line);border-radius:14px;padding:20px;margin-bottom:18px}.full{grid-column:1/-1}.row{display:flex;align-items:center;flex-wrap:wrap;gap:10px}.spread{justify-content:space-between}.stats{display:grid;grid-template-columns:repeat(4,1fr);gap:12px}.stat{background:var(--bg);padding:12px;border-radius:8px}.stat strong{display:block;font-size:22px;margin-top:4px}.good{color:var(--green)}.bad{color:var(--red)}button,input,select{font:inherit;border-radius:7px;border:1px solid #415671;padding:9px;background:var(--bg);color:var(--text)}button{cursor:pointer;background:#234d74}button:hover{filter:brightness(1.2)}button:disabled{opacity:.5;cursor:wait}.stop{background:#a92e46;border-color:#db526e;font-weight:700;padding:12px 25px}.primary{background:#196a9b}.secondary{background:#253448}input[type=number]{width:100px}input[type=checkbox]{width:18px;height:18px;accent-color:var(--blue)}label{display:inline-flex;gap:8px;align-items:center}.labels{display:grid;grid-template-columns:1fr 1fr;gap:12px}.labels label{justify-content:space-between}.scroll{overflow-x:auto}table{border-collapse:collapse;width:100%;white-space:nowrap}td,th{padding:10px;text-align:right;border-bottom:1px solid var(--line)}th:first-child,td:first-child{text-align:left}th{font-size:12px;color:var(--muted);font-weight:500}td .row{justify-content:flex-end}td button{font-size:12px;padding:6px}td input[type=number]{width:85px}#notice{min-height:24px;position:sticky;bottom:8px;background:#122b3d;border:1px solid var(--line);padding:10px;border-radius:8px;margin-top:12px}canvas{width:100%;height:310px;display:block;touch-action:none;margin:12px 0}.tag{border-radius:20px;padding:5px 10px;background:var(--bg)}.bench{color:#ffcd8c}.metrics{font:12px ui-monospace,monospace;color:var(--muted);line-height:1.8}details summary{cursor:pointer;color:var(--blue)}@media(max-width:800px){main{padding:12px}.grid{grid-template-columns:1fr}.stats{grid-template-columns:repeat(2,1fr)}header{align-items:flex-start}.labels{grid-template-columns:1fr}.card{padding:14px}}
</style></head><body><main>
<header><div><h1>Project Tsintsadze</h1><span class="muted">Two wings · four closed-loop drives</span></div><button class="stop" id="stop">STOP ALL</button></header>
<div class="card"><div class="row spread"><div class="row"><span class="tag" id="uart">UART —</span><span class="tag" id="mpu">MPU —</span><span class="tag" id="armState">DISARMED</span><span class="tag" id="saveState">—</span></div><span id="faults" class="bad"></span></div></div>
<div class="grid">
<section class="card"><h2>Motion &amp; wing targets</h2><div class="stats"><div class="stat">Roll<strong id="roll">—</strong></div><div class="stat">Pitch<strong id="pitch">—</strong></div><div class="stat">Left target<strong id="left">—</strong></div><div class="stat">Right target<strong id="right">—</strong></div></div><p id="imuDetails">Waiting for sensor data</p><button id="calibrate" data-command>Calibrate MPU here</button><p><small>Stationary calibration sets the current roll and pitch to zero.</small></p></section>
<section class="card"><h2>Operating mode</h2><div class="row"><select id="mode"><option value="0">Automatic · MPU curves</option><option value="1">Manual · wing angles</option><option value="2">Bench · individual motors</option></select><button id="applyMode" data-command>Apply mode &amp; disarm</button></div><p id="modeNote">Automatic targets use the selected mapping preset.</p><form id="manualForm" hidden><div class="row" id="manualInputs"></div><button id="manualApply" class="primary" style="margin-top:12px" data-command>Apply manual targets</button></form><div class="row" style="margin-top:16px"><button id="arm" class="primary" data-command>ARM ALL MOTORS</button><span class="muted">Every boot and mode change starts disarmed.</span></div></section>
<section class="card full"><div class="row spread"><h2>Motor feedback &amp; calibration</h2><span class="muted">M1/M3: 125 mm · M2/M4: 100 mm · right linkage mirrored</span></div><div class="scroll"><table><thead><tr><th>Motor</th><th>Target °</th><th>Measured °</th><th>Error °</th><th>Raw °</th><th>PWM</th><th>Setup</th></tr></thead><tbody id="motorRows"></tbody></table></div><div class="row" style="margin-top:16px"><button id="zeroAll" data-command>Set all horizontal positions to 0°</button><button id="reverseAll" class="secondary" data-command>Reverse all logical directions</button><button id="polarityAll" class="secondary" data-command>Reverse all output polarities</button></div><p><small>Zero and direction changes disarm the system. Output polarity corrects feedback wiring; logical direction defines upward movement. Run polarity tests only in bench mode with the 30 mm connecting links removed and 5° clearance in either direction. Drive continues until 5° of encoder movement, with a 5-second timeout. STOP ALL remains available.</small></p></section>
<section class="card"><h2>Shared PID &amp; drive settings</h2><form id="pidForm"><div class="labels"><label>Kp<input type="number" id="kp" min="0" max="1000" step="0.01" required></label><label><input type="checkbox" id="enP">P enabled</label><label>Ki<input type="number" id="ki" min="0" max="1000" step="0.01" required></label><label><input type="checkbox" id="enI">I enabled</label><label>Kd<input type="number" id="kd" min="0" max="1000" step="0.01" required></label><label><input type="checkbox" id="enD">D enabled</label><label>Min PWM<input type="number" id="minPwm" min="0" max="4095" step="1" required></label><label>Deadband °<input type="number" id="db" min="0" max="10" step="0.01" required></label></div><button id="pidApply" class="primary" style="margin-top:16px" data-command>Apply shared settings &amp; disarm</button></form><p><small>All four controllers share these settings. Calibration and PID settings are stored on N1; mapping and MPU calibration are stored on N2.</small></p></section>
<section class="card"><h2>Timing &amp; health</h2><div class="metrics" id="metrics">Waiting for telemetry</div><p><small>N1: one encoder bus and two 1 kHz PID loops per core. N2: MPU/UART/control on core 1; Wi-Fi/dashboard on core 0. Missed deadlines include disarmed flash saves and sensor timeouts.</small></p></section>
<section class="card full"><div class="row spread"><h2>MPU → physical wing angle</h2><div class="row"><label>Preset<select id="preset"><option value="0">0</option><option value="1">1</option><option value="2">2</option></select></label><button id="reset" class="secondary" data-command>Reset preset</button><button id="savePreset" data-command>Save preset</button></div></div><div class="row"><select id="curve"><option value="rollToRight">Roll → right wing</option><option value="rollToLeft">Roll → left wing (mirrored)</option><option value="pitchToRight">Pitch → both wings</option></select><span class="muted">Drag points vertically · 0–70° · roll + pitch clamp at 70°</span></div><canvas id="curveCanvas"></canvas><details><summary>Edit coordinates</summary><div class="scroll"><table><thead><tr><th>MPU input °</th><th>Wing output °</th></tr></thead><tbody id="pointRows"></tbody></table></div></details><p><small>Fixed inputs: −90° to +90°, every 9°. Curves are stored after disarming. Positive wing commands solve for a negative physical wing angle, with both motor arms rising.</small></p></section>
</div><div id="notice" role="status">Connect both controllers to begin.</div>
</main><script>
'use strict';
const $=id=>document.getElementById(id),fmt=(n,d=2)=>typeof n==='number'&&Number.isFinite(n)?n.toFixed(d):'—';
let status=null,config=null,presetData=null,activeMode=-1,configRevision=-1,loadingConfig=false,polling=false,drag=-1;
const canvas=$('curveCanvas'),ctx=canvas.getContext('2d');
function notice(message,bad=false){$('notice').textContent=message;$('notice').className=bad?'bad':'';}
// BEGIN REQUEST HELPERS
const backgroundRequests=new Set(),actionQueue=[],pendingKeys=new Set(),lastClicks=new Map();
let commandBusy=false,stopPending=false,commandEpoch=0,activeCommand=null;
function abortBackground(){for(const controller of backgroundRequests)controller.abort();}
async function requestJson(path,data){
  const background=data===undefined&&!path.startsWith('/operation?');
  const controller=new AbortController();let timedOut=false;
  if(background)backgroundRequests.add(controller);
  const timer=setTimeout(()=>{timedOut=true;controller.abort();},background?1500:2000);
  try{
    const response=await fetch(path,{cache:'no-store',signal:controller.signal,...(data===undefined?{}:{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)})});
    const value=await response.json();
    if(!response.ok)throw Error(value.error||'Request failed');
    return {response,value};
  }catch(error){if(timedOut)throw Error('Dashboard request timed out; check system status before retrying.');throw error;}
  finally{clearTimeout(timer);backgroundRequests.delete(controller);}
}
async function api(path,data){
  let {response,value}=await requestJson(path,data);
  if(response.status===202&&value.pending&&value.request){
    const request=value.request,deadline=performance.now()+(value.timeoutMs||6050)+1000;
    notice('Waiting for controller confirmation. STOP ALL is available.');
    do{
      await new Promise(resolve=>setTimeout(resolve,150));
      ({response,value}=await requestJson('/operation?request='+request));
    }while(value.pending&&performance.now()<deadline);
    if(value.pending)throw Error('Controller acknowledgment timed out; check system status before retrying.');
  }
  return value;
}
function setPending(key,pending){
  if(!key)return;
  document.querySelectorAll('[data-action-key="'+key+'"]').forEach(button=>{button.disabled=pending;button.setAttribute('aria-busy',String(pending));});
}
function command(fn,message='Applied by N1; check save status.',key='',replace=false){
  if(stopPending){notice('Stopping motors; wait for confirmation.',true);return Promise.resolve(false);}
  const queued=key&&actionQueue.find(job=>job.key===key);
  if(queued&&replace){queued.fn=fn;queued.message=message;return queued.promise;}
  if(key&&pendingKeys.has(key)&&!replace){notice('That action is already waiting for confirmation.');return Promise.resolve(false);}
  if(key==='arm'&&(commandBusy||actionQueue.length)){notice('Wait for the current action before arming.',true);return Promise.resolve(false);}
  if(actionQueue.length>=8){notice('Action queue is full; wait for confirmation.',true);return Promise.resolve(false);}
  if(key&&!replace&&lastClicks.has(key)&&performance.now()-lastClicks.get(key)<250){notice('Action already requested.');return Promise.resolve(false);}
  if(key&&!replace)lastClicks.set(key,performance.now());
  let resolve;const promise=new Promise(done=>resolve=done);
  actionQueue.push({fn,message,key,epoch:commandEpoch,resolve,promise});
  if(key){pendingKeys.add(key);setPending(key,true);}
  if(commandBusy)notice('Action queued ('+actionQueue.length+' waiting).');
  pumpCommands();return promise;
}
async function pumpCommands(){
  if(commandBusy||stopPending||!actionQueue.length)return;
  const job=actionQueue.shift();activeCommand=job;commandBusy=true;abortBackground();
  let success=false;
  try{
    notice('Applying action…'+(actionQueue.length?' '+actionQueue.length+' queued.':''));
    await job.fn();
    if(job.epoch===commandEpoch){
      // Refresh calibration between queued toggles; preserve PID form edits.
      if(typeof loadConfig==='function')await loadConfig(false);
      success=true;notice(job.message);
    }
  }catch(error){if(job.epoch===commandEpoch&&error.name!=='AbortError')notice(error.message,true);}
  finally{
    if(job.key&&!actionQueue.some(next=>next.key===job.key)){pendingKeys.delete(job.key);setPending(job.key,false);}
    commandBusy=false;activeCommand=null;job.resolve(success);
    if(actionQueue.length)pumpCommands();else if(!stopPending&&typeof poll==='function')poll();
  }
}
async function stopAll(){
  if(stopPending)return;
  stopPending=true;++commandEpoch;abortBackground();
  for(const job of actionQueue.splice(0)){job.resolve(false);if(job.key&&(!activeCommand||activeCommand.key!==job.key)){pendingKeys.delete(job.key);setPending(job.key,false);}}
  notice('Stopping all motors…');
  try{await api('/arm',{enabled:false});notice('N1 confirmed all motors stopped.');}
  catch(error){notice(error.message,true);}
  finally{stopPending=false;if(typeof poll==='function')poll();}
}
// END REQUEST HELPERS
function motorOperation(operation,mask,value){return api('/motor',{operation,mask,...(value===undefined?{}:{value})});}
async function loadConfig(refreshPid=true){if(loadingConfig)return;loadingConfig=true;try{const c=await api('/config');if(!c.available)return;config=c;if(refreshPid){for(const field of ['kp','ki','kd','minPwm','db'])$(field).value=c.pid[field];for(const term of ['enP','enI','enD'])$(term).checked=c.pid[term];}renderCalibration();if(status)configRevision=status.configRevision;}finally{loadingConfig=false;}}
function renderCalibration(){if(!config)return;config.motors.forEach((m,i)=>{const row=$('motor'+i);row.querySelector('[data-zero]').textContent=m.zeroed?'Zero ✓':'Set zero';row.querySelector('[data-direction]').textContent=m.angleReversed?'Angle: reversed':'Angle: normal';row.querySelector('[data-polarity]').textContent=m.outputReversed?'Output: reversed':'Output: normal';});}
for(let i=0;i<4;i++){const row=document.createElement('tr');row.id='motor'+i;row.innerHTML=`<td>M${i+1} · ${i<2?'left':'right'}</td><td data-target>—</td><td data-angle>—</td><td data-error>—</td><td data-raw>—</td><td data-pwm>—</td><td><div class="row"><button data-zero data-command data-action-key="zero-${i}">Set zero</button><button data-direction data-command data-action-key="direction-${i}">Angle: —</button><button data-polarity data-command data-action-key="polarity-${i}">Output: —</button><button data-test data-command data-action-key="test-${i}">Test polarity (5°)</button></div></td>`;$('motorRows').appendChild(row);row.querySelector('[data-zero]').onclick=()=>command(()=>motorOperation('zero',1<<i),undefined,'zero-'+i);row.querySelector('[data-direction]').onclick=()=>command(()=>{if(!config)throw Error('Wait for controller settings');return motorOperation('direction',1<<i,config.motors[i].angleReversed?0:1<<i);},undefined,'direction-'+i);row.querySelector('[data-polarity]').onclick=()=>command(()=>{if(!config)throw Error('Wait for controller settings');return motorOperation('polarity',1<<i,config.motors[i].outputReversed?0:1<<i);},undefined,'polarity-'+i);row.querySelector('[data-test]').onclick=()=>command(()=>motorOperation('test',1<<i),'Polarity test completed; all motors remain disarmed.','test-'+i);}
function setupManual(mode){activeMode=mode;$('mode').value=String(mode);$('manualForm').hidden=mode===0;$('manualInputs').replaceChildren();$('modeNote').textContent=mode===0?'Automatic targets use the selected mapping preset.':mode===1?'Both motor targets are derived from each requested wing angle.':'BENCH ONLY: remove both 30 mm connecting links before commanding individual motors.';$('modeNote').className=mode===2?'bench':'muted';if(!mode)return;for(let i=0;i<(mode===1?2:4);i++){const label=document.createElement('label');label.textContent=mode===1?(i?'Right °':'Left °'):`M${i+1} °`;const input=document.createElement('input');input.type='number';input.step='0.1';input.required=true;input.min=mode===1?'0':'-45';input.max=mode===1?'70':'45';input.value='0';label.appendChild(input);$('manualInputs').appendChild(label);}}
let lastPollAt=-1000;
async function poll(){if(polling||stopPending||document.hidden||performance.now()-lastPollAt<(commandBusy?400:200))return;lastPollAt=performance.now();polling=true;try{status=await api('/status');$('uart').textContent=status.linkHealthy?'UART connected':'UART LOST';$('uart').className='tag '+(status.linkHealthy?'good':'bad');$('mpu').textContent=status.mpuHealthy?'MPU ready':'MPU unavailable';$('mpu').className='tag '+(status.mpuHealthy?'good':'bad');$('armState').textContent=status.armed?'ARMED':'DISARMED';$('armState').className='tag '+(status.armed?'good':'');$('saveState').textContent=status.pendingSave?'Save pending · disarm to store':'Settings stored';$('faults').textContent=status.faults?'Latched faults: '+[[1,'encoder'],[2,'UART'],[4,'controller']].filter(([bit])=>status.faults&bit).map(([,name])=>name).join(', '):'';for(const [id,key] of [['roll','calibratedRoll'],['pitch','calibratedPitch'],['left','leftWing'],['right','rightWing']])$(id).textContent=fmt(status[key],1)+'°';$('imuDetails').textContent=`Acceleration m/s²: ${status.accel.map(v=>fmt(v)).join(' / ')} · Gyro °/s: ${status.gyro.map(v=>fmt(v)).join(' / ')} · ${fmt(status.temperature,1)} °C`;if(activeMode!==status.mode)setupManual(status.mode);status.motors.forEach((motor,i)=>{const row=$('motor'+i);for(const field of ['target','angle','error','raw','pwm'])row.querySelector('[data-'+field+']').textContent=fmt(motor[field],field==='pwm'?0:2);row.firstChild.className=motor.valid?'good':'bad';});$('metrics').textContent=`N1 max cycle µs: ${status.pairMaxUs.join(' / ')} | missed: ${status.pairMissed.join(' / ')} | N2 max work µs: ${status.controlMaxUs} | missed: ${status.controlMissed} | UART rejected N1/N2: ${status.n1UartErrors} / ${status.uartErrors} | MPU errors: ${status.mpuErrors} | Encoder errors: ${status.motors.map(m=>m.sensorErrors).join(' / ')}`;if(status.configAvailable&&configRevision!==status.configRevision&&!$('pidForm').contains(document.activeElement))await loadConfig();}catch(e){if(e.name==='AbortError')return;$('uart').textContent='Dashboard disconnected';$('uart').className='tag bad';notice(e.message,true);}finally{polling=false;}}
$('stop').onclick=stopAll;
$('arm').onclick=()=>command(()=>api('/arm',{enabled:true}),'N1 confirmed motors armed.','arm');
$('applyMode').onclick=()=>{const selected=Number($('mode').value);command(()=>api('/mode',{mode:selected}),'Mode applied; motors disarmed.','applyMode');};
$('calibrate').onclick=()=>command(()=>api('/calibrate',{}),'MPU reference updated; check save status.','calibrate');
$('manualForm').onsubmit=e=>{e.preventDefault();const angles=[...$('manualInputs').querySelectorAll('input')].map(i=>Number(i.value));command(()=>api('/manual',{angles}),'Manual targets updated.','manualApply');};
$('zeroAll').onclick=()=>command(()=>motorOperation('zero',15),undefined,'zeroAll');
function reverseAll(operation,key){return command(()=>{if(!config)throw Error('Wait for controller settings');let bits=0;config.motors.forEach((m,i)=>{if(!m[key])bits|=1<<i;});return motorOperation(operation,15,bits);},undefined,operation==='direction'?'reverseAll':'polarityAll');}
$('reverseAll').onclick=()=>reverseAll('direction','angleReversed');$('polarityAll').onclick=()=>reverseAll('polarity','outputReversed');
$('pidForm').onsubmit=e=>{e.preventDefault();const data={};for(const field of ['kp','ki','kd','minPwm','db'])data[field]=Number($(field).value);for(const term of ['enP','enI','enD'])data[term]=$(term).checked;command(()=>api('/pid',data),undefined,'pidApply');};
async function loadPreset(){presetData=await api('/preset?p='+$('preset').value);drawCurve();renderPoints();}
function geometry(){const box=canvas.getBoundingClientRect(),ratio=devicePixelRatio||1;canvas.width=Math.round(box.width*ratio);canvas.height=Math.round(310*ratio);ctx.setTransform(ratio,0,0,ratio,0,0);return {w:box.width,h:310,l:48,r:18,t:18,b:38};}
function drawCurve(){if(!presetData)return;const g=geometry(),pw=g.w-g.l-g.r,ph=g.h-g.t-g.b;ctx.clearRect(0,0,g.w,g.h);ctx.font='12px system-ui';ctx.fillStyle='#9fb2c9';ctx.strokeStyle='#2c4059';ctx.lineWidth=1;for(let y=0;y<=70;y+=10){const py=g.h-g.b-y/70*ph;ctx.beginPath();ctx.moveTo(g.l,py);ctx.lineTo(g.w-g.r,py);ctx.stroke();ctx.fillText(y+'°',9,py+4);}for(let x=-90;x<=90;x+=30){const px=g.l+(x+90)/180*pw;ctx.fillText(x+'°',px-12,g.h-13);}const values=presetData[$('curve').value];ctx.strokeStyle='#64c7ff';ctx.lineWidth=2;ctx.beginPath();values.forEach((v,i)=>{const x=g.l+i/20*pw,y=g.h-g.b-v/70*ph;i?ctx.lineTo(x,y):ctx.moveTo(x,y);});ctx.stroke();ctx.fillStyle='#64c7ff';values.forEach((v,i)=>{ctx.beginPath();ctx.arc(g.l+i/20*pw,g.h-g.b-v/70*ph,4,0,2*Math.PI);ctx.fill();});}
function renderPoints(){if(!presetData)return;$('pointRows').replaceChildren();presetData[$('curve').value].forEach((v,i)=>{const row=document.createElement('tr'),x=document.createElement('td'),cell=document.createElement('td'),input=document.createElement('input');x.textContent=i*9-90;input.type='number';input.min='0';input.max='70';input.step='0.1';input.value=fmt(v,1);input.onchange=()=>{const value=Number(input.value),preset=$('preset').value,field=$('curve').value;command(()=>savePoint(i,value,preset,field),'Curve point updated; save deferred while armed.','point-'+preset+'-'+field+'-'+i,true);};cell.appendChild(input);row.append(x,cell);$('pointRows').appendChild(row);});}
function localPoint(index,value){const field=$('curve').value;presetData[field][index]=value;if(field==='rollToRight')presetData.rollToLeft[20-index]=value;else if(field==='rollToLeft')presetData.rollToRight[20-index]=value;else presetData.pitchToLeft[index]=value;}
async function savePoint(index,value,preset=$('preset').value,field=$('curve').value){if(!Number.isFinite(value)||value<0||value>70)throw Error('Wing angle must be 0–70°');await api('/set',{preset:Number(preset),field,index,value});await loadPreset();}
function movePoint(e){if(drag<0)return;const box=canvas.getBoundingClientRect(),ph=310-18-38;const value=Math.round(Math.max(0,Math.min(70,(310-38-(e.clientY-box.top))/ph*70))*10)/10;localPoint(drag,value);drawCurve();}
canvas.onpointerdown=e=>{if(!presetData||stopPending)return;const box=canvas.getBoundingClientRect();drag=Math.max(0,Math.min(20,Math.round((e.clientX-box.left-48)/(box.width-48-18)*20)));canvas.setPointerCapture(e.pointerId);movePoint(e);};
canvas.onpointermove=movePoint;canvas.onpointerup=e=>{if(drag<0)return;movePoint(e);const index=drag,value=presetData[$('curve').value][index];drag=-1;const preset=$('preset').value,field=$('curve').value;command(()=>savePoint(index,value,preset,field),'Curve point updated; save deferred while armed.','point-'+preset+'-'+field+'-'+index,true);};canvas.onpointercancel=()=>{drag=-1;loadPreset().catch(e=>notice(e.message,true));};
$('curve').onchange=()=>{drawCurve();renderPoints();};$('preset').onchange=()=>{const preset=Number($('preset').value);command(async()=>{await api('/use',{preset});await loadPreset();},'Preset activated.','presetSelect',true);};$('reset').onclick=()=>{const preset=Number($('preset').value);command(async()=>{await api('/reset',{preset});await loadPreset();},'Preset reset.','reset');};$('savePreset').onclick=()=>{const preset=Number($('preset').value);command(()=>api('/save',{preset}),'Preset queued for flash storage after disarming.','savePreset');};
document.querySelectorAll('[data-command][id]').forEach(button=>button.dataset.actionKey=button.id);
window.addEventListener('resize',drawCurve);
(async()=>{await poll();if(status)$('preset').value=String(status.activePreset);await loadPreset();})().catch(e=>notice(e.message,true));setInterval(poll,200);
</script></body></html>)HTML";
