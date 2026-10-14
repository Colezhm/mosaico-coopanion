import { afterAll, beforeAll, describe, expect, it, vi } from 'vitest';
import { mkdirSync, mkdtempSync, writeFileSync, readFileSync, readdirSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';
import { createServer } from 'node:net';
import { createHash } from 'node:crypto';
import { WebSocket } from 'ws';
import { MosaicoRelay, recoveryEpoch } from '../src/relay.ts';
import type { PageMessage, PetBodyPort } from 'cortico-world-desktop-pet';
import { DESKTOP_PET_DEFAULTS } from 'cortico-world-desktop-pet';
import { AssetSync, validAtlas } from '../src/assets.ts';
import { MosaicoTransport } from '../src/transport.ts';
import type { Message } from '../src/coordinator.ts';

/** A relay for `dataDir` with Mosaico switched on through the legacy config section, as 0.1.0-mosaico.7 left it. */
function relayFor(dataDir:string,mosaico:Record<string,unknown>,token=''){
  const cfg={...structuredClone(DESKTOP_PET_DEFAULTS),mosaico};
  return new MosaicoRelay({dataDir,secret:()=>token,cfg} as never);
}
const log={info(){},warn(){},error(){},debug(){},trace(){},child(){return this;}} as unknown as PetBodyPort['log'];
function portFor(over:Partial<PetBodyPort>={}):PetBodyPort{
  return {sendPage:()=>true,input:()=>{},heard:()=>{},transcribe:async()=>({text:''}),voiceEnabled:()=>false,skin:()=>({} as never),words:()=>[],push:()=>{},log,...over};
}
function atlas():Buffer {
  const b=Buffer.alloc(188+22*5);b.write('COO1');b.writeUInt16LE(192,4);b.writeUInt16LE(216,6);b.writeUInt16LE(22,8);
  for(let i=0;i<22;i++){b.writeUInt32LE(188+i*5,12+i*8);b.writeUInt32LE(5,16+i*8);b.writeUInt16LE(192*216,188+i*5);b.writeUInt16LE(65535,190+i*5);b[192+i*5]=255;}return b;
}
describe('Mosaico presence journal',()=>{
  it('sets an unreadable journal aside and gives the body to the desktop above any board epoch',()=>{
    const directory=mkdtempSync(join(tmpdir(),'mosaico-journal-'));
    try{
      mkdirSync(join(directory,'mosaico'));
      writeFileSync(join(directory,'mosaico','presence.json'),'{"version":1,"owner":');
      const before=recoveryEpoch();
      const relay=relayFor(directory,{enabled:true,pairingFile:'',autonomous:false});
      const state=relay.presence.snapshot();
      expect(state.owner).toBe('desktop');expect(state.transfer).toBeNull();
      expect(state.epoch).toBeGreaterThanOrEqual(before);expect(state.epoch).toBeLessThan(2**32);
      expect(JSON.parse(readFileSync(join(directory,'mosaico','presence.json'),'utf8'))).toEqual(state);
      expect(readdirSync(join(directory,'mosaico')).some(f=>f.startsWith('presence.json.corrupt-'))).toBe(true);
      expect(String(relay.state().error)).toContain('身体位置记录无法读取');
    }finally{rmSync(directory,{recursive:true,force:true});}
  });
});
describe('Mosaico resource channel',()=>{
  it('keeps the commit alive while hardware validates and writes NOR, but still bounds failure',()=>{
    const bytes=atlas(),sent:Message[]=[],errors:string[]=[];
    let now=0,id:unknown,ready=false;
    const sync=new AssetSync(m=>{sent.push(m);return true;},m=>{id=m.id;return true;},v=>{ready=v;},m=>errors.push(m),()=>now);
    const commit=()=>{sync.start({});sync.exported({t:'asset_exported',id,data:bytes.toString('base64')});sync.receive({t:'asset_next',id,offset:bytes.length});now+=51;sync.tick();};
    commit();const count=sent.length;
    for(let i=0;i<6;i++){now+=2001;sync.tick();}
    expect(sent).toHaveLength(count);expect(ready).toBe(false);expect(errors).toEqual([]);
    sync.receive({t:'asset_ready',id,sha256:createHash('sha256').update(bytes).digest('hex')});
    expect(ready).toBe(true);
    commit();
    for(let i=0;i<4;i++){now+=30001;sync.tick();}
    expect(ready).toBe(false);expect(errors).toEqual(['资源同步超时']);
  });
  it('validates run lengths and waits for hash-confirmed durable readiness',()=>{
    const bytes=atlas();expect(validAtlas(bytes)).toBe(true);const corrupt=Buffer.from(bytes);corrupt.writeUInt16LE(65535,188);expect(validAtlas(corrupt)).toBe(false);
    let now=0,exportRequest:Message|null=null,ready=false;const sent:Message[]=[];
    const sync=new AssetSync(m=>{sent.push(m);return true;},m=>{exportRequest=m;return true;},v=>{ready=v;},()=>{},()=>now);
    sync.start({});const id=exportRequest!.id;sync.exported({t:'asset_exported',id,data:bytes.toString('base64')});
    expect(sent.at(-1)?.t).toBe('asset_offer');expect(ready).toBe(false);
    sync.receive({t:'asset_next',id,offset:0});now=51;sync.tick();expect(sent.at(-1)?.t).toBe('asset_chunk');
    sync.receive({t:'asset_next',id,offset:bytes.length});now=102;sync.tick();expect(sent.at(-1)?.t).toBe('asset_commit');
    sync.receive({t:'asset_ready',id,sha256:'wrong'});expect(ready).toBe(false);
    sync.receive({t:'asset_ready',id,sha256:createHash('sha256').update(bytes).digest('hex')});expect(ready).toBe(true);
  });
});
describe('paired WSS audio and asset integration',()=>{
  let directory:string,bridge:MosaicoRelay,peer:WebSocket;
  beforeAll(()=>{
    directory=mkdtempSync(join(tmpdir(),'mosaico-wss-test-'));
    writeFileSync(join(directory,'tls.cnf'),'[req]\ndistinguished_name=dn\nx509_extensions=ext\nprompt=no\n[dn]\nCN=localhost\n[ext]\nsubjectAltName=DNS:localhost,IP:127.0.0.1\nbasicConstraints=CA:TRUE\n');
    execFileSync('openssl',['req','-x509','-newkey','rsa:2048','-nodes','-days','1','-config',join(directory,'tls.cnf'),'-keyout',join(directory,'key.pem'),'-out',join(directory,'cert.pem')],{stdio:'ignore'});
  });
  afterAll(async()=>{peer?.terminate();await bridge?.stop();rmSync(directory,{recursive:true,force:true});});
  it('keeps a paired session ready through one slow clock sample',async()=>{
    const connected=vi.fn(),quality:boolean[]=[];
    // Reserve a loopback port through the OS without exposing transport internals.
    const probe=createServer();await new Promise<void>(r=>probe.listen(0,'127.0.0.1',r));const port=(probe.address() as {port:number}).port;await new Promise<void>(r=>probe.close(()=>r()));
    const pairing={deviceId:'clock-test',token:'b'.repeat(64),port,advertise:false,certFile:join(directory,'cert.pem'),keyFile:join(directory,'key.pem')};
    const live=new MosaicoTransport(pairing,{connected,disconnected:()=>{},message:()=>{},audio:()=>{},error:()=>{}});
    let socket:WebSocket|undefined;
    try{
      await live.start();socket=new WebSocket(`wss://127.0.0.1:${port}/mosaico/v1`,{ca:readFileSync(pairing.certFile),headers:{Authorization:`Bearer ${pairing.token}`}});
      let session='',seq=0,pings=0;
      socket.on('message',data=>{const m=JSON.parse(data.toString());
        if(m.t==='hello')session=m.session;
        if(m.t==='clock_quality')quality.push(m.ready);
        if(m.t==='clock_ping'){
          const send=()=>socket?.readyState===WebSocket.OPEN&&socket.send(JSON.stringify({v:1,t:'clock_pong',session,seq:++seq,echo:m.at,at:performance.now()}));
          if(++pings===2)setTimeout(send,150);else send();
        }
      });
      // The minimum-RTT window still holds the first quick sample.
      await vi.waitFor(()=>expect(pings).toBeGreaterThanOrEqual(3),{timeout:6000});
      await vi.waitFor(()=>expect(quality.length).toBeGreaterThanOrEqual(3),{timeout:4000});
      expect(quality).not.toContain(false);
      expect(live.ready).toBe(true);expect(connected).toHaveBeenCalledTimes(1);
    }finally{socket?.terminate();await live.stop();}
  },10000);
  it('exchanges verified assets, transcribes complete PCM, and rejects missing audio',async()=>{
    const probe=createServer();await new Promise<void>(r=>probe.listen(0,'127.0.0.1',r));const port=(probe.address() as {port:number}).port;await new Promise<void>(r=>probe.close(()=>r()));
    const token='a'.repeat(64),pairingFile=join(directory,'pairing.json');
    writeFileSync(pairingFile,JSON.stringify({deviceId:'test-device',port,advertise:false,certFile:join(directory,'cert.pem'),keyFile:join(directory,'key.pem')}));
    mkdirSync(join(directory,'mosaico'),{recursive:true});
    writeFileSync(join(directory,'mosaico','presence.json'),JSON.stringify({version:1,owner:'device',epoch:1,transfer:null}));
    const spoken:string[]=[],transcribe=vi.fn(async(pcm:Int16Array)=>{expect(pcm.length).toBe(1920);return {text:'你好'};});const received:Message[]=[];
    bridge=relayFor(directory,{enabled:true,pairingFile,autonomous:false},token);
    await bridge.start(portFor({
      sendPage:(m:PageMessage)=>{if(m.t==='asset_export')queueMicrotask(()=>bridge.fromPage({t:'asset_exported',id:m.id,data:atlas().toString('base64')}));return true;},
      heard:text=>spoken.push(text),transcribe,voiceEnabled:()=>true,
      words:()=>[{id:'happy',kind:'expression'},{id:'wave',kind:'motion'}],
    }));
    bridge.pageConnected();
    peer=new WebSocket(`wss://127.0.0.1:${port}/mosaico/v1`,{ca:readFileSync(join(directory,'cert.pem')),headers:{Authorization:`Bearer ${token}`}});
    let session='',seq=0,assetBytes:Buffer[]=[];
    const send=(m:Message)=>peer.send(JSON.stringify({...m,v:1,session,seq:++seq}));
    peer.on('message',data=>{const m=JSON.parse(data.toString()) as Message;received.push(m);
      if(m.t==='hello'){session=String(m.session);return;}
      if(m.t==='clock_ping')send({t:'clock_pong',echo:m.at,at:performance.now()+1000});
      if(m.t==='asset_offer'){assetBytes=[];send({t:'asset_next',id:m.id,offset:0});}
      if(m.t==='asset_chunk'){assetBytes.push(Buffer.from(String(m.data),'base64'));send({t:'asset_next',id:m.id,offset:assetBytes.reduce((n,b)=>n+b.length,0)});}
      if(m.t==='asset_commit'){expect(validAtlas(Buffer.concat(assetBytes))).toBe(true);send({t:'asset_ready',id:m.id,sha256:createHash('sha256').update(Buffer.concat(assetBytes)).digest('hex')});}
    });
    await vi.waitFor(()=>expect(bridge.state().ready).toBe(true),{timeout:5000});
    send({t:'voice_start',utterance:7});
    // A delayed end/cancel from the previous capture must not cancel utterance 7.
    send({t:'voice_end',utterance:6,frames:900});send({t:'voice_cancel',utterance:6});
    for(let i=0;i<3;i++){const pcm=Buffer.alloc(1292);pcm.write('MPCM');pcm.writeUInt32LE(7,4);pcm.writeUInt32LE(i,8);peer.send(pcm);}
    send({t:'voice_end',utterance:7,frames:3});await vi.waitFor(()=>expect(spoken).toEqual(['你好']));
    send({t:'voice_start',utterance:8});const bad=Buffer.alloc(1292);bad.write('MPCM');bad.writeUInt32LE(8,4);bad.writeUInt32LE(1,8);peer.send(bad);send({t:'voice_end',utterance:8,frames:2});
    await vi.waitFor(()=>expect(received.some(m=>m.t==='voice_error')).toBe(true));expect(transcribe).toHaveBeenCalledTimes(1);
    const before=received.length;peer.send(JSON.stringify({v:1,session:'old-session',seq:seq+100,t:'summon'}));
    await new Promise(r=>setTimeout(r,30));expect(received.slice(before).some(m=>m.t==='transfer_prepare')).toBe(false);
    // the body is on the board: orders go there in the board's words, and the computer's microphone is off
    expect(bridge.here()).toBe(false);expect(bridge.reachable()).toBe(true);
    expect(bridge.send({t:'say',id:'s1',beats:[{text:'嗨',actions:['wave','happy','determined'],anchors:[]}]})).toBe(true);
    expect(bridge.send({t:'status',status:{kind:'think',text:''}})).toBe(true);
    expect(bridge.send({t:'status',status:{kind:'read',text:'在看'}})).toBe(true);
    expect(bridge.send({t:'dialog',id:'d1'})).toBe(false);
    await vi.waitFor(()=>expect(received.filter(m=>m.t==='pet_command').map(m=>(m.command as Message).t)).toEqual(expect.arrayContaining(['say','thinking'])));
    const say=received.find(m=>m.t==='pet_command'&&(m.command as Message).t==='say')!.command as {beats:Array<{text:string;actions:string[]}>};
    expect(say.beats).toEqual([{text:'嗨',actions:['hop','happy','smug']}]);
    expect(received.filter(m=>m.t==='pet_command'&&(m.command as Message).t==='thinking')).toHaveLength(2);
  },10000);
});
