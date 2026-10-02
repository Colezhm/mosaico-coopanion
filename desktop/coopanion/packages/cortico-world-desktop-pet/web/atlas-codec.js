/* MIT. COO2: bounded palette animation clips, independently checked on the board.
 * Header (24): magic, u16 width/height/frames/clips/colors/figure, u32 bytes, reserved.
 * Clips (12): u16 id/start/count/durationMs/loop/reserved.
 * Frames (12): u32 offset/length, u16 base/reserved. base = previous frame or 0xffff.
 * Palette: RGB565 LE + alpha; frame = LZ4 block of palette indices (XOR base if delta).
 */
// The unchanged 1 MiB cache partition keeps 24 KiB for its header and erase alignment.
export const ATLAS_LIMIT = 1000 * 1024, PIXELS = 192 * 216;
const key = (r,g,b,a) => a < 16 ? 0 : ((r>>3)<<19)|((g>>3)<<14)|((b>>3)<<9)|((a+8>>4)<<4);
const unpack = k => [(k>>>19&31)*255/31,(k>>>14&31)*255/31,(k>>>9&31)*255/31,Math.min(255,k&511)];

/** Weighted median-cut palette, shared across all clips; alpha stays in the palette. */
export function quantize(frames) {
  const hist=new Map();
  for(const rgba of frames)for(let i=0;i<rgba.length;i+=4){const k=key(...rgba.subarray(i,i+4));if(k)hist.set(k,(hist.get(k)||0)+1);}
  const points=[...hist].map(([k,n])=>({k,n,c:unpack(k)}));
  const box=p=>{const lo=[255,255,255,255],hi=[0,0,0,0];let n=0;for(const q of p){n+=q.n;for(let c=0;c<4;c++){lo[c]=Math.min(lo[c],q.c[c]);hi[c]=Math.max(hi[c],q.c[c]);}}const range=hi.map((v,i)=>(v-lo[i])*(i===3?1.6:1));return {p,n,axis:range.indexOf(Math.max(...range)),score:Math.max(...range)*Math.sqrt(n)};};
  const boxes=points.length?[box(points)]:[];
  while(boxes.length<127){let best=-1;for(let i=0;i<boxes.length;i++)if(boxes[i].p.length>1&&(best<0||boxes[i].score>boxes[best].score))best=i;if(best<0)break;
    const b=boxes.splice(best,1)[0];b.p.sort((a,c)=>a.c[b.axis]-c.c[b.axis]);let n=0,m=0;for(;m<b.p.length-1;m++){n+=b.p[m].n;if(n>=b.n/2)break;}m=Math.min(m,b.p.length-2);boxes.push(box(b.p.slice(0,m+1)),box(b.p.slice(m+1)));
  }
  const palette=[[0,0,0,0]],map=new Map([[0,0]]);
  for(const b of boxes){const c=[0,0,0,0];for(const p of b.p)for(let i=0;i<4;i++)c[i]+=p.c[i]*p.n;palette.push(c.map(v=>Math.round(v/b.n)));for(const p of b.p)map.set(p.k,palette.length-1);}
  return {palette,frames:frames.map(rgba=>{const out=new Uint8Array(PIXELS);for(let i=0;i<PIXELS;i++)out[i]=map.get(key(...rgba.subarray(i*4,i*4+4)));return out;})};
}

/** Small deterministic LZ4 block encoder. No native/cloud dependency in the renderer. */
export function packLz4(src) {
  const out=[],table=new Int32Array(65536).fill(-1),chain=new Int32Array(src.length).fill(-1),n=src.length;
  const hash=i=>Math.imul((src[i]|src[i+1]<<8|src[i+2]<<16|src[i+3]<<24),-1640531535)>>>16;
  const len=n=>{while(n>=255){out.push(255);n-=255;}out.push(n);};
  let at=0,anchor=0;
  while(at+12<n){const h=hash(at);let candidate=table[h],ref=-1,match=3;chain[at]=candidate;table[h]=at;
    for(let tries=0;candidate>=0&&at-candidate<=65535&&tries<64;tries++,candidate=chain[candidate]){
      if(src[candidate]!==src[at]||src[candidate+1]!==src[at+1]||src[candidate+2]!==src[at+2]||src[candidate+3]!==src[at+3])continue;
      let m=4;while(at+m<n-5&&src[candidate+m]===src[at+m])m++;
      if(m>match){match=m;ref=candidate;if(m>=512)break;}
    }
    if(ref<0){at++;continue;}
    const lit=at-anchor;out.push(Math.min(15,lit)<<4|Math.min(15,match-4));if(lit>=15)len(lit-15);for(let i=anchor;i<at;i++)out.push(src[i]);const d=at-ref;out.push(d&255,d>>8);if(match>=19)len(match-19);
    const end=at+match;for(let i=at+1;i<end&&i+4<n;i++){const h=hash(i);chain[i]=table[h];table[h]=i;}at=end;anchor=at;
  }
  const lit=n-anchor;out.push(Math.min(15,lit)<<4);if(lit>=15)len(lit-15);for(let i=anchor;i<n;i++)out.push(src[i]);return Uint8Array.from(out);
}
export function unpackLz4(src, size=PIXELS) {
  const dst=new Uint8Array(size);let i=0,j=0;
  const length=base=>{let n=base;if(base===15){let v;do{if(i>=src.length)throw Error('LZ4 length');v=src[i++];n+=v;if(n>size)throw Error('LZ4 overflow');}while(v===255);}return n;};
  while(i<src.length){const token=src[i++],lit=length(token>>4);if(i+lit>src.length||j+lit>size)throw Error('LZ4 literals');dst.set(src.subarray(i,i+lit),j);i+=lit;j+=lit;if(i===src.length){if((token&15)||j!==size)throw Error('LZ4 end');return dst;}if(i+2>src.length)throw Error('LZ4 offset');const off=src[i++]|src[i++]<<8;if(!off||off>j)throw Error('LZ4 reference');const match=length(token&15)+4;if(j+match>size)throw Error('LZ4 match');for(let k=0;k<match;k++,j++)dst[j]=dst[j-off];}
  throw Error('LZ4 truncated');
}
export function encodeAtlas(clips, rgbaFrames) {
  const {palette,frames}=quantize(rgbaFrames),chunks=[],bases=[];let start=0;
  for(const clip of clips){clip.start=start;for(let i=0;i<clip.count;i++,start++){
    let bytes=packLz4(frames[start]),base=65535;
    if(i){const delta=frames[start].map((v,k)=>v^frames[start-1][k]),packed=packLz4(delta);if(packed.length<bytes.length){bytes=packed;base=start-1;}}
    chunks.push(bytes);bases.push(base);
  }}
  const header=24+clips.length*12+frames.length*12+palette.length*3,length=header+chunks.reduce((s,c)=>s+c.length,0);
  if(length>ATLAS_LIMIT)throw Error(`造型资源 ${length} 字节，超过 ${ATLAS_LIMIT} 字节容量`);
  const bytes=new Uint8Array(length),v=new DataView(bytes.buffer);bytes.set([67,79,79,50]);[192,216,frames.length,clips.length,palette.length,1].forEach((n,i)=>v.setUint16(4+i*2,n,true));v.setUint32(16,length,true);
  clips.forEach((c,i)=>[c.id,c.start,c.count,c.ms,c.loop?1:0,0].forEach((n,k)=>v.setUint16(24+i*12+k*2,n,true)));
  const indexes=24+clips.length*12;let offset=header;
  chunks.forEach((c,i)=>{v.setUint32(indexes+i*12,offset,true);v.setUint32(indexes+i*12+4,c.length,true);v.setUint16(indexes+i*12+8,bases[i],true);bytes.set(c,offset);offset+=c.length;});
  palette.forEach(([r,g,b,a],i)=>{const p=indexes+frames.length*12+i*3;v.setUint16(p,(r>>3)<<11|(g>>2)<<5|(b>>3),true);bytes[p+2]=a;});
  return bytes;
}

/** Exact structural + decompression validation. C reader enforces the same bounds. */
export function validAnimationAtlas(bytes) {
  try {
    if(bytes.length<24||bytes.length>ATLAS_LIMIT)return false;const v=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
    if(v.getUint32(0,true)!==0x324f4f43||v.getUint16(4,true)!==192||v.getUint16(6,true)!==216||v.getUint16(14,true)!==1||v.getUint32(16,true)!==bytes.length||v.getUint32(20,true))return false;
    const n=v.getUint16(8,true),c=v.getUint16(10,true),p=v.getUint16(12,true);if(!n||n>512||!c||c>64||p<2||p>256)return false;
    const index=24+c*12,header=index+n*12+p*3;if(header>bytes.length)return false;
    let frame=0,end=header;const ids=new Set();
    for(let k=0;k<c;k++){const o=24+k*12,id=v.getUint16(o,true),start=v.getUint16(o+2,true),count=v.getUint16(o+4,true),ms=v.getUint16(o+6,true);if(id>=64||ids.has(id)||start!==frame||!count||count>32||frame+count>n||!ms||v.getUint16(o+8,true)>1||v.getUint16(o+10,true))return false;ids.add(id);
      let prev=null;for(let j=0;j<count;j++,frame++){const a=index+frame*12,off=v.getUint32(a,true),len=v.getUint32(a+4,true),base=v.getUint16(a+8,true);if(off!==end||!len||len>bytes.length-off||v.getUint16(a+10,true)||(base!==65535&&(j===0||base!==frame-1)))return false;end=off+len;const decoded=unpackLz4(bytes.subarray(off,end));for(let q=0;q<PIXELS;q++){if(base!==65535)decoded[q]^=prev[q];if(decoded[q]>=p)return false;}prev=decoded;}
    }
    return frame===n&&end===bytes.length&&ids.has(0);
  } catch {return false;}
}
export function base64(bytes){let s='';for(let i=0;i<bytes.length;i+=8192)s+=String.fromCharCode(...bytes.subarray(i,i+8192));return btoa(s);}
