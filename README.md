# Mosaico × Coopanion

[简体中文](README_CN.md) · [Development and setup](docs/coopanion-implementation.md) · [Acceptance status](docs/coopanion-acceptance.md)

A cross-screen Coo / DeepSeek whale companion for an Apple Silicon desktop and ESP-Mosaico.
The desktop keeps Coopanion’s conversation, memory, FunASR and computer-use permission flow.
The device runs native C animation, IMU/touch interaction and local emotion. TTS output is temporarily disabled; microphone input remains available.
A paired WSS channel coordinates durable single-body transfers with two 500 ms transition intervals.

**Development candidate; hardware acceptance is pending.** See the acceptance record before flashing.
Full-image installation overwrites all 16 MiB of NOR, including existing settings and pairing.
The retained Vibe Mode partition layout is preserved; existing data is not preserved by initial installation.

- Desktop source: `desktop/coopanion` (Coopanion `78a46f83`, original MIT license retained).
- Device and shared simulator: `projects/coopanion` (GSP 1.5.1; pinned ESP-IDF/ESP32-S31).
- Reproducible setup, private pairing, preview and image export: [guide](docs/coopanion-implementation.md).
- Original workspace documentation: [English](README.upstream.md) / [Chinese](README_CN.upstream.md).

Version 1.1.0 adds the original DeepSeek whale rig, 36 device animation clips and expanded expressions; see the [whale guide](docs/coopanion-whale.md).
Version 1.2.0 (desktop 0.1.0-mosaico.4) fixes false offline states, captions and version reporting, adds idle Wi-Fi power saving and refines board art; hardware acceptance is pending.
Version 1.3.0 (desktop 0.1.0-mosaico.5) adds physical feedback — inertia, toss and catch, knocks, shaking, spinning and sleeping face down — procedural eyes and emote effects, and dirty-rectangle rendering about 3–5× faster. Installed on the board; hands-on feel still needs a person.
No camera or external modules are included.
Dependencies retain their original licenses. New desktop and application modules carry MIT licensing.
