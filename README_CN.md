# AtomClaw

AtomClaw 是面向 ESP32-S3 的 AI Agent 固件。

主要特性:

- 面向 8MB Flash + 8MB PSRAM
- 支持 Discord Interactions
- 可选 Cloudflare Worker/KV 历史同步
- 使用 SPIFFS 进行本地记忆存储

本仓库基于 MimiClaw 演进而来，  
默认构建目标为 AtomClaw。

## 构建目标

- `env:atomclaw`（默认）: Discord + Cloudflare，8MB Flash
- `env:mimiclaw`（遗留）: Telegram + WebSocket，16MB Flash

## 快速开始（AtomClaw）

1. 创建密钥文件:

```bash
cp main/atom_secrets.h.template main/atom_secrets.h
```

2. 构建:

```bash
pio run -e atomclaw
```

3. 烧录:

```bash
pio run -e atomclaw -t upload
```

4. 串口监控:

```bash
pio device monitor -e atomclaw
```

详细文档:

- 设备刷机: `docs/DEVICE_SETUP.md`
- Discord/Cloudflare 配置: `docs/ATOMCLAW_SETUP.md`
- 命名迁移说明: `docs/NAMING_NOTES.md`
- 许可证说明: `docs/LICENSE_NOTES.md`

## 仓库说明

- 共享模块中仍保留部分 `MIMI_*` 历史命名。
- 实际行为由 `CONFIG_DEVICE_ATOMCLAW` / `CONFIG_DEVICE_MIMICLAW` 控制。

## 许可证

MIT License，见 `LICENSE`。

Fork 归属与注意事项见 `docs/LICENSE_NOTES.md`。
