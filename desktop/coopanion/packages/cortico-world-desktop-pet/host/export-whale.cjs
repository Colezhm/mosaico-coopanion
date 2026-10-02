/* MIT. Isolated asset generation; no live bot, model calls, settings, or device access. */
const {app,BrowserWindow}=require('electron');
const fs=require('node:fs'),path=require('node:path'),http=require('node:http');
const [out,mode='atlas',scheme='deepseek']=process.argv.slice(2);
if(!out){console.error('usage: electron export-whale.cjs OUTPUT [atlas|FACE] [SCHEME]');app.exit(2);}
app.whenReady().then(async()=>{
 const root=path.resolve(__dirname,'../web');
 const server=http.createServer((req,res)=>{const file=path.resolve(root,'.'+decodeURIComponent(new URL(req.url,'http://localhost').pathname));if(!file.startsWith(root+path.sep)){res.writeHead(403);res.end();return;}fs.readFile(file,(error,body)=>{if(error){res.writeHead(404);res.end();return;}res.setHeader('Content-Type',({'.js':'text/javascript','.json':'application/json','.png':'image/png','.html':'text/html'})[path.extname(file)]||'application/octet-stream');res.end(body);});});
 await new Promise(r=>server.listen(0,'127.0.0.1',r));
 const win=new BrowserWindow({show:false,width:768,height:864,webPreferences:{offscreen:true,backgroundThrottling:false,contextIsolation:true,nodeIntegration:false}});
 win.webContents.on('console-message',(_e,_level,message)=>console.log(message));
 await win.loadURL(`http://127.0.0.1:${server.address().port}/whale-export.html`);
 await win.webContents.executeJavaScript('new Promise(r=>{const wait=()=>window.ready?r():setTimeout(wait,30);wait()})');
 const data=await win.webContents.executeJavaScript(mode==='atlas'?`window.exportWhale({scheme:${JSON.stringify(scheme)}},(n,f)=>console.log(n+': '+f))`:`window.still(${JSON.stringify(mode)})`);
 fs.mkdirSync(path.dirname(path.resolve(out)),{recursive:true});fs.writeFileSync(out,Buffer.from(data.replace(/^data:image\/png;base64,/,''),'base64'));
 if(mode==='atlas'){const clips=await win.webContents.executeJavaScript('window.clips');fs.writeFileSync(out+'.json',JSON.stringify({format:'COO2',figure:'whale',scheme,width:192,height:216,bytes:fs.statSync(out).size,clips},null,2));}
 console.log(`${out}: ${fs.statSync(out).size} bytes`);win.destroy();server.close();app.quit();
}).catch(error=>{console.error(error);app.exit(1)});
