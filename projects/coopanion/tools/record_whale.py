#!/usr/bin/env python3
"""Record the real GSP native backend, inputs, snapshots and events. No device access.

Start the official simulator + coop_backend with COOP_SIM_ASSET_PATH pointing at
web/whale/mosaico-deepseek.bin and COOP_SIM_PORT matching --input-port first.
"""
import argparse
import json
import socket
import sys
import time
from pathlib import Path

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / 'managed_components/espressif__esp-gsp/tools/sim_bridge'))
from rpc_client import connect

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--input-port', type=int, default=19810)
parser.add_argument('--api-port', type=int, default=8371)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
samples, events, inputs = [], [], []
buffer = b''
state = {}
start = time.monotonic()
with socket.create_connection(('127.0.0.1', args.input_port)) as sock, connect(port=args.api_port) as api:
    sock.setblocking(False)
    def send(command):
        sock.sendall((json.dumps(command, ensure_ascii=False) + '\n').encode())
        inputs.append({'at': time.monotonic()-start, 'command': command})

    def capture(seconds, label):
        global buffer, state
        until = time.monotonic()+seconds
        while time.monotonic() < until:
            tick = time.monotonic()
            send({'t': 'sim_state'})
            try:
                buffer += sock.recv(65536)
                while b'\n' in buffer:
                    line, buffer = buffer.split(b'\n', 1)
                    event = json.loads(line)
                    events.append({'at': time.monotonic()-start, 'data': event})
                    if event.get('t') == 'sim_state':
                        state = event
            except BlockingIOError:
                pass
            path = args.output/f'{len(samples):04d}.png'
            result = api.call('screenshot', {'path': str(path.resolve()), 'format': 'png'})
            if 'error' in result:
                raise RuntimeError(result)
            samples.append({'at': time.monotonic()-start, 'path': str(path.resolve()), 'label': label})
            time.sleep(max(0, .1-(time.monotonic()-tick)))

    def ready():
        capture(.2, 'settle')
        deadline = time.monotonic()+15
        while state.get('busy') and time.monotonic() < deadline:
            capture(.2, 'wait for animation completion')
        if state.get('busy'):
            raise RuntimeError('finite animation failed to finish')
        send({'t': 'sim_action', 'action': 'stand'})

    send({'t': 'clock_quality', 'ready': True})
    send({'t': 'sim_battery', 'percent': 100})
    send({'t': 'asset_apply', 'id': 'whale'})
    send({'t': 'transfer_arrive', 'id': 'whale-enter', 'epoch': 200})
    capture(2.0, 'arrive')
    faces = [('worried','委屈'),('furious','炸毛'),('smug','得意'),('pleading','撒娇'),
             ('curious','好奇'),('excited','雀跃'),('crying','哭泣'),('pout','鼓脸'),('sulking','背身生闷气')]
    for face, label in faces:
        ready()
        send({'t':'pet_command','command':{'t':'say','beats':[{'face':face,'text':label+'。'}]}})
        capture(5.1, face)
    for action, duration in [('walk',5.0),('run',2.0),('jump',.9),('sway',1.4),('stumble',1.7),('fall',5.6),('sulk',4.2)]:
        ready();send({'t':'sim_action','action':action});capture(duration,action)
    ready();send({'t':'sim_button','down':True});capture(1.3,'listen')
    send({'t':'sim_button','down':False});capture(1,'thinking')
    ready();send({'t':'pet_command','command':{'t':'say','beats':[{'face':'happy','text':'我已经学会了摇晃、踉跄、摔倒后爬起，还能背过身去生闷气。摸摸头就原谅你啦。长回复现在会自动换行并分页，语音输出暂时关闭。'}]}})
    capture(10.4,'captions')
    for name, imu in [('left',{'ax':1,'az':0}),('top',{'ay':-1,'az':0}),('right',{'ax':-1,'az':0}),('bottom',{'ay':1,'az':0})]:
        send({'t':'sim_imu',**imu});capture(8.0,'gravity-'+name)
    ready();send({'t':'transfer_depart','id':'whale-return','epoch':201});capture(2,'depart')
    send({'t':'transfer_arrive','id':'whale-returned','epoch':202});capture(2,'arrive-again')
    ready();send({'t':'sim_offline'});send({'t':'sim_touch'});capture(2,'offline-touch')
    ready();send({'t':'asset_apply','id':'coo-fallback','builtin':True});capture(2,'coo-fallback')
    send({'t':'asset_apply','id':'whale-restored'});capture(2,'whale-restored')
    result={'samples':samples,'inputs':inputs,'events':events,'frame':api.call('frame_info')}
    (args.output/'capture.json').write_text(json.dumps(result,ensure_ascii=False,indent=2))
    concat=[]
    for i,sample in enumerate(samples):
        duration=samples[i+1]['at']-sample['at'] if i+1<len(samples) else .1
        concat += ["file '"+sample['path']+"'",'duration %.6f'%duration]
    concat.append("file '"+samples[-1]['path']+"'")
    (args.output/'frames.ffconcat').write_text('\n'.join(concat)+'\n')
    print(json.dumps({'frames':len(samples),'seconds':samples[-1]['at'],'events':len(events)}))
