import { describe, it, expect } from 'vitest';
import { PresenceCoordinator, type Presence, type Message, type Body } from '../src/mosaico/coordinator.ts';
function fixture(saved?: Presence) {
  let now=0; const sent: Array<{to:Body;msg:Message}>=[]; const journal: Presence[]=[];
  const p=new PresenceCoordinator({now:()=>now,send:(to,msg)=>{sent.push({to,msg});return true;},save:s=>journal.push(s),random:()=>0},saved);
  p.connected('desktop');p.connected('device');
  return {p,sent,journal,advance:(ms:number)=>{now+=ms;p.tick(false);},now:()=>now};
}
describe('Mosaico single-body transfer',()=>{
  it('waits for fresh resources and clock synchronization when recovering',()=>{
    const f=fixture({version:1,owner:'device',epoch:9,transfer:{id:'transfer-9',epoch:9,from:'desktop',to:'device',phase:'arriving'}});
    f.p.setDeviceReady(false);
    f.p.receive('desktop',{t:'transfer_report',id:'transfer-9',epoch:9,hidden:true});
    expect(f.p.snapshot().transfer?.phase).toBe('recovering');
    f.p.setDeviceReady(true);
    expect(f.sent.at(-1)?.msg.t).toBe('transfer_status');
    f.p.receive('desktop',{t:'transfer_report',id:'transfer-9',epoch:9,hidden:true});
    expect(f.p.snapshot().transfer?.phase).toBe('arriving');
  });
  it('does not advance ownership or send departure if the journal cannot be saved',async()=>{
    const sent:Message[]=[];const p=new PresenceCoordinator({now:()=>0,save:()=>{throw new Error('disk full');},send:(_to,m)=>{sent.push(m);return true;}});
    p.connected('desktop');p.connected('device');
    await expect(p.request('device')).rejects.toThrow('disk full');
    expect(p.snapshot()).toEqual({version:1,owner:'desktop',epoch:0,transfer:null});
    expect(sent.some(m=>m.t==='transfer_prepare'||m.t==='transfer_depart')).toBe(false);
  });
  it('reconciles every persisted transfer phase in both directions without timing out into duplicates',()=>{
    for(const to of ['desktop','device'] as const)for(const phase of ['preparing','departing','arriving'] as const){
      const from=to==='desktop'?'device':'desktop';
      const f=fixture({version:1,owner:phase==='arriving'?to:from,epoch:7,transfer:{id:'transfer-7',epoch:7,from,to,phase}});
      f.advance(999999);expect(f.p.snapshot().transfer?.phase).toBe('recovering');
      f.p.receive(to,{t:'transfer_report',id:'transfer-7',epoch:7,hidden:true});
      expect(f.sent.some(x=>x.msg.t==='transfer_arrive')).toBe(false);
      f.p.receive(from,{t:'transfer_report',id:'transfer-7',epoch:7,hidden:true});
      const count=f.sent.filter(x=>x.msg.t==='transfer_arrive').length;
      f.p.receive(from,{t:'transfer_report',id:'transfer-7',epoch:7,hidden:true});
      expect(f.sent.filter(x=>x.msg.t==='transfer_arrive')).toHaveLength(count);
      f.p.receive(to,{t:'transfer_arrived',id:'transfer-6',epoch:6});expect(f.p.transferring).toBe(true);
      f.p.receive(to,{t:'transfer_arrived',id:'transfer-7',epoch:7});expect(f.p.owner).toBe(to);expect(f.p.transferring).toBe(false);
    }
  });
  it('waits for the source to hide, then schedules two distinct 500ms phases',async()=>{
    const f=fixture();const done=f.p.request('device');const tr=f.p.snapshot().transfer!;
    expect(f.sent.filter(e=>e.msg.t==='transfer_arrive')).toHaveLength(0);
    f.p.receive('device',{t:'transfer_ready',...tr});
    expect(f.p.owner).toBe('desktop');
    f.advance(950);f.p.receive('desktop',{t:'transfer_hidden',id:tr.id,epoch:tr.epoch,at:f.now()});
    const arrival=f.sent.find(e=>e.msg.t==='transfer_arrive')!;
    expect(arrival.to).toBe('device');expect(arrival.msg.glowAt).toBe(1450);expect((arrival.msg.timing as {glow:number}).glow).toBe(500);
    f.p.receive('device',{t:'transfer_arrived',id:tr.id,epoch:tr.epoch});await done;
    expect(f.p.snapshot()).toMatchObject({owner:'device',transfer:null});
  });
  it('coalesces repeat summons and rejects opposite transfers',async()=>{
    const f=fixture();const a=f.p.request('device'), b=f.p.request('device');
    await expect(f.p.request('desktop')).rejects.toThrow('另一方向');
    expect(f.sent.filter(x=>x.msg.t==='transfer_prepare')).toHaveLength(1);
    const tr=f.p.snapshot().transfer!;f.p.receive('device',{t:'transfer_ready',...tr});f.p.receive('desktop',{t:'transfer_hidden',...tr});f.p.receive('device',{t:'transfer_arrived',...tr});await Promise.all([a,b]);
  });
  it('rejects stale receipts and receipts from the wrong body',()=>{
    const f=fixture();void f.p.request('device').catch(()=>{});const tr=f.p.snapshot().transfer!;
    f.p.receive('desktop',{t:'transfer_ready',...tr});f.p.receive('device',{t:'transfer_ready',...tr,epoch:0});
    expect(f.p.snapshot().transfer?.phase).toBe('preparing');
  });
  it('preparation timeout leaves the original visible owner intact',async()=>{
    const f=fixture();const result=f.p.request('device');const checked=expect(result).rejects.toThrow('准备超时');
    f.advance(5001);await checked;expect(f.p.owner).toBe('desktop');expect(f.p.transferring).toBe(false);
  });
  it('after departure timeout does not recreate a desktop body',async()=>{
    const f=fixture();const result=f.p.request('device');const checked=expect(result).rejects.toThrow('核对');const tr=f.p.snapshot().transfer!;
    f.p.receive('device',{t:'transfer_ready',...tr});f.p.receive('desktop',{t:'transfer_hidden',...tr});f.advance(7001);await checked;
    expect(f.p.owner).toBe('device');expect(f.p.snapshot().transfer?.phase).toBe('recovering');
    expect(f.sent.filter(x=>x.msg.t==='presence').at(-1)?.msg.transfer).not.toBeNull();
  });
  it('restarts conservatively and needs a source hidden report',()=>{
    const f=fixture({version:1,owner:'device',epoch:4,transfer:{id:'transfer-4',epoch:4,from:'desktop',to:'device',phase:'arriving'}});
    expect(f.p.snapshot().transfer?.phase).toBe('recovering');
    f.p.receive('device',{t:'transfer_report',id:'transfer-4',epoch:4,hidden:true});
    expect(f.p.snapshot().transfer?.phase).toBe('recovering');
    f.p.receive('desktop',{t:'transfer_report',id:'transfer-4',epoch:4,hidden:true});
    expect(f.p.snapshot().transfer?.phase).toBe('arriving');
  });
  it('does not migrate autonomously during conversation or low battery',()=>{
    const f=fixture();f.p.setBusy(true);f.advance(120001);f.p.tick(true);expect(f.p.transferring).toBe(false);
    f.p.setBusy(false);f.p.setBattery(10);f.advance(120001);f.p.tick(true);expect(f.p.transferring).toBe(false);
  });
  it('return completes only after the desktop landing acknowledgement',async()=>{
    const f=fixture({version:1,owner:'device',epoch:2,transfer:null});let resolved=false;
    const done=f.p.request('desktop').then(()=>{resolved=true;});const tr=f.p.snapshot().transfer!;
    f.p.receive('desktop',{t:'transfer_ready',...tr});f.p.receive('device',{t:'transfer_hidden',...tr});
    await Promise.resolve();expect(resolved).toBe(false);
    f.p.receive('desktop',{t:'transfer_arrived',...tr});await done;expect(f.p.owner).toBe('desktop');
  });
});
