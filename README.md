<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# FoloToy AI Passport firmware

This personal fork keeps the upstream FoloToy AI Passport hardware-test baseline and
adds the current **Agent Monitor Terminal** application for ESP32-C3 (8 MB Flash,
no PSRAM, 240×320 display, three buttons).

The terminal receives bounded, read-only task, alert, quota and settings snapshots from
[Agent Monitor](https://github.com/lxxaisy/agent-monitor) over USB Serial/JTAG or encrypted
BLE. It does not approve, cancel, retry or execute agent work. The application guide
documents the screens, protocol, resource limits, Chinese font and host-rendered tests:

- [Agent Monitor Terminal guide](docs/agent-monitor-terminal.md)
- [Chinese guide](docs/agent-monitor-terminal.zh_CN.md)
- [Full upstream development docs](docs/README.md)

## Build and validate

Use ESP-IDF 5.5.3 and the repository's normal environment setup, then run:

```sh
./tools/validate.sh --static
./tools/validate.sh --firmware
```

The firmware gate produces a merged `build/FoloToy-AI-Passport-full.bin` for address
`0x0` and keeps build output ignored. Host UI rendering can be run without hardware;
physical flashing, pairing and acceptance remain separate device checks.

The repository contains source, tests, documentation and the retained font license.
Local credentials, device QR secrets, generated builds and personal data are excluded
from version control.

## Upstream

This fork is based on [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport),
licensed under MIT. The upstream `main` baseline remains available in the fork history;
the current downstream application is on `feature/agent-monitor-terminal`.
