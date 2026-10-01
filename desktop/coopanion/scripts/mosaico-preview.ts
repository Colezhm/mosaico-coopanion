/** Real desktop renderer + WSS coordinator + shared native C simulator.
 * No model, microphone, computer tool or physical device is opened by this harness.
 * Start the native simulator with COOP_SIM_PORT=19774 and the same asset path first.
 */
import { createServer } from 'node:http';
import { connect, type Socket } from 'node:net';
import { readFileSync, writeFileSync, mkdirSync, appendFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { resolve } from 'node:path';
import { WebSocket } from 'ws';
import { PetServer } from '../packages/cortico-world-desktop-pet/src/server.ts';
import { MosaicoBridge } from '../packages/cortico-world-desktop-pet/src/mosaico/bridge.ts';
import { validAtlas } from '../packages/cortico-world-desktop-pet/src/mosaico/assets.ts';
import { DESKTOP_PET_DEFAULTS } from '../packages/cortico-world-desktop-pet/src/config.ts';
import type { Message } from '../packages/cortico-world-desktop-pet/src/mosaico/coordinator.ts';

const directory=resolve(process.env.COOP_PREVIEW_DIR||'../../artifacts/mosaico-coopanion/preview');
mkdirSync(directory,{recursive:true});
const pairingFile=resolve(directory,'pairing/pairing.json');
const pairing=JSON.parse(readFileSync(pairingFile,'utf8'));
const pairingToken = readFileSync(resolve(directory,'pairing/.env'),'utf8').match(/^CORTICO_MOSAICO_TOKEN=([a-f0-9]{64})$/m)?.[1];
if (!pairingToken) throw new Error('Run the pairing helper with --prepare-only first');
pairing.advertise=false;writeFileSync(pairingFile,JSON.stringify(pairing),{mode:0o600});
const assetFile=resolve(directory,'sim-atlas.bin');
const log=(side:string,m:Message)=>{if(!['clock_ping','clock_pong','clock_quality','asset_chunk'].includes(m.t))appendFileSync(resolve(directory,'events.jsonl'),JSON.stringify({observedAt:performance.now(),side,...m,data:undefined})+'\n');};
let skin=DESKTOP_PET_DEFAULTS.skin;
let bridge:MosaicoBridge;
const pet=new PetServer({port:()=>7798,webDir:resolve('packages/cortico-world-desktop-pet/web'),
  snapshot:()=>({skin,roam:'off',sound:false,theme:'dark',scale:1.7,mic:false,user:'伙伴',hoverButtons:[],bot:{name:'Coo'},mosaico:bridge?.presence.snapshot()}),
  onPetMessage:m=>{log('desktop',m);bridge.desktopMessage(m);},onAudio:()=>{},
  onPetConnect:()=>bridge.desktopConnected(),onPetDisconnect:()=>bridge.desktopDisconnected(),
  onSkin:s=>{skin=s as typeof skin;pet.broadcast({t:'prefs',skin});bridge.syncSkin();},onPrefs:()=>{}});
bridge=new MosaicoBridge(resolve(directory,'journal'),{
  sendDesktop:m=>{log('to-desktop',m);return pet.sendPet(m);},input:m=>log('input',m),speech:text=>log('speech',{t:'text',text}),
  transcribe:async()=>({text:'',error:'模拟器没有麦克风；此入口仅验证倾听状态'}),voiceEnabled:()=>true,skin:()=>skin,error:text=>{console.error(text);log('error',{t:'error',text});}
},()=>false);
await pet.start();await bridge.start(pairingFile, pairingToken);
let native:Socket|null=null,ws:WebSocket|null=null,session='',seq=0,buffer='',offline=false,stopping=false;
let offer:Message|null=null,bytes:Buffer|null=null,offset=0;
const sendNative=(m:Message)=>{if(native&&!native.destroyed)native.write(JSON.stringify(m)+'\n');};
const sendBoard=(m:Message)=>{log('board',m);if(ws?.readyState===WebSocket.OPEN&&session)ws.send(JSON.stringify({...m,v:1,session,seq:++seq}));};
function connectWss(){
  if(offline||stopping||ws||!native)return;
  const socket=new WebSocket(`wss://127.0.0.1:${pairing.port}/mosaico/v1`,{ca:readFileSync(pairing.certFile),headers:{Authorization:`Bearer ${pairingToken}`}});ws=socket;
  socket.on('message',raw=>{
    const m=JSON.parse(raw.toString()) as Message;log('to-board',m);
    if(m.t==='hello'){session=String(m.session);seq=0;return;}
    if(m.t==='asset_offer'){offer=m;bytes=Buffer.alloc(Number(m.size));offset=0;sendBoard({t:'asset_next',id:m.id,offset});return;}
    if(m.t==='asset_chunk'&&offer&&m.id===offer.id&&bytes){
      if(m.offset===offset){const data=Buffer.from(String(m.data),'base64');if(offset+data.length<=bytes.length){data.copy(bytes,offset);offset+=data.length;}}
      sendBoard({t:'asset_next',id:m.id,offset});return;
    }
    if(m.t==='asset_commit'&&offer&&bytes&&m.id===offer.id){
      const hash=createHash('sha256').update(bytes).digest('hex');
      if(offset!==bytes.length||hash!==m.sha256||!validAtlas(bytes))sendBoard({t:'asset_error',id:m.id,error:'校验失败'});
      else{writeFileSync(assetFile,bytes);sendNative({t:'asset_apply',id:m.id,sha256:hash});}return;
    }
    sendNative(m);
  });
  socket.on('error',e=>console.error('preview WSS:',e.message));
  socket.on('close',()=>{if(ws===socket){ws=null;session='';sendNative({t:'sim_offline'});if(!offline&&!stopping)setTimeout(connectWss,1000);}});
}
function connectNative(){
  if(stopping)return;
  const socket=connect({host:'127.0.0.1',port:Number(process.env.COOP_SIM_PORT||19774)});
  socket.on('connect',()=>{native=socket;buffer='';connectWss();});
  socket.on('data',data=>{buffer+=data.toString();let end;while((end=buffer.indexOf('\n'))>=0){const line=buffer.slice(0,end);buffer=buffer.slice(end+1);try{sendBoard(JSON.parse(line));}catch(e){console.error(e);}}});
  socket.on('error',()=>{});
  socket.on('close',()=>{if(native===socket){native=null;ws?.terminate();}if(!stopping)setTimeout(connectNative,1000);});
}
connectNative();
const html=`<!doctype html><html lang="zh"><meta charset="utf-8"><title>Mosaico × Coo 双端联调</title>
<style>body{margin:0;background:#12181e;color:#eaf5f0;font:15px system-ui}main{max-width:1100px;margin:24px auto}h1{font-size:24px}p{color:#9dafae}.screens{display:flex;gap:20px}.screen{flex:1}iframe{width:100%;height:570px;border:1px solid #334944;border-radius:20px;background:#202e32}button{background:#233e39;color:#d8fff0;border:1px solid #487b69;border-radius:8px;padding:10px 14px;margin:5px;cursor:pointer}pre{white-space:pre-wrap;color:#9edbc2}.badge{color:#8ef5bf}</style>
<main><h1>Mosaico × Coo <span class="badge">双端联调</span></h1><p>左侧为原版桌宠渲染器，右侧为共享 C 状态机的 GSP 模拟器。声音与传感器输入在此模拟，未连接真实硬件。</p>
<div class="screens"><section class="screen"><h3>电脑 · 椭圆传送门</h3><iframe src="${pet.origin}/pet"></iframe></section><section class="screen"><h3>Mosaico · 480 × 480</h3><iframe src="http://127.0.0.1:3222/"></iframe></section></div>
<div id="controls">${[['device','前往 Mosaico'],['desktop','返回电脑'],['sway','摇晃'],['stumble','踉跄'],['fall','摔倒'],['cry','哭泣'],['sulk','生闷气'],['soothe','摸头安抚'],['say','中文字幕'],['offline','断开连接'],['online','重新连接']].map(([id,title])=>`<button data-command="${id}">${title}</button>`).join('')}<button id="talk">按住 AI 键</button></div><pre id="status">连接中…</pre><p id="result"></p></main>
<script>async function command(action){try{const r=await fetch('/command',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({action})});document.querySelector('#result').textContent=await r.text();}catch(e){document.querySelector('#result').textContent=e.message;}}document.querySelectorAll('[data-command]').forEach(b=>b.onclick=()=>command(b.dataset.command));const talk=document.querySelector('#talk');talk.onpointerdown=e=>{talk.setPointerCapture(e.pointerId);command('down');};talk.onpointerup=()=>command('up');talk.onpointercancel=()=>command('up');setInterval(async()=>{document.querySelector('#status').textContent=JSON.stringify(await(await fetch('/status')).json(),null,2);},500);</script></html>`;
const server=createServer(async(req,res)=>{
  if(!/^127\.0\.0\.1:5199$/.test(req.headers.host||'')){res.writeHead(403);res.end();return;}
  if(req.url==='/status'){res.setHeader('Content-Type','application/json');res.end(JSON.stringify(bridge.state()));return;}
  if(req.url==='/command'&&req.method==='POST'){
    if(req.headers.origin!=='http://127.0.0.1:5199'){res.writeHead(403);res.end();return;}
    let input='';for await(const part of req){input+=part;if(input.length>1024){res.writeHead(413);res.end();return;}}
    try{const {action}=JSON.parse(input);log('control',{t:action});
      if(action==='desktop'||action==='device')await bridge.transfer(action);
      else if(action==='offline'){offline=true;ws?.terminate();sendNative({t:'sim_offline'});}
      else if(action==='online'){offline=false;connectWss();}
      else if(action==='down'||action==='up')sendNative({t:'sim_button',down:action==='down'});
      else if(action==='soothe')sendNative({t:'sim_touch'});
      else if(action==='fall')sendNative({t:'sim_imu',ax:2.8,gx:400});
      else if(action==='say')bridge.send({t:'say',beats:[{text:'我到小屏幕里啦！轻轻摸摸头，就不生气了。',face:'happy'}]});
      else if(['sway','stumble','cry','sulk'].includes(action))sendNative({t:'sim_action',action});
      res.end('已执行：'+action);
    }catch(e){res.writeHead(409);res.end(String(e));}return;
  }
  res.setHeader('Content-Type','text/html; charset=utf-8');res.end(html);
});
server.listen(5199,'127.0.0.1',()=>console.log('Preview: http://127.0.0.1:5199/'));
async function stop(){if(stopping)return;stopping=true;native?.destroy();ws?.terminate();server.close();await bridge.stop();await pet.stop();process.exit(0);}
process.on('SIGINT',()=>void stop());process.on('SIGTERM',()=>void stop());
