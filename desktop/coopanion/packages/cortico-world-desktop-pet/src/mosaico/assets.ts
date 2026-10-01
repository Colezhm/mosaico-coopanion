import { createHash, randomUUID } from 'node:crypto';
import type { Message } from './coordinator.ts';

export function validAtlas(bytes: Buffer): boolean {
  if(bytes.length<188||bytes.length>900*1024||bytes.toString('ascii',0,4)!=='COO1'||bytes.readUInt16LE(4)!==192||bytes.readUInt16LE(6)!==216||bytes.readUInt16LE(8)!==22)return false;
  for(let i=0;i<22;i++){
    const offset=bytes.readUInt32LE(12+i*8),length=bytes.readUInt32LE(16+i*8);
    if(offset<188||offset+length>bytes.length||length%5)return false;
    let pixels=0;for(let at=offset;at<offset+length;at+=5){pixels+=bytes.readUInt16LE(at);if(pixels>192*216)return false;}
    if(pixels!==192*216)return false;
  }
  return true;
}
/** Stop-and-wait 4 KiB chunks at <= 20 Hz; controls and audio keep their own budget. */
export class AssetSync {
  private id='';private bytes:Buffer|null=null;private hash='';private offset=0;private deadline=0;private retry=0;private pending:Message|null=null;
  constructor(private readonly send:(m:Message)=>boolean,private readonly render:(m:Message)=>boolean,private readonly ready:(value:boolean)=>void,private readonly error:(text:string)=>void,private readonly now=()=>performance.now()){}
  start(skin:unknown):void {this.cancel();if(skin&&typeof skin==='object'&&'figure' in skin&&skin.figure&&skin.figure!=='coo'){this.error('Mosaico 首版仅支持 Coo，请先切回 Coo 造型');return;}this.id=randomUUID();if(!this.render({t:'asset_export',id:this.id,skin}))this.error('桌面角色窗口尚未连接');this.deadline=this.now()+10000;}
  cancel():void{this.ready(false);this.id='';this.bytes=null;this.pending=null;this.offset=0;this.retry=0;this.deadline=0;}
  exported(m:Message):void {
    if(m.id!==this.id)return;
    if(typeof m.data!=='string'||m.data.length>1230000){this.fail(String(m.error||'角色资源导出失败'));return;}
    const data=Buffer.from(m.data,'base64');if(!validAtlas(data)){this.fail('角色资源格式校验失败');return;}
    this.bytes=data;this.hash=createHash('sha256').update(data).digest('hex');this.issue({t:'asset_offer',id:this.id,sha256:this.hash,size:data.length});
  }
  receive(m:Message):boolean{
    if(!m.t.startsWith('asset_'))return false;if(m.id!==this.id)return true;
    if(m.t==='asset_ready'&&m.sha256===this.hash){this.ready(true);this.pending=null;this.deadline=0;return true;}
    if(m.t==='asset_error'){this.fail(String(m.error||'板端资源同步失败'));return true;}
    if(m.t==='asset_next'&&this.bytes&&Number.isSafeInteger(m.offset)&&Number(m.offset)>=0&&Number(m.offset)<=this.bytes.length){
      this.offset=Number(m.offset);this.pending=null;this.retry=0;this.deadline=this.now()+50;
    }return true;
  }
  tick():void{
    if(!this.id||!this.deadline||this.now()<this.deadline)return;
    if(this.pending){if(++this.retry>3){this.fail('资源同步超时');return;}this.send(this.pending);this.deadline=this.now()+2000;return;}
    if(!this.bytes){this.fail('桌面资源导出超时');return;}
    if(this.offset===this.bytes.length)this.issue({t:'asset_commit',id:this.id,sha256:this.hash});
    else this.issue({t:'asset_chunk',id:this.id,offset:this.offset,data:this.bytes.subarray(this.offset,this.offset+4096).toString('base64')});
  }
  private issue(m:Message):void{this.pending=m;this.retry=0;this.send(m);this.deadline=this.now()+2000;}
  private fail(text:string):void{this.cancel();this.error(text);}
}
