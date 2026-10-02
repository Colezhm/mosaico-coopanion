import { figure, FACES, STAND, normalizeSkin, skinVars } from './pet-core.js';
import {base64,validAnimationAtlas} from './atlas-codec.js';

export const COOP_POSES = ['neutral','happy','wink','love','shy','surprised','angry','sad','sleepy','sleep','dizzy','dragged','thinking','sit',...Array.from({length:8},(_,i)=>`walk${i}`)];
/** Export the original SVG geometry into the board's bounded RGB565+alpha atlas. */
export async function exportCoo(skin) {
  if(skin?.figure==='whale'){
    if(!skin.scheme||skin.scheme==='deepseek'){
      const response=await fetch(new URL('./whale/mosaico-deepseek.bin',import.meta.url));
      if(!response.ok)throw Error('大肥鱼预编译动画资源缺失，请重新安装桌面端');
      const bytes=new Uint8Array(await response.arrayBuffer());
      if(!validAnimationAtlas(bytes))throw Error('大肥鱼动画资源损坏');
      return base64(bytes);
    }
    return (await import('./whale/mosaico.js')).exportWhale(skin);
  }
  const s=normalizeSkin(skin), css=(await (await fetch('/web/pet.css')).text()).split('\n').filter(line=>/^\.(ink|c-|f-)/.test(line)).join('\n');
  const canvas=document.createElement('canvas');canvas.width=192;canvas.height=216;
  const ctx=canvas.getContext('2d',{willReadFrequently:true}), chunks=[];
  for(const name of COOP_POSES){
    let legs=STAND.map(l=>[...l]),low=0,t=0;
    if(name.startsWith('walk')){t=Number(name.slice(4))/8;for(let i=0;i<2;i++){legs[i][2]+=Math.sin(t*Math.PI*2+i*Math.PI)*25;legs[i][3]-=Math.max(0,Math.cos(t*Math.PI*2+i*Math.PI))*18;}}
    if(name==='sit'||name==='sleep'){low=30;legs=legs.map(l=>[l[0],l[1]+30,l[2]+18,256]);}
    const body=figure((FACES[name]||FACES.neutral).f(t),{look:[0,0],legs,low,t,blink:0,acc:s,swing:0});
    const svg=`<svg xmlns="http://www.w3.org/2000/svg" width="192" height="216" viewBox="-32 -48 320 360" style="${skinVars(s,true)}"><style>${css}</style>${body}</svg>`;
    const url='data:image/svg+xml;charset=utf-8,'+encodeURIComponent(svg);
    try {
      const img=new Image();img.src=url;await img.decode();ctx.clearRect(0,0,192,216);ctx.drawImage(img,0,0);
      const pixels=ctx.getImageData(0,0,192,216).data,runs=[];let previous=-1,count=0;
      const flush=()=>{if(count)runs.push(count&255,count>>8,previous&255,(previous>>8)&255,previous>>>16);};
      for(let i=0;i<pixels.length;i+=4){const a=pixels[i+3],rgb=((pixels[i]>>3)<<11)|((pixels[i+1]>>2)<<5)|(pixels[i+2]>>3),value=a?rgb|(a<<16):0;
        if(value===previous&&count<65535)count++;else{flush();previous=value;count=1;}}
      flush();chunks.push(new Uint8Array(runs));
    } finally { /* The data URL needs no object-URL lifecycle or broader CSP. */ }
  }
  const length=188+chunks.reduce((sum,a)=>sum+a.length,0);if(length>900*1024)throw new Error('造型资源超过板端容量');
  const bytes=new Uint8Array(length),view=new DataView(bytes.buffer);bytes.set([67,79,79,49]);view.setUint16(4,192,true);view.setUint16(6,216,true);view.setUint16(8,22,true);
  let offset=188;chunks.forEach((a,i)=>{view.setUint32(12+i*8,offset,true);view.setUint32(16+i*8,a.length,true);bytes.set(a,offset);offset+=a.length;});
  let binary='';for(let i=0;i<bytes.length;i+=8192)binary+=String.fromCharCode(...bytes.subarray(i,i+8192));return btoa(binary);
}
