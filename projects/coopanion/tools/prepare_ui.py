#!/usr/bin/env python3
"""Refresh scene inputs (canvas placeholder, caption charset) shared by device and sim."""
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
chars.update('□，。！？：；（）…·')
(root/'charset.txt').write_text(''.join(sorted(chars)),encoding='utf8')
# ui/main.json is the hand-maintained scene source; this tool only refreshes
# the transparent canvas placeholder and the GB2312 caption charset.
