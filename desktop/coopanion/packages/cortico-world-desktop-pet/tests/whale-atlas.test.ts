import {describe,it,expect} from 'vitest';
import {readFileSync} from 'node:fs';
import {packLz4,unpackLz4,encodeAtlas,validAnimationAtlas,PIXELS} from '../web/atlas-codec.js';
import {AssetSync,validAtlas} from '../src/mosaico/assets.ts';
import {parseActions} from '../src/script.ts';
import type {Message} from '../src/mosaico/coordinator.ts';

describe('whale animation resource contract',()=>{
  it('round trips noisy, repeated, overlapping and sparse LZ4 blocks',()=>{
    let seed=13;
    for(const kind of ['zero','noise','runs','sparse']){
      const bytes=new Uint8Array(PIXELS);
      for(let i=0;i<bytes.length;i++){seed=(Math.imul(seed,1664525)+1013904223)>>>0;bytes[i]=kind==='zero'?0:kind==='noise'?seed>>>24:kind==='runs'?Math.floor(i/24)%96:i%400===0?63:0;}
      expect(unpackLz4(packLz4(bytes))).toEqual(bytes);
    }
    for(const bytes of [[0],[0,0,0],[0xf0,255],[0x10,12,1,0]])expect(()=>unpackLz4(Uint8Array.from(bytes))).toThrow();
  });
  it('bounds clip metadata, palette and decompression before accepting a resource',()=>{
    const a=new Uint8Array(PIXELS*4),b=new Uint8Array(a.length);
    for(let i=0;i<100;i++){a.set([45,105,208,255],i*4);b.set([45,105,208,255],(i+2)*4);}
    const encoded=encodeAtlas([{id:0,count:2,ms:900,loop:true}],[a,b]);
    expect(validAnimationAtlas(encoded)).toBe(true);
    for(const [at,value] of [[16,0],[26,1],[28,33],[30,0],[44,0],[24,64]] as const){const bad=encoded.slice();new DataView(bad.buffer).setUint16(at,value,true);expect(validAnimationAtlas(bad)).toBe(false);}
    expect(validAnimationAtlas(encoded.subarray(0,encoded.length-1))).toBe(false);
  });
  it('validates every shipped frame and rejects corrupted offsets and delta references',()=>{
    const bytes=readFileSync(new URL('../web/whale/mosaico-deepseek.bin',import.meta.url));
    expect(validAtlas(bytes)).toBe(true);expect(bytes.length).toBeLessThanOrEqual(1000*1024);
    const index=24+bytes.readUInt16LE(10)*12;
    for(const offset of [index,index+8,index+12+8]){const bad=Buffer.from(bytes);bad.writeUInt16LE(0xfffe,offset);expect(validAtlas(bad)).toBe(false);}
  });
  it('exports whale skins, ignores stale exports and keeps readiness gated on the cache receipt',()=>{
    let exported:Message={t:''};let ready=true;const errors:string[]=[],sent:Message[]=[];
    const sync=new AssetSync(m=>{sent.push(m);return true;},m=>{exported=m;return true;},v=>{ready=v;},e=>errors.push(e));
    sync.start({figure:'whale',scheme:'deepseek'});expect(exported.t).toBe('asset_export');expect(errors).toEqual([]);expect(ready).toBe(false);
    const id=exported.id;sync.start({figure:'coo'});sync.exported({t:'asset_exported',id,data:'ignored'});expect(sent).toEqual([]);
    expect(parseActions(['委屈','炸毛','得意','撒娇','好奇','雀跃','哭泣','鼓脸','生闷气','慌张','眉开眼笑','比耶']).dropped).toEqual([]);
  });
});
