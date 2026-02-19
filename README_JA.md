# AtomClaw

AtomClaw は ESP32-S3 向けの AI エージェントファームウェアです。

主な構成:

- 8MB Flash + 8MB PSRAM 向け
- Discord Interactions 対応
- Cloudflare Worker/KV 連携（任意）
- SPIFFS によるローカルメモリ保存

このリポジトリは MimiClaw をベースに発展したフォークで、  
デフォルトのビルド対象は AtomClaw です。

## ビルドターゲット

- `env:atomclaw`（デフォルト）: Discord + Cloudflare、8MB Flash
- `env:mimiclaw`（レガシー）: Telegram + WebSocket、16MB Flash

## クイックスタート（AtomClaw）

1. シークレット作成:

```bash
cp main/atom_secrets.h.template main/atom_secrets.h
```

2. ビルド:

```bash
pio run -e atomclaw
```

3. 書き込み:

```bash
pio run -e atomclaw -t upload
```

4. モニター:

```bash
pio device monitor -e atomclaw
```

詳細ドキュメント:

- デバイスセットアップ: `docs/DEVICE_SETUP.md`
- Discord/Cloudflare 設定: `docs/ATOMCLAW_SETUP.md`
- 命名移行メモ: `docs/NAMING_NOTES.md`
- ライセンスメモ: `docs/LICENSE_NOTES.md`

## リポジトリ補足

- 共有モジュールには `MIMI_*` 命名が一部残っています。
- 実際の挙動は `CONFIG_DEVICE_ATOMCLAW` / `CONFIG_DEVICE_MIMICLAW` で切り替わります。

## ライセンス

MIT License（`LICENSE` を参照）。

フォーク時の帰属・注意点は `docs/LICENSE_NOTES.md` に記載しています。
