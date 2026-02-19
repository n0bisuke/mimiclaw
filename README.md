# AtomClaw

AtomClaw is an ESP32-S3 AI agent firmware focused on:

- 8MB Flash + 8MB PSRAM targets
- Discord interactions
- Optional Cloudflare Worker/KV history sync
- Local SPIFFS memory files

This repository is a fork/evolution of MimiClaw.  
The default build target is AtomClaw.

## Build Targets

- `env:atomclaw` (default): Discord + Cloudflare, 8MB Flash
- `env:mimiclaw` (legacy): Telegram + WebSocket, 16MB Flash

## Quick Start (AtomClaw)

1. Create secrets:

```bash
cp main/atom_secrets.h.template main/atom_secrets.h
```

2. Build:

```bash
pio run -e atomclaw
```

3. Flash:

```bash
pio run -e atomclaw -t upload
```

4. Monitor:

```bash
pio device monitor -e atomclaw
```

Detailed setup:

- Device setup: `docs/DEVICE_SETUP.md`
- Discord/Cloudflare setup: `docs/ATOMCLAW_SETUP.md`
- Naming migration notes: `docs/NAMING_NOTES.md`
- License notes: `docs/LICENSE_NOTES.md`

## Repository Notes

- Internal identifiers still include some `MIMI_*` names in shared modules.
- Variant behavior is controlled by `CONFIG_DEVICE_ATOMCLAW` / `CONFIG_DEVICE_MIMICLAW`.

## License

MIT License. See `LICENSE`.

Fork/license context and attribution notes are documented in `docs/LICENSE_NOTES.md`.
