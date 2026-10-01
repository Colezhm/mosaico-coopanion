#!/usr/bin/env python3
"""Export a verified, complete 16 MiB browser-flasher image. Never writes USB.

Unlike IDF's ordinary application flash plan, the normal app belongs at 0x210000.
The reviewed bootloader and Vibe Mode retain their reserved slots. This is an
initial-install image: erased gaps WILL replace existing NOR data when flashed.
"""
from pathlib import Path
import argparse, hashlib, json, struct, zlib

FLASH_SIZE=0x1000000
RETAINED={'otadata':(1,0,0x9000,0x2000),'phy_init':(1,1,0xb000,0x1000),
          'sysmeta':(1,2,0xc000,0x14000),'factory':(0,0,0x20000,0x1c0000),
          'coredump':(1,3,0x1e0000,0x20000)}
def sha(data): return hashlib.sha256(data).hexdigest()
def partitions(data):
    result={}
    for pos in range(0,len(data),32):
        magic=struct.unpack_from('<H',data,pos)[0]
        if magic==0xebeb:
            if hashlib.md5(data[:pos]).digest()!=data[pos+16:pos+32]:raise ValueError('Partition table MD5 mismatch')
            return result
        if magic==0xffff:return result
        if magic!=0x50aa:raise ValueError('Invalid partition table entry')
        _,kind,sub,offset,size,label,flags=struct.unpack_from('<HBBII16sI',data,pos)
        name=label.split(b'\0')[0].decode('ascii')
        if name in result or flags:raise ValueError('Duplicate/encrypted partition is unsupported')
        result[name]=(kind,sub,offset,size)
    raise ValueError('Unterminated partition table')
def export(root,output):
    workspace=root.parents[1]
    reviewed=workspace/'submodule/esp-mosaico-utils/esp-mosaico-recovery/firmware/recovery/prebuilt/recovery'
    source=json.loads((reviewed/'manifest.json').read_text())
    if source['target']!='esp32s31' or source['security']!={'flash_encryption':False,'secure_boot':False}:raise ValueError('Unsupported reviewed base')
    # Verify every byte in the reviewed base, even the two components replaced below.
    for entry in source['images'].values():
        data=(reviewed/entry['file']).read_bytes()
        if len(data)!=entry['size'] or sha(data)!=entry['sha256']:raise ValueError(f"Reviewed component mismatch: {entry['file']}")
    build=root/'build';table=(build/'partition_table/partition-table.bin').read_bytes();layout=partitions(table)
    for name,expected in RETAINED.items():
        if layout.get(name)!=expected:raise ValueError(f'Retained partition changed: {name}')
    if layout.get('ota_0',(0,0,0,0))[:3]!=(0,0x10,0x210000):raise ValueError('Normal app must be ota_0 at 0x210000')
    ordered=sorted((v[2],v[2]+v[3],name) for name,v in layout.items())
    for i,(start,end,name) in enumerate(ordered):
        if end>FLASH_SIZE or start<0x9000 or (i and start<ordered[i-1][1]):raise ValueError(f'Invalid/overlapping partition: {name}')
    image=bytearray(b'\xff'*FLASH_SIZE);components=[]
    def add(name,offset,data,limit,origin):
        if len(data)>limit or offset+len(data)>FLASH_SIZE:raise ValueError(f'Component too large: {name}')
        for c in components:
            if offset<c['offset']+c['size'] and c['offset']<offset+len(data):raise ValueError(f'Overlapping component: {name}')
        image[offset:offset+len(data)]=data
        components.append({'name':name,'offset':offset,'size':len(data),'sha256':sha(data),'source':origin})
    add('bootloader',0x2000,(reviewed/'bootloader.bin').read_bytes(),0x6000,'reviewed Vibe Mode 0.1.4')
    add('partition_table',0x8000,table,0x1000,'coopanion build')
    ota=bytearray(b'\xff'*0x2000);seq=struct.pack('<I',1)
    ota[:4]=seq;struct.pack_into('<I',ota,24,0) # ESP_OTA_IMG_NEW: first boot must be accepted.
    struct.pack_into('<I',ota,28,zlib.crc32(seq,0xffffffff)&0xffffffff)
    add('otadata',0x9000,ota,0x2000,'ota_0, rollback-enabled first boot')
    add('factory',0x20000,(reviewed/'factory.bin').read_bytes(),0x1c0000,'reviewed Vibe Mode 0.1.4')
    voice=root/'managed_components/espressif__esp-sr/esp-tts/esp_tts_chinese/esp_tts_voice_data_xiaole.dat'
    for name,path in [('ota_0',build/'coopanion.bin'),('voice_data',voice),('ui_apps',build/'ui_apps.bin')]:
        if name not in layout:raise ValueError(f'Missing partition: {name}')
        add(name,layout[name][2],path.read_bytes(),layout[name][3],str(path.relative_to(root)))
    if image[0x20000]!=0xe9 or image[0x210000]!=0xe9:raise ValueError('Both Vibe Mode and the application must be ESP images')
    output.parent.mkdir(parents=True,exist_ok=True);output.write_bytes(image)
    manifest={'schema':1,'target':'esp32s31','image':output.name,'bytes':len(image),'sha256':sha(image),
              'flash_offset':0,'first_boot':'normal ota_0; hold AI at boot for Vibe Mode',
              'reviewed_base_manifest_sha256':sha((reviewed/'manifest.json').read_bytes()),
              'components':components,'partitions':{k:{'offset':v[2],'size':v[3]} for k,v in layout.items()},
              'overwrite':{'nor':'Entire 0x000000–0xFFFFFF, including existing settings, pairing, crash logs and cached assets; gaps are 0xFF','nand':'Not touched'},
              'hardware_validation':'NOT PERFORMED by this exporter'}
    output.with_suffix('.manifest.json').write_text(json.dumps(manifest,indent=2,ensure_ascii=False)+'\n')
    assert output.stat().st_size==FLASH_SIZE and sha(output.read_bytes())==manifest['sha256']
    print(json.dumps({'image':str(output),'bytes':len(image),'sha256':manifest['sha256'],'device_written':False},ensure_ascii=False))
    return manifest
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();export(Path(__file__).resolve().parents[1],args.output.resolve())
