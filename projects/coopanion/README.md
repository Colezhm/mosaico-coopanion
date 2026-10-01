# Native Mosaico Coo application

See [the workspace guide](../../docs/coopanion-implementation.md) and [acceptance record](../../docs/coopanion-acceptance.md).

`main/coop_state.c`, `coop_render.c`, `coop_script.c` and `coop_ui.c` are shared by firmware and the native GSP simulator.
`coop_board.c` owns FreeRTOS queues/peripherals; `coop_link.c` owns paired WSS; `coop_audio.c` owns both codecs and TTS;
`coop_assets.c` verifies and caches the bounded atlas. All mutable device state is instance-owned. The board runtime is
firmware-lifetime; fatal setup failures reboot rather than hot-unload tasks. The PC test adapter uses process-lifetime globals.

The retained first 2 MiB layout, `iris_ota_support_start()` and Vibe Mode OTA writer are preserved.
Do not flash the ordinary application BIN at address zero. The reviewed full-image exporter is `tools/export_browser_image.py`.
