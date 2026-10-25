// Exercise the production request/queue helpers with controllable fetches and
// timers. No ESP32 or DOM renderer is required for these scheduling tests.
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const source=fs.readFileSync(path.join(__dirname,'../include/dashboard.h'),'utf8');
const helper=source.split('// BEGIN REQUEST HELPERS')[1].split('// END REQUEST HELPERS')[0];
assert(helper.includes('function command('));
function harness(backend){
  const timers=new Map(),notices=[],buttons=new Map();let now=0,nextTimer=0;
  const context=vm.createContext({
    AbortController,
    fetch:(url,options)=>new Promise((resolve,reject)=>{
      const abort=()=>{const error=Error('aborted');error.name='AbortError';reject(error);};
      if(options.signal.aborted){abort();return;}
      options.signal.addEventListener('abort',abort,{once:true});
      Promise.resolve().then(()=>backend(url,options)).then(([status,data])=>resolve({status,ok:status>=200&&status<300,json:async()=>data}),reject);
    }),
    performance:{now:()=>now},notice:text=>notices.push(text),
    setTimeout:(callback,delay)=>{const id=++nextTimer;timers.set(id,{at:now+delay,callback});return id;},
    clearTimeout:id=>timers.delete(id),
    document:{querySelectorAll:selector=>{if(!buttons.has(selector))buttons.set(selector,{disabled:false,setAttribute(){}});return [buttons.get(selector)];}},
    loadConfig:async()=>{},poll:()=>{},
  });
  vm.runInContext(helper,context);
  async function flush(){for(let i=0;i<60;++i)await Promise.resolve();}
  async function tick(){
    const next=[...timers].sort((a,b)=>a[1].at-b[1].at)[0];assert(next,'Expected a pending timer');
    timers.delete(next[0]);now=next[1].at;next[1].callback();await flush();
  }
  return {api:context.api,command:context.command,stopAll:context.stopAll,read:code=>vm.runInContext(code,context),flush,tick,timers,notices,buttons,advance:ms=>now+=ms};
}
function gate(){let release;const promise=new Promise(resolve=>release=resolve);return {promise,release};}
(async()=>{
  let polls=0;
  const success=harness(url=>url==='/motor'?[202,{pending:true,request:9,timeoutMs:6050}]:++polls<3?[202,{pending:true,request:9}]:[200,{pending:false,ok:true}]);
  let completed=false;const operation=success.api('/motor',{operation:'test',mask:1}).then(value=>{completed=true;return value;});
  await success.flush();assert(!completed);
  await success.tick();await success.tick();assert(!completed);
  await success.tick();assert((await operation).ok);assert.equal(polls,3);assert.equal(success.timers.size,0);

  // Independent clicks queue, duplicate clicks do not issue duplicate actions.
  const queue=harness(()=>[200,{ok:true}]),hold=gate(),calls=[];
  const first=queue.command(async()=>{calls.push(1);await hold.promise;},'one','pidApply');
  const second=queue.command(async()=>calls.push(2),'two','manualApply');
  assert.equal(await queue.command(async()=>calls.push(99),'duplicate','pidApply'),false);
  assert.equal(await queue.command(async()=>calls.push(100),'arm','arm'),false); // arming is never delayed in the queue
  assert(queue.read('commandBusy') && queue.read('actionQueue.length')===1);
  hold.release();await queue.flush();assert(await first && await second);assert.deepEqual(calls,[1,2]);
  assert.equal(await queue.command(async()=>calls.push(101),'bounce','manualApply'),false); // 250 ms leading debounce
  queue.advance(300);assert(await queue.command(async()=>calls.push(3),'three','manualApply'));
  assert.equal(queue.read('pendingKeys.size'),0);

  // Multiple edits to the same queued curve point keep the latest value.
  const edits=harness(()=>[200,{ok:true}]),editing=gate(),values=[];
  const busy=edits.command(()=>editing.promise,'busy','applyMode');
  const old=edits.command(async()=>values.push(10),'point','point-1',true);
  const latest=edits.command(async()=>values.push(25),'point','point-1',true);
  editing.release();await edits.flush();await Promise.all([busy,old,latest]);assert.deepEqual(values,[25]);

  // Starting a click cancels a stuck telemetry read before issuing the action.
  const priority=harness(url=>url==='/status'?new Promise(()=>{}):[200,{ok:true}]);
  const background=assert.rejects(priority.api('/status'),error=>error.name==='AbortError');
  await priority.flush();assert(await priority.command(()=>priority.api('/manual',{angles:[1,2]}),'manual','manualApply'));
  await background;assert.equal(priority.timers.size,0);

  // A timed-out request releases the queue and controls instead of freezing them.
  const timeout=harness(url=>url==='/pid'?new Promise(()=>{}):[200,{ok:true}]);
  const timed=timeout.command(()=>timeout.api('/pid',{kp:35}),'pid','pidApply');
  const following=timeout.command(()=>timeout.api('/manual',{angles:[0,0]}),'manual','manualApply');
  await timeout.flush();await timeout.tick();assert.equal(await timed,false);assert(await following);
  assert(!timeout.read('commandBusy'));assert(timeout.notices.some(text=>text.includes('timed out')));
  assert.equal(timeout.read('pendingKeys.size'),0);

  // STOP interrupts an active PID operation and discards unsent actions.
  let stopped=false,stopPosts=0;
  const cancel=harness((url,options)=>{
    if(url==='/pid')return [202,{pending:true,request:10,timeoutMs:2500}];
    if(url==='/arm'){assert.equal(JSON.parse(options.body).enabled,false);stopped=true;++stopPosts;return [202,{pending:true,request:11,timeoutMs:2500}];}
    if(url.endsWith('=11'))return [200,{ok:true,pending:false}];
    return stopped?[409,{error:'Action cancelled by STOP ALL'}]:[202,{pending:true,request:10}];
  });
  let queuedRan=false;
  const running=cancel.command(()=>cancel.api('/pid',{kp:35}),'pid','pidApply');
  const discarded=cancel.command(async()=>{queuedRan=true;},'manual','manualApply');
  await cancel.flush();const stopping=cancel.stopAll();await cancel.flush();
  assert.equal(await discarded,false);assert(!queuedRan && stopped);
  assert.equal(await cancel.command(async()=>{},'arm','arm'),false);
  cancel.stopAll();assert.equal(stopPosts,1);
  await cancel.tick();await cancel.tick();await stopping;assert.equal(await running,false);
  assert(!queuedRan && !cancel.read('commandBusy') && !cancel.read('stopPending'));
  assert.equal(cancel.notices.at(-1),'N1 confirmed all motors stopped.');
  console.log('PASS dashboard: async confirmation, queued clicks, duplicate debounce, coalesced edits, request timeout, telemetry priority, STOP preemption');
})().catch(error=>{console.error(error);process.exitCode=1;});
