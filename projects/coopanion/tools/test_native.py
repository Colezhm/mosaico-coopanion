#!/usr/bin/env python3
"""Portable controller + renderer tests with address/undefined sanitizers."""
from pathlib import Path
import subprocess, tempfile, sys
root=Path(__file__).resolve().parents[1]
whale=root.parents[1]/'desktop/coopanion/packages/cortico-world-desktop-pet/web/whale/mosaico-deepseek.bin'
with tempfile.TemporaryDirectory(prefix='coopanion-test-') as tmp:
    binary=Path(tmp)/'state-test'
    cjson=root/'managed_components/espressif__cjson/cJSON'
    subprocess.run(['cc','-std=c11','-g','-O1','-fsanitize=address,undefined','-I',str(root/'main'),'-I',str(cjson),str(root/'tests/state_test.c'),str(root/'main/coop_state.c'),str(root/'main/coop_render.c'),str(root/'main/coop_animation.c'),str(root/'main/coop_script.c'),str(root/'main/coop_subtitle.c'),str(cjson/'cJSON.c'),'-lm','-o',str(binary)],check=True)
    subprocess.run([str(binary),str(root/'main/coo-atlas.bin'),*(sys.argv[1:] or [str(whale)])],check=True)
