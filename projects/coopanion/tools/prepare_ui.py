#!/usr/bin/env python3
"""Rebuild the small native scene; the same scene is compiled for device and sim."""
from pathlib import Path
import json, struct, zlib
root=Path(__file__).resolve().parents[1]/'ui'
root.mkdir(exist_ok=True)
def chunk(kind,payload):
    return struct.pack('>I',len(payload))+kind+payload+struct.pack('>I',zlib.crc32(kind+payload)&0xffffffff)
(root/'canvas.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',480,480,8,2,0,0,0))+chunk(b'IDAT',zlib.compress((b'\0'+b'\0'*1440)*480,9))+chunk(b'IEND',b''))
chars=set(chr(i) for i in range(32,127))
for a in range(0xa1,0xf8):
 for b in range(0xa1,0xff):
  try: chars.add(bytes([a,b]).decode('gb2312'))
  except UnicodeDecodeError: pass
chars.update('□，。！？：；（）…')
(root/'charset.txt').write_text(''.join(sorted(chars)),encoding='utf8')
objects=[
 {'type':'image','parent':-1,'x':0,'y':0,'w':480,'h':480,'image':'canvas.png','codec':'lossless','bind':'canvas','events':[{'event':'click','action':'call','target_name':'poke'},{'event':'long','action':'call','target_name':'menu_open'}]},
 {'type':'label','parent':-1,'x':24,'y':30,'w':432,'h':100,'text':'等待 Coo 来访','font_size':22,'fg_color':'#DDEFEA','text_align':'center','bind':'subtitle','font_charset_file':'charset.txt'},
 {'type':'label','parent':-1,'x':24,'y':140,'w':432,'h':28,'text':'MOSAICO × COO','font_size':18,'fg_color':'#59C9AA','text_align':'center','bind':'status','font_charset':'已连接离线传送中静音倾听识别思考播报 ·0123456789%MOSAICO×'},
 {'type':'container','parent':-1,'x':16,'y':318,'w':448,'h':146,'bg_color':'#14221F','radius':20,'bind':'menu','bind_target':'visible','hidden':True},
]
for i,(name,text) in enumerate([('return_pc','返回电脑'),('mute','静音切换'),('petting','摸摸头'),('menu_close','关闭菜单')]):
 objects.append({'type':'button','parent':3,'x':12+(i%2)*212,'y':12+(i//2)*64,'w':204,'h':56,'text':text,'font_size':22,'bg_color':'#254D40','fg_color':'#E8FFF5','radius':12,'callback':name})
for i in range(3):
 objects.append({'type':'button','parent':-1,'x':24,'y':175+i*57,'w':432,'h':50,'text':'','font_size':22,'bg_color':'#163F31','fg_color':'#E8FFF5','radius':12,'bind':f'answer_{i}','hidden':True,'events':[{'event':'click','action':'call','target_name':'answer','arg':i}],'font_charset_file':'charset.txt'})
for obj in objects:
 if 'bind' in obj: obj['name']=obj['bind']
scene={'screen':'coop','w':480,'h':480,'screen_bg':'#000000','font':'fonts/NotoSansSC.ttf','default_font_size':22,'swipe':False,'objects':objects}
(root/'main.json').write_text(json.dumps(scene,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
