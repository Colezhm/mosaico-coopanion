#!/usr/bin/env python3
"""Host preview of the board screen without ESP-IDF or the GSP runtime.

Builds tests/preview_render.c with the host C compiler, renders the shared C
state machine/renderer for a few scenarios, and overlays status and subtitle
labels with the bundled Noto Sans SC at the positions in ui/main.json.
Label rasterisation is an approximation of GSP; the canvas pixels are exact.

  python tools/preview_screen.py --out build/preview [--atlas PATH ...]
"""
from pathlib import Path
import argparse, json, os, shutil, subprocess, sys, tempfile

ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parents[1]
WHALE = WORKSPACE / 'desktop/coopanion/packages/cortico-world-desktop-pet/web/whale/mosaico-deepseek.bin'
SCENARIOS = ['idle', 'happy', 'subtitle', 'walk', 'jump', 'listen', 'offline', 'reconnected', 'away', 'away-offline', 'edge-left', 'edge-top', 'sit', 'sulk', 'cry', 'fall', 'arrive', 'flustered', 'delighted', 'cheeky']
SOURCES = ['coop_state.c', 'coop_render.c', 'coop_animation.c', 'coop_subtitle.c', 'coop_hud.c']


def build(work):
    exe = work / 'preview_render'
    cc = os.environ.get('CC') or shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
    if not cc:
        raise SystemExit('A host C compiler is required')
    cmd = [cc, '-std=gnu11', '-O2', '-Wall', '-Wextra', '-I', str(ROOT / 'main'),
           str(ROOT / 'tests/preview_render.c'), *[str(ROOT / 'main' / s) for s in SOURCES], '-lm', '-o', str(exe)]
    subprocess.run(cmd, check=True)
    return exe


def labels():
    scene = json.loads((ROOT / 'ui/main.json').read_text(encoding='utf8'))
    by_name = {o.get('name'): o for o in scene['objects'] if o.get('name')}
    return by_name


def compose(frame, hud, out, font_path):
    from PIL import Image, ImageDraw, ImageFont
    import numpy as np
    raw = np.frombuffer(frame.read_bytes(), dtype='<u2').reshape(480, 480)
    rgb = np.dstack([((raw >> 11) & 31) * 255 // 31, ((raw >> 5) & 63) * 255 // 63, (raw & 31) * 255 // 31]).astype('uint8')
    image = Image.fromarray(rgb, 'RGB')
    draw = ImageDraw.Draw(image)
    edge, status, *lines = hud.read_text(encoding='utf8').split('\n')
    objects = labels()
    top = 342 if edge.strip() == '2' else 32
    status_y = 444 if edge.strip() == '2' else 4
    st = objects['status']
    font = ImageFont.truetype(str(font_path), st.get('font_size', 18))
    if status:
        width = draw.textlength(status, font=font)
        draw.text((24 + (432 - width) / 2, status_y + 2), status, font=font, fill=st['fg_color'])
    sub = objects['subtitle']
    font = ImageFont.truetype(str(font_path), sub.get('font_size', 22))
    for i, line in enumerate([l for l in lines if l][:3]):
        draw.text((24, top + 32 * i + 2), line, font=font, fill=sub['fg_color'])
    image.save(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--atlas', type=Path, action='append', help='Default: built-in Coo and the DeepSeek whale')
    parser.add_argument('--scenario', action='append', choices=SCENARIOS)
    args = parser.parse_args()
    atlases = args.atlas or [ROOT / 'main/coo-atlas.bin', WHALE]
    args.out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        exe = build(work)
        written = []
        for atlas in atlases:
            for scenario in args.scenario or SCENARIOS:
                frame, hud = work / 'frame.rgb565', work / 'hud.txt'
                subprocess.run([str(exe), str(atlas), scenario, str(frame), str(hud)], check=True)
                target = args.out / f'{atlas.stem}-{scenario}.png'
                compose(frame, hud, target, ROOT / 'ui/fonts/NotoSansSC.ttf')
                written.append(str(target))
    print(json.dumps({'images': written}, ensure_ascii=False))


if __name__ == '__main__':
    sys.exit(main())
