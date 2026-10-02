/* MIT. A deterministic sampling of the original procedural rig, not a new character model.
 * The source has no fixed frame count. Walk keeps the Coo atlas's eight phases; finite clips
 * keep coop_state.c's durations. Body position/rotation/portal stay continuous on the board.
 */
import {createWhaleFigure} from './figure.js';
import {FACES,STAND} from '../pet-core.js';
import {encodeAtlas,base64} from '../atlas-codec.js';
export const WHALE_CLIPS=[
  [0,'neutral',4,3200,1],[1,'happy',2,1600,1],[2,'wink',2,1600,1],[3,'love',2,1600,1],
  [4,'shy',2,1600,1],[5,'surprised',2,1200,1],[6,'angry',2,1600,1],[7,'sad',2,1600,1],
  [8,'sleepy',2,2400,1],[9,'sleep',2,3600,1],[10,'dizzy',2,1800,1],[11,'dragged',2,1200,1],
  [12,'thinking',2,2400,1],[13,'sit',2,2400,1],[14,'walk',8,667,1],[15,'run',8,480,1],
  [16,'jump',6,700,0],[17,'sway',6,1200,0],[18,'stumble',6,1500,0],[19,'fall',6,800,0],
  [20,'getup',6,1200,0],[21,'cry',4,3000,0],[22,'sulk',3,4000,0],[23,'listening',2,1800,1],
  [24,'speaking',4,1400,1],[25,'depart',6,950,0],[26,'arrive',6,1300,0],[27,'settle',4,650,0],
  [32,'worried',2,1600,1],[33,'furious',2,1200,1],[34,'smug',2,2000,1],[35,'pleading',2,1800,1],
  [36,'curious',2,2000,1],[37,'excited',2,1400,1],[38,'crying',2,1800,1],[39,'pout',2,2000,1],
].map(([id,name,count,ms,loop])=>({id,name,count,ms,loop:!!loop}));
const clamp=(n,a,b)=>Math.max(a,Math.min(b,n));
export function whaleFrame(name,time,progress=0) {
  const o={t:time,mode:'idle',face:name,look:[0,0],blink:0,eyeClose:0,talk:0,low:0,sit:0,swing:0,tilt:0,lean:0,facing:1,legs:STAND.map(l=>[...l]),progress};
  if(name==='neutral')o.blink=progress>.65&&progress<.8?1:0;
  if(name==='walk'||name==='run') {o.mode=name;o.face=name==='run'?'run':'neutral';const phase=progress*Math.PI*2;
    o.legs.forEach((l,i)=>{l[2]+=Math.sin(phase+i*Math.PI)*25;l[3]-=Math.max(0,Math.cos(phase+i*Math.PI))*18;});o.swing=Math.sin(phase)*10;o.low=Math.abs(Math.sin(phase))*2;
  }
  if(name==='sit'||name==='sleep'){o.mode=name;o.face=name==='sit'?'content':'sleep';o.low=29;o.sit=1;}
  if(name==='dragged'){o.mode='drag';o.swing=12*Math.sin(time*9);}
  if(name==='jump'||name==='depart'||name==='arrive'){o.mode='air';o.face='surprised';if(name==='depart'&&progress<.3){o.mode='crouch';o.low=16*Math.sin(progress/.3*Math.PI);}}
  if(name==='sway'||name==='settle'){o.face='neutral';o.swing=18*Math.sin(progress*Math.PI*3)*(1-progress);o.tilt=8*Math.sin(progress*Math.PI*2);}
  if(name==='stumble'){o.mode=name;o.face='worried';o.swing=22*Math.sin(progress*Math.PI*4)*(1-progress);}
  if(name==='fall'){o.mode=name;o.face=progress<.4?'surprised':'dizzy';o.swing=20*Math.sin(progress*Math.PI);o.low=6*progress;}
  if(name==='getup'){o.mode=name;o.face=progress<.65?'worried':'neutral';o.low=12*(1-progress);}
  if(name==='cry'){o.face='crying';o.swing=3*Math.sin(time*13);}
  if(name==='sulk')o.face='sulking';
  if(name==='listening'){o.face='listening';o.look=[-2,-1];}
  if(name==='speaking'){o.face='neutral';o.talk=.3+.6*Math.abs(Math.sin(time*8));}
  if(name==='curious')o.look=[2,-2];
  return o;
}
export async function createWhaleSampler(scheme='deepseek') {
  const figure=await createWhaleFigure(undefined,{raster:true,scheme});figure.resolution(2);
  const group=document.createElementNS('http://www.w3.org/2000/svg','g');
  const canvas=document.createElement('canvas');canvas.width=192;canvas.height=216;const ctx=canvas.getContext('2d',{willReadFrequently:true});
  const paint=(name,t,p)=>{const o=whaleFrame(name,t,p);figure.draw(group,(FACES[o.face]||FACES.neutral).f(t),o);};
  async function capture(width=192,height=216) {
    const svg=`<svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="${height}" viewBox="-32 -48 320 360">${group.outerHTML}</svg>`;
    const image=new Image();image.src='data:image/svg+xml;charset=utf-8,'+encodeURIComponent(svg);await image.decode();canvas.width=width;canvas.height=height;ctx.clearRect(0,0,width,height);ctx.drawImage(image,0,0,width,height);return ctx.getImageData(0,0,width,height).data;
  }
  return {figure,paint,capture,canvas,group,
    async clip(clip){figure.reset();for(let i=0;i<60;i++)paint(clip.name,i/60,0);const frames=[];let previous=1;
      for(let f=0;f<clip.count;f++){const progress=f/(clip.loop?clip.count:Math.max(1,clip.count-1)),at=1+progress*clip.ms/1000;for(let t=previous+1/60;t<at;t+=1/60)paint(clip.name,t,clamp((t-1)*1000/clip.ms,0,1));paint(clip.name,at,progress);frames.push(await capture());previous=at;}return frames;
    },
    dispose(){figure.dispose();}
  };
}
export async function exportWhale(skin,onProgress=()=>{}) {
  const sampler=await createWhaleSampler(skin?.scheme),clips=WHALE_CLIPS.map(c=>({...c})),frames=[];
  try {for(const c of clips){frames.push(...await sampler.clip(c));onProgress(c.name,frames.length);}return base64(encodeAtlas(clips,frames));}finally{sampler.dispose();}
}
