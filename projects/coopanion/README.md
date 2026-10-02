# Native Mosaico Coo / DeepSeek whale application

See [the workspace guide](../../docs/coopanion-implementation.md) and [acceptance record](../../docs/coopanion-acceptance.md).

`main/coop_state.c`, `coop_render.c`, `coop_animation.c`, `coop_script.c`, `coop_subtitle.c` and `coop_ui.c` are shared by firmware and the native GSP simulator.
`coop_board.c` owns FreeRTOS queues/peripherals; `coop_link.c` owns paired WSS; `coop_audio.c` owns both codecs and TTS;
`coop_assets.c` verifies and caches the bounded atlas. All mutable device state is instance-owned. The board runtime is
firmware-lifetime; fatal setup failures reboot rather than hot-unload tasks. The PC test adapter uses process-lifetime globals.

The retained first 2 MiB layout, `iris_ota_support_start()` and Vibe Mode OTA writer are preserved.
Do not flash the ordinary application BIN at address zero. The reviewed full-image exporter is `tools/export_browser_image.py`.

Version 1.0.2 gates IMU reactions by duration, uses tilt-triggered falls and four gravity edges,
locks finite animations against replacement, and pages captions in three explicit rows.
TTS output is suspended while microphone capture remains available. Changed UI resources
require `iris system-update`, not a code-only `app-update`.

Version 1.1.0 adds the bounded COO2 animation decoder and the original DeepSeek whale
character: 36 clips, 121 samples, palette/LZ4 delta compression and alpha-aware bilinear
rendering. The existing 1 MiB resource partition is unchanged. Firmware retains default
Coo; a selected whale is sent by the paired desktop and cached before transfer is enabled.
See [the whale guide](../../docs/coopanion-whale.md) for format, export and validation details.

Version 1.1.1 retries render fences that GSP 1.5.1 rejects while a Canvas callback
is active. A single 1500 ms budget bounds admission and completion; failures never
become successful receipts. Durable ownership checks remain mandatory. Critical
network queue admission also retries briefly. Receipt and GSP performance logs
support device diagnosis. This is a code-only update with identical resource and
partition hashes; use the official `iris app-update` after device-write confirmation.
