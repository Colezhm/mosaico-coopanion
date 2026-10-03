#!/usr/bin/env python3
"""Portable controller + renderer tests with address/undefined sanitizers.

cJSON comes from the managed component after an ESP-IDF build, or from
CJSON_DIR (CI checks out the same upstream release).
"""
from pathlib import Path
import os, subprocess, tempfile, sys
import re

# Every non-ASCII character in a C string literal must exist in the scene's
# font subsets, or the device shows '?' in its place (the caption font is a
# GB2312 subset without, for example, the em dash).
glyphs = set((Path(__file__).resolve().parents[1] / 'ui/charset.txt').read_text(encoding='utf8'))
glyphs |= set((Path(__file__).resolve().parents[1] / 'ui/main.json').read_text(encoding='utf8'))
for source in sorted((Path(__file__).resolve().parents[1] / 'main').glob('*.c')):
    code = re.sub(r'/\*.*?\*/|//[^\n]*', '', source.read_text(encoding='utf8'), flags=re.S)
    for literal in re.findall(r'"((?:[^"\\]|\\.)*)"', code):
        missing = sorted({ch for ch in literal if ord(ch) > 127 and ch not in glyphs})
        if missing:
            raise SystemExit(f'{source.name}: {literal!r} uses glyphs missing from the UI font: {missing}')
print('PASS: caption glyphs present in the UI font subset')

root = Path(__file__).resolve().parents[1]
whale = root.parents[1] / 'desktop/coopanion/packages/cortico-world-desktop-pet/web/whale/mosaico-deepseek.bin'
cjson = Path(os.environ.get('CJSON_DIR') or root / 'managed_components/espressif__cjson/cJSON')
if not (cjson / 'cJSON.h').exists():
    raise SystemExit(f'cJSON not found at {cjson}; build once with ESP-IDF or set CJSON_DIR')
cc = os.environ.get('CC', 'cc')
flags = ['-std=c11', '-g', '-O1', '-fsanitize=address,undefined', '-fno-sanitize-recover=all']
main = lambda *names: [str(root / 'main' / n) for n in names]
SHARED = ('coop_state.c', 'coop_body.c', 'coop_fx.c', 'coop_render.c', 'coop_animation.c', 'coop_subtitle.c', 'coop_hud.c')
SCENARIOS = ['idle', 'happy', 'subtitle', 'walk', 'jump', 'listen', 'offline', 'reconnected', 'away',
             'away-offline', 'edge-left', 'edge-top', 'sit', 'sulk', 'cry', 'fall', 'arrive', 'flustered', 'delighted', 'cheeky',
             'toss', 'land', 'shake', 'dizzy', 'angry', 'tap', 'spin', 'face-down', 'petting', 'look']
atlases = [root / 'main/coo-atlas.bin', *(Path(a) for a in (sys.argv[1:] or [str(whale)]))]

with tempfile.TemporaryDirectory(prefix='coopanion-test-') as tmp:
    tmp = Path(tmp)
    state = tmp / 'state-test'
    subprocess.run([cc, *flags, '-I', str(root / 'main'), '-I', str(cjson), str(root / 'tests/state_test.c'),
                    *main(*SHARED, 'coop_script.c'), str(cjson / 'cJSON.c'), '-lm', '-o', str(state)], check=True)
    subprocess.run([str(state), *map(str, atlases)], check=True)

    receipt = tmp / 'receipt-test'
    subprocess.run([cc, *flags, '-Wall', '-Wextra', '-Werror', '-I', str(root / 'main'),
                    str(root / 'tests/receipt_test.c'), *main('coop_receipt.c'), '-o', str(receipt)], check=True)
    subprocess.run([str(receipt)], check=True)

    # Every preview scenario renders a full frame for every atlas under the sanitizers.
    preview = tmp / 'preview-render'
    subprocess.run([cc, *flags, '-Wall', '-Wextra', '-Werror', '-I', str(root / 'main'),
                    str(root / 'tests/preview_render.c'), *main(*SHARED), '-lm', '-o', str(preview)], check=True)
    for atlas in atlases:
        for scenario in SCENARIOS:
            subprocess.run([str(preview), str(atlas), scenario, str(tmp / 'frame'), str(tmp / 'hud')], check=True)
    print(f'PASS: preview {len(SCENARIOS)} scenarios x {len(atlases)} atlases')

    # A full animated timeline per atlas: every frame stays inside the reported
    # bounds, and dirty-rectangle repaints reproduce the full redraw exactly.
    bench = tmp / 'render-bench'
    subprocess.run([cc, *flags, '-Wall', '-Wextra', '-Werror', '-I', str(root / 'main'),
                    str(root / 'tests/render_bench.c'), *main(*SHARED), '-lm', '-o', str(bench)], check=True)
    for atlas in atlases:
        subprocess.run([str(bench), str(atlas)], check=True)
    print(f'PASS: dirty-rectangle timeline x {len(atlases)} atlases')
