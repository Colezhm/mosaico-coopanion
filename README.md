# Mosaico × Coopanion

[简体中文](README_CN.md) · [Development and setup](docs/coopanion-implementation.md) · [Acceptance status](docs/coopanion-acceptance.md)

A cross-screen Coo companion for an Apple Silicon desktop and ESP-Mosaico.
The desktop keeps Coopanion’s conversation, memory, FunASR and computer-use permission flow.
The device runs native C animation, IMU/touch interaction, local emotion and Chinese Xiaole TTS.
A paired WSS channel coordinates durable single-body transfers with two 500 ms transition intervals.

**Development candidate; hardware acceptance is pending.** See the acceptance record before flashing.
Full-image installation overwrites all 16 MiB of NOR, including existing settings and pairing.
The retained Vibe Mode partition layout is preserved; existing data is not preserved by initial installation.

- Desktop source: `desktop/coopanion` (Coopanion `78a46f83`, original MIT license retained).
- Device and shared simulator: `projects/coopanion` (GSP 1.5.1; pinned ESP-IDF/ESP32-S31).
- Reproducible setup, private pairing, preview and image export: [guide](docs/coopanion-implementation.md).
- Original workspace documentation: [English](README.upstream.md) / [Chinese](README_CN.upstream.md).

No camera, external modules or whale character are included in the Mosaico integration.
Dependencies retain their original licenses. New desktop and application modules carry MIT licensing.
