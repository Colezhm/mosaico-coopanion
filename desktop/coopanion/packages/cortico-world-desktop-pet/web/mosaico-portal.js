/** Local renderer acknowledgements are sent only after a frame actually hides/lands Coo. */
export function createMosaicoPortal({ ctl, send, stopMic, resumeMic, elements }) {
  const ns = 'http://www.w3.org/2000/svg';
  const svg = document.querySelector('#stageSvg'), pet = document.querySelector('#pet');
  const make = (tag, attrs) => { const el = document.createElementNS(ns, tag); for (const [k,v] of Object.entries(attrs)) el.setAttribute(k,String(v)); return el; };
  const defs = svg.querySelector('defs');
  const gradient = make('radialGradient',{id:'mosaicoPortalLight'});
  gradient.append(make('stop',{offset:0,'stop-color':'#e7fff8','stop-opacity':'.9'}),make('stop',{offset:'.55','stop-color':'#57e8c0','stop-opacity':'.5'}),make('stop',{offset:1,'stop-color':'#2fd59b','stop-opacity':0})); defs.append(gradient);
  const clip=make('clipPath',{id:'mosaicoPortalClip'}), rect=make('rect',{x:0,y:0,width:innerWidth,height:innerHeight});clip.append(rect);defs.append(clip);
  const outer=make('g',{});pet.before(outer);outer.append(pet);
  const back=make('ellipse',{fill:'url(#mosaicoPortalLight)',opacity:0});outer.before(back);
  const front=make('path',{fill:'none',stroke:'#adffdf','stroke-width':3,opacity:0});outer.after(front);
  let enabled=false, owner='desktop', transition=null, hidden=false, frozen=false;
  let savedX=null, lastId='', lastEpoch=0;
  try { const s=JSON.parse(localStorage.getItem('mosaico-presence')||'null');if(s){hidden=s.hidden;lastId=s.id;lastEpoch=s.epoch;savedX=s.x;} } catch {}
  function persist(){localStorage.setItem('mosaico-presence',JSON.stringify({hidden,id:lastId,epoch:lastEpoch,x:savedX}));}
  function visibility(){outer.style.visibility=hidden?'hidden':'';document.querySelector('#shadow').style.visibility=hidden||frozen?'hidden':'';document.querySelector('#fx').style.visibility=hidden||frozen?'hidden':'';for(const e of elements)e.style.visibility=hidden||frozen?'hidden':'';}
  function ack(t){send({t,id:lastId,epoch:lastEpoch,at:performance.now()});}
  function order(m){
    if(m.t==='presence'){
      const first=!enabled;enabled=true;owner=m.owner;frozen=!!m.transfer;
      if(m.transfer&&(owner!=='desktop'||(first&&m.transfer.to==='desktop'))){hidden=true;lastId=m.transfer.id;lastEpoch=m.transfer.epoch;}
      if(!m.transfer){transition=null;hidden=owner!=='desktop';outer.removeAttribute('transform');outer.removeAttribute('clip-path');back.setAttribute('opacity',0);front.setAttribute('opacity',0);if(!hidden){ctl.releaseRoam();resumeMic();}else stopMic();}
      else {ctl.holdRoam(3600);stopMic();}
      visibility();persist();return true;
    }
    if(!m.t.startsWith('transfer_'))return false;
    if(m.t==='transfer_status'){
      const tr=m.transfer;if(tr?.from==='desktop')send({t:'transfer_report',id:tr.id,epoch:tr.epoch,hidden: hidden&&lastId===tr.id});return true;
    }
    if(m.epoch<lastEpoch)return true;
    if(m.t==='transfer_prepare'){lastId=m.id;lastEpoch=m.epoch;ack('transfer_ready');return true;}
    if(m.t==='transfer_depart'){
      if(lastId===m.id&&hidden){ack('transfer_hidden');return true;}
      if(transition?.id===m.id)return true;
      savedX=ctl.pet.x;hidden=false;transition={...m,start:performance.now(),kind:'depart'};
    }else if(m.t==='transfer_arrive'){
      if(transition?.id===m.id)return true;
      ctl.pet.x=Math.max(ctl.bounds.minX,Math.min(ctl.bounds.maxX,savedX??innerWidth/2));
      ctl.pet.fy=ctl.bounds.floorY;ctl.pet.vx=ctl.pet.vy=0;hidden=true;
      // Arrival deadlines from the host use its monotonic clock; host sends a relative delay for this page.
      transition={...m,start:performance.now()+Math.max(0,m.delayMs??500),kind:'arrive'};
    }else return true;
    lastId=m.id;lastEpoch=m.epoch;frozen=true;ctl.act('stand');if(m.emotion)ctl.act(m.emotion);ctl.holdRoam(3600);stopMic();persist();return true;
  }
  function frame(now){
    if(!enabled&&!transition)return;
    if(!transition){visibility();return;}
    const tr=transition, b=ctl.bounds, baseX=savedX??ctl.pet.x;
    const x=Math.max(b.minX,Math.min(b.maxX,baseX+30*ctl.pet.facing));
    const y=b.floorY-2,S=b.S, radius=105*S;
    rect.setAttribute('width',innerWidth);rect.setAttribute('height',y+3);outer.setAttribute('clip-path','url(#mosaicoPortalClip)');
    const t=now-tr.start;let open=0,dy=0,dx=x-ctl.pet.x;
    if(tr.kind==='depart'){
      open=Math.min(1,Math.max(0,t/300));const u=Math.min(1,Math.max(0,(t-300)/650));
      dy=u*u*(300*S+24);dx*=Math.min(1,t/300);
      if(t>=950){hidden=true;persist();transition=null;back.setAttribute('opacity',0);front.setAttribute('opacity',0);visibility();requestAnimationFrame(()=>ack('transfer_hidden'));return;}
    }else{
      open=Math.min(1,Math.max(0,t/250));
      if(t>=500){hidden=false;const u=Math.min(1,(t-500)/600);dy=(1-u)*(300*S+24)-Math.sin(Math.PI*u)*100*S;}
      if(t>=1100)open=1-Math.min(1,(t-1100)/200);
      if(t>=1300){hidden=false;frozen=false;transition=null;ctl.pet.x=x;ctl.pet.fy=b.floorY;outer.removeAttribute('transform');outer.removeAttribute('clip-path');back.setAttribute('opacity',0);front.setAttribute('opacity',0);persist();visibility();requestAnimationFrame(()=>ack('transfer_arrived'));return;}
    }
    // The mask stays at the portal mouth while the body moves through it.
    rect.setAttribute('x',-dx);rect.setAttribute('y',-dy);rect.setAttribute('height',y+3);
    outer.setAttribute('transform',`translate(${dx} ${dy})`);
    back.setAttribute('cx',x);back.setAttribute('cy',y);back.setAttribute('rx',radius*open*1.6);back.setAttribute('ry',radius*.3*open);back.setAttribute('opacity',open);
    front.setAttribute('d',`M${x-radius*open} ${y} A${radius*open||.01} ${radius*.23*open||.01} 0 0 0 ${x+radius*open} ${y}`);front.setAttribute('opacity',open);
    visibility();
  }
  return {order,frame,get enabled(){return enabled;},get blocked(){return enabled&&(hidden||frozen);},get frozen(){return frozen;}};
}
