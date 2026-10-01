/* MIT. Rasterize the upstream SVG renderer into a small, bounded RLE atlas. */
const {app,BrowserWindow}=require('electron');
const fs=require('node:fs'),path=require('node:path');
const args=process.argv.slice(2),out=args[0],skinFile=args[1];
if(!out){console.error('usage: electron export-coo.cjs OUTPUT [SKIN_JSON]');app.exit(2);}
const poses=['neutral','happy','wink','love','shy','surprised','angry','sad','sleepy','sleep','dizzy','dragged','thinking','sit',...Array.from({length:8},(_,i)=>`walk${i}`)];
function encode(bitmap){
  const runs=[];let previous=-1,count=0;
  const flush=()=>{if(!count)return;const b=Buffer.alloc(5);b.writeUInt16LE(count);b.writeUInt16LE(previous&65535,2);b[4]=previous>>>16;runs.push(b);};
  for(let i=0;i<bitmap.length;i+=4){const blue=bitmap[i],green=bitmap[i+1],red=bitmap[i+2],alpha=bitmap[i+3];const rgb=((red>>3)<<11)|((green>>2)<<5)|(blue>>3),pixel=alpha===0?0:rgb|(alpha<<16);if(pixel===previous&&count<65535)count++;else{flush();previous=pixel;count=1;}}
  flush();return Buffer.concat(runs);
}
app.whenReady().then(async()=>{
  const win=new BrowserWindow({show:false,width:192,height:216,useContentSize:true,backgroundColor:'#00000000',webPreferences:{offscreen:true,backgroundThrottling:false,contextIsolation:false,nodeIntegration:false}});
  await win.loadFile(path.join(__dirname,'../web/coo-export.html'));
  const skin=skinFile?JSON.parse(fs.readFileSync(skinFile,'utf8')):{};
  const chunks=[];
  for(const pose of poses){await win.webContents.executeJavaScript(`window.pose(${JSON.stringify(pose)},${JSON.stringify(skin)})`);const image=await win.webContents.capturePage({x:0,y:0,width:192,height:216});const bmp=image.resize({width:192,height:216}).toBitmap();chunks.push(encode(bmp));}
  const header=Buffer.alloc(12+poses.length*8);header.write('COO1');header.writeUInt16LE(192,4);header.writeUInt16LE(216,6);header.writeUInt16LE(poses.length,8);
  let offset=header.length;chunks.forEach((b,i)=>{header.writeUInt32LE(offset,12+i*8);header.writeUInt32LE(b.length,16+i*8);offset+=b.length;});
  if(offset>1536*1024)throw new Error('Coo atlas exceeds the asset slot budget');
  fs.mkdirSync(path.dirname(path.resolve(out)),{recursive:true});fs.writeFileSync(out,Buffer.concat([header,...chunks]));
  fs.writeFileSync(`${out}.json`,JSON.stringify({format:'COO1',width:192,height:216,poses,bytes:offset,skin},null,2));
  console.log(`Coo atlas: ${poses.length} poses, ${offset} bytes`);win.destroy();app.quit();
}).catch(e=>{console.error(e);app.exit(1);});
