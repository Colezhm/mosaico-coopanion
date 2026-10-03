#!/usr/bin/env python3
"""Record the physical-reaction layer in the official GSP simulator. No device access.

Drives the real native backend with scripted IMU sequences (lean, toss and
catch, knocks, shaking, spinning, rough handling, petting, face down) for Coo
and then the whale, saving a screenshot about every 100 ms plus the backend's
state, and writes frames.ffconcat for ffmpeg.

Start the official simulator + coop_backend first, with COOP_SIM_PORT matching
--input-port and COOP_SIM_ASSET_PATH pointing at web/whale/mosaico-deepseek.bin.
The simulator feeds held IMU values once per 33 ms UI tick, so short knocks
are coarser than on the device's 100 Hz sensor.
"""
import argparse
import json
import math
import socket
import sys
import time
from pathlib import Path

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / 'managed_components/espressif__esp-gsp/tools/sim_bridge'))
from rpc_client import connect  # noqa: E402

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
REST = {'ax': 0, 'ay': 0, 'az': 1, 'gz': 0}

with socket.create_connection(('127.0.0.1', args.input_port)) as sock, connect(port=args.api_port) as api:
    sock.setblocking(False)

    def send(command):
        sock.sendall((json.dumps(command, ensure_ascii=False) + '\n').encode())
        inputs.append({'at': time.monotonic() - start, 'command': command})

    def imu(**values):
        send({'t': 'sim_imu', **{**REST, **values}})

    def poll():
        global buffer, state
        send({'t': 'sim_state'})
        try:
            buffer += sock.recv(65536)
            while b'\n' in buffer:
                line, buffer = buffer.split(b'\n', 1)
                event = json.loads(line)
                events.append({'at': time.monotonic() - start, 'data': event})
                if event.get('t') == 'sim_state':
                    state = event
        except BlockingIOError:
            pass

    def shot(label):
        path = args.output / f'{len(samples):04d}.png'
        result = api.call('screenshot', {'path': str(path.resolve()), 'format': 'png'})
        if 'error' in result:
            raise RuntimeError(result)
        samples.append({'at': time.monotonic() - start, 'path': str(path.resolve()), 'label': label,
                        'state': dict(state)})

    def capture(seconds, label, during=None):
        until = time.monotonic() + seconds
        while time.monotonic() < until:
            tick = time.monotonic()
            if during:
                during(tick)
            poll()
            shot(label)
            time.sleep(max(0, .1 - (time.monotonic() - tick)))

    def toss(label, impact=2.6):
        imu()
        capture(.4, label)
        imu(az=.05)
        capture(.5, label)
        imu(az=impact)
        time.sleep(.05)
        imu()
        capture(2.2, label)

    def knock(label):
        imu(ax=.9)
        time.sleep(.04)
        imu()
        capture(1.2, label)

    def shake(label, seconds=1.6):
        t0 = time.monotonic()
        capture(seconds, label, lambda now: imu(ax=1.2 * math.sin((now - t0) * 2 * math.pi * 5)))
        imu()
        capture(2.4, label + '-after')

    def scenes(who):
        capture(2.5, who + '-idle')
        imu(ax=.3, az=.95)
        capture(3.0, who + '-lean')
        imu()
        capture(1.5, who + '-upright')
        toss(who + '-toss')
        knock(who + '-knock')
        knock(who + '-double-knock-1')
        shake(who + '-shake')
        imu(gz=320)
        capture(2.2, who + '-spin')
        imu()
        capture(2.0, who + '-spin-after')
        for i in range(4):
            toss(f'{who}-rough-{i}', 3.0)
        capture(3.0, who + '-sulk')
        send({'t': 'sim_touch'})
        capture(2.0, who + '-petting')
        imu(az=-1)
        capture(2.5, who + '-face-down')
        imu()
        capture(2.0, who + '-face-up')

    send({'t': 'clock_quality', 'ready': True})
    send({'t': 'sim_battery', 'percent': 100})
    imu()
    send({'t': 'transfer_arrive', 'id': 'phys-enter', 'epoch': 300})
    capture(2.0, 'arrive')
    scenes('coo')
    send({'t': 'asset_apply', 'id': 'whale'})
    capture(30.0, 'calm-down')  # let the tolerance recover before the second figure
    scenes('whale')

    result = {'samples': samples, 'inputs': inputs, 'events': events, 'frame': api.call('frame_info')}
    (args.output / 'capture.json').write_text(json.dumps(result, ensure_ascii=False, indent=2))
    concat = []
    for i, sample in enumerate(samples):
        duration = samples[i + 1]['at'] - sample['at'] if i + 1 < len(samples) else .1
        concat += ["file '" + sample['path'] + "'", 'duration %.6f' % duration]
    concat.append("file '" + samples[-1]['path'] + "'")
    (args.output / 'frames.ffconcat').write_text('\n'.join(concat) + '\n')
    print(json.dumps({'frames': len(samples), 'seconds': samples[-1]['at'], 'events': len(events)}))
