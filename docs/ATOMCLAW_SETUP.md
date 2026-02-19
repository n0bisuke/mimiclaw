# AtomClaw セットアップガイド

AtomClawはESP32-S3（8MB Flash）上で動作するDiscord連携AIエージェントです。
Discordのスラッシュコマンドで会話し、オプションでCloudflare KVに会話履歴を保存します。

> **デバイスへの書き込み手順**（ビルド・フラッシュ）は [DEVICE_SETUP.md](DEVICE_SETUP.md) を参照してください。
> 命名移行メモ（MimiClaw -> AtomClaw）は [NAMING_NOTES.md](NAMING_NOTES.md) を参照してください。

---

## 目次

1. [必要なもの](#1-必要なもの)
2. [Discordアプリの作成](#2-discordアプリの作成)
3. [Cloudflare Workerのセットアップ（オプション）](#3-cloudflare-workerのセットアップオプション)
4. [atom_secrets.h の設定](#4-atom_secretsh-の設定)
5. [デバイスのビルド・書き込み](#5-デバイスのビルド書き込み)
6. [Discord に Interactions エンドポイントを登録](#6-discord-に-interactions-エンドポイントを登録)
7. [スラッシュコマンドの登録](#7-スラッシュコマンドの登録)
8. [動作確認](#8-動作確認)
9. [シリアルCLIによる設定変更](#9-シリアルcliによる設定変更)
10. [トラブルシューティング](#10-トラブルシューティング)

---

## 1. 必要なもの

### ハードウェア

| 品目 | 仕様 | 例 |
|------|------|----|
| ESP32-S3ボード | **8MB Flash + 8MB PSRAM** | M5Stack AtomS3R、AtomS3R Cam |
| USB Type-Cケーブル | データ通信対応のもの | — |

> **重要：** AtomClawは8MB Flash専用です。16MBボードはMimiClaw（`env:mimiclaw`）を使用してください。

### ソフトウェア

- [PlatformIO](https://platformio.org/install/cli) または VS Code + PlatformIO拡張
- Python 3.8以上（PlatformIO依存）
- Node.js 18以上（Cloudflare Workerデプロイ時のみ）

### アカウント・APIキー

| サービス | 用途 | 無料枠 |
|----------|------|--------|
| [Discord Developer Portal](https://discord.com/developers/applications) | Botアプリ作成 | 無料 |
| [Anthropic Console](https://console.anthropic.com) または [OpenAI Platform](https://platform.openai.com) | LLM API | 有料 |
| [Cloudflare](https://dash.cloudflare.com) | 会話履歴保存（オプション） | Workers 無料枠あり |
| [ngrok](https://ngrok.com) | ESP32をDiscordに公開（開発時） | 無料枠あり |

---

## 2. Discordアプリの作成

### 2-1. アプリの作成

1. [Discord Developer Portal](https://discord.com/developers/applications) を開く
2. **「New Application」** をクリック
3. アプリ名を入力（例: `AtomClaw`）して作成

### 2-2. 認証情報の取得

**「General Information」** タブを開き以下をコピー:

| 項目 | 用途 |
|------|------|
| **APPLICATION ID** | `ATOM_SECRET_DISCORD_APP_ID` に設定 |
| **PUBLIC KEY** | `ATOM_SECRET_DISCORD_PUBLIC_KEY` に設定（64文字の16進数） |

### 2-3. BotをDiscordサーバーに追加

1. **「OAuth2」→「URL Generator」** を開く
2. **Scopes** で `applications.commands` にチェック
3. 生成されたURLを開き、追加先のサーバーを選択

> スラッシュコマンドのみ使用するため `bot` スコープは不要です。

---

## 3. Cloudflare Workerのセットアップ（オプション）

> **スキップ可能:** `ATOM_SECRET_CF_WORKER_URL` を空にすれば、CFなしでローカルモード（直近2往復のみ）で動作します。

### 3-1. wranglerのインストール

```bash
npm install -g wrangler
wrangler login
```

### 3-2. KV Namespaceの作成

```bash
cd cloudflare-worker
wrangler kv namespace create ATOMCLAW_HISTORY
```

出力された `id` をコピーし、`wrangler.toml` の `REPLACE_WITH_YOUR_KV_NAMESPACE_ID` と置き換えます:

```toml
[[kv_namespaces]]
binding = "ATOMCLAW_HISTORY"
id      = "ここにIDを貼り付ける"
```

### 3-3. 認証トークンの設定（オプション）

ESP32からWorkerへのアクセスを保護したい場合:

```bash
wrangler secret put AUTH_TOKEN
# プロンプトに任意のトークン文字列を入力
```

設定したトークンを `ATOM_SECRET_CF_AUTH_TOKEN` に記載します。

### 3-4. デプロイ

```bash
wrangler deploy
```

デプロイ後に表示されるURL（例: `https://atomclaw-history.yourname.workers.dev`）を控えておきます。

### 3-5. 動作確認

```bash
curl https://atomclaw-history.yourname.workers.dev/health
# → {"ok":true,"ts":...}
```

---

## 4. atom_secrets.h の設定

```bash
cp main/atom_secrets.h.template main/atom_secrets.h
```

`main/atom_secrets.h` を編集:

```c
/* WiFi */
#define ATOM_SECRET_WIFI_SSID           "your-wifi-ssid"
#define ATOM_SECRET_WIFI_PASS           "your-wifi-password"

/* LLM API */
#define ATOM_SECRET_API_KEY             "sk-ant-..."   // AnthropicまたはOpenAIのキー
#define ATOM_SECRET_MODEL               "claude-haiku-4-5"
#define ATOM_SECRET_MODEL_PROVIDER      "anthropic"    // "anthropic" または "openai"

/* Discord */
#define ATOM_SECRET_DISCORD_APP_ID      "123456789012345678"   // APPLICATION ID
#define ATOM_SECRET_DISCORD_PUBLIC_KEY  "abcdef...（64文字）"  // PUBLIC KEY

/* Cloudflare（オプション。不要なら空文字のままでOK）*/
#define ATOM_SECRET_CF_WORKER_URL       "https://atomclaw-history.yourname.workers.dev"
#define ATOM_SECRET_CF_AUTH_TOKEN       ""   // 設定した場合はここに

/* Brave Search（オプション）*/
#define ATOM_SECRET_SEARCH_KEY          ""
```

> `atom_secrets.h` は `.gitignore` に登録済みです。絶対にコミットしないでください。

### モデルの選択

| プロバイダー | 推奨モデル | `ATOM_SECRET_MODEL_PROVIDER` |
|---|---|---|
| Anthropic | `claude-haiku-4-5` | `"anthropic"` |
| OpenAI | `gpt-4o-mini` | `"openai"` |

ESP32の8MBメモリ制約から、低コスト・高速モデルが適しています。

---

## 5. デバイスのビルド・書き込み

ビルドおよびフラッシュ手順の詳細は **[DEVICE_SETUP.md](DEVICE_SETUP.md)** を参照してください。

ビルドが完了しシリアルモニターを起動すると、以下のようなログが出れば準備完了です:

```
I (1234) atomclaw: ========================================
I (1234) atomclaw:   AtomClaw - ESP32-S3 8MB AI Agent
I (1234) atomclaw:   Discord + Cloudflare Hybrid
I (1234) atomclaw: ========================================
I (1345) atomclaw: CF history: enabled (cloud history + summary)
  ↑ CF URLが空の場合: "disabled (local-only, last 2 exchanges)"
I (5678) atomclaw: WiFi connected: 192.168.1.42
I (5700) atomclaw: AtomClaw ready! Discord interaction endpoint: http://192.168.1.42/interactions
```

**表示されたIPアドレスをメモしておきます。**

---

## 6. Discord に Interactions エンドポイントを登録

DiscordはHTTPS必須のため、ESP32のローカルIPを外部に公開する必要があります。

### 方法A: ngrok（開発・評価向け）

```bash
# インストール: https://ngrok.com/download
ngrok http 192.168.1.42:80
# → https://xxxx-xxx-xxx-xxx-xxx.ngrok-free.app
```

### 方法B: Cloudflare Tunnel（本番向け・無料）

```bash
# cloudflaredインストール後
cloudflared tunnel --url http://192.168.1.42:80
# → https://random-name.trycloudflare.com
```

### エンドポイントの登録

1. [Discord Developer Portal](https://discord.com/developers/applications) → アプリを選択
2. **「General Information」** → **「Interactions Endpoint URL」** に入力:
   ```
   https://xxxx-xxx-xxx-xxx-xxx.ngrok-free.app/interactions
   ```
3. **「Save Changes」** をクリック

> Discordはエンドポイント登録時にPINGリクエスト（`{"type":1}`）を送信し、
> ESP32が `{"type":1}` を返せれば検証成功です。
> シリアルモニターに `I (xxxx) discord: Discord HTTP server on port 80` が出ていれば準備完了です。

---

## 7. スラッシュコマンドの登録

AtomClawに話しかけるスラッシュコマンドをDiscordに登録します。
以下のcurlコマンドを実行してください（`APP_ID` と `BOT_TOKEN` を置き換え）。

### Bot Tokenの取得

Discord Developer Portal → **「Bot」** タブ → **「Reset Token」** → トークンをコピー

### コマンド登録

```bash
APP_ID="あなたのAPPLICATION_ID"
BOT_TOKEN="あなたのBot Token"

curl -X POST \
  "https://discord.com/api/v10/applications/${APP_ID}/commands" \
  -H "Authorization: Bot ${BOT_TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{
    "name": "chat",
    "description": "AtomClawに話しかける",
    "options": [
      {
        "name": "message",
        "description": "送るメッセージ",
        "type": 3,
        "required": true
      }
    ]
  }'
```

成功すると登録されたコマンド情報がJSONで返ります。
Discordのサーバーで `/chat` が使えるようになるまで数分かかる場合があります。

---

## 8. 動作確認

1. Discordサーバーを開き、AtomClawを追加したサーバーのテキストチャンネルで:
   ```
   /chat message: こんにちは！
   ```
2. 「AtomClawが考え中...」のような応答が表示された後、AIの返答が届けば成功です。

### 起動後のモード確認（シリアルモニター）

| ログ | 意味 |
|------|------|
| `CF history: enabled (cloud history + summary)` | Cloudflare連携モード |
| `CF history: disabled (local-only, last 2 exchanges)` | ローカルのみモード（直近2往復） |

---

## 9. シリアルCLIによる設定変更

シリアルモニター接続中に入力できる設定コマンドです。
設定はNVS Flashに保存され、`atom_secrets.h` の値より優先されます（再コンパイル不要）。

```
atom> help                              # コマンド一覧表示

# WiFi
atom> wifi_set MyNewSSID MyPassword     # WiFiを変更して再接続

# LLM API
atom> set_api_key sk-ant-api03-xxxxx    # APIキーを変更
atom> set_model claude-haiku-4-5        # モデルを変更
atom> set_model_provider anthropic      # プロバイダー切替（anthropic|openai）

# Discord
atom> set_discord_app_id 123456789...   # Application IDを変更
atom> set_discord_pub_key abcdef...     # Public Keyを変更（64文字hex）

# Cloudflare
atom> set_cf_url https://xxx.workers.dev    # Worker URLを設定
atom> set_cf_token mytoken                  # 認証トークンを設定

# その他
atom> config_show                       # 全設定を表示（キーはマスク）
atom> config_reset                      # NVSをクリア、ビルド時デフォルトに戻す
atom> heap_info                         # メモリ使用量確認
atom> restart                           # 再起動
```

---

## 10. トラブルシューティング

### Discord検証に失敗する（Interactions Endpoint URLが保存できない）

- ESP32がWiFiに接続できているか確認（シリアルモニターで `WiFi connected` を確認）
- ngrok/Cloudflare TunnelのURLが正しいか確認（末尾に `/interactions` がついているか）
- シリアルモニターでDiscordからのPINGリクエストが来ているか確認

### 「Sorry, I couldn't process your request.」と返ってくる

- APIキーが正しいか確認: `atom> config_show`
- LLMへのネットワーク疎通確認（プロキシ設定が必要な環境か確認）
- `heap_info` でPSRAMの空き容量が十分かチェック

### フラッシュ書き込みに失敗する

- USBポートが正しいか確認（[DEVICE_SETUP.md](DEVICE_SETUP.md) の「USBポートの確認」を参照）
- ボードをBootモードにする（BOOTボタンを押しながらリセット）
- アップロード速度を下げる: `platformio.ini` で `upload_speed = 460800`

### CF summary fetchがエラーになる（CFモード）

- Worker URLに `/health` エンドポイントでアクセスできるか確認:
  ```bash
  curl https://yourname.workers.dev/health
  ```
- `AUTH_TOKEN` を設定した場合、`ATOM_SECRET_CF_AUTH_TOKEN` が一致しているか確認
- KV Namespaceのバインディングが正しいか確認（wrangler.toml の `id`）

### 8MB Flashを超えてビルドエラーになる

AtomClawのfactoryパーティションは5MBです。バイナリが5MBを超える場合:
- 不要なツール・モジュールをKconfigで無効化
- `CONFIG_COMPILER_OPTIMIZATION_SIZE=y` でコンパイル最適化

---

## 付録: ファイル構成

```
atomclaw/
├── main/
│   ├── atom_secrets.h.template   ← コピーして atom_secrets.h を作成
│   ├── atom_secrets.h            ← gitignore済み（コミット禁止）
│   ├── atom_config.h             ← AtomClaw全定数
│   ├── atom_main.c               ← エントリーポイント・エージェントループ
│   ├── discord/                  ← Discord HTTP server + Ed25519検証
│   ├── cloudflare/               ← Cloudflare KV クライアント
│   ├── memory/                   ← PSRAMリングバッファ（会話履歴）
│   └── agent/                    ← システムプロンプト・コンテキスト構築
├── cloudflare-worker/
│   ├── worker.js                 ← CF Worker（純粋なKVストレージ）
│   └── wrangler.toml             ← Cloudflareデプロイ設定
├── spiffs_data/
│   ├── config/SOUL.md            ← ボットの性格（要編集）
│   ├── config/USER.md            ← あなたの情報（要編集）
│   └── memory/MEMORY.md         ← 長期記憶
├── platformio.ini                ← PlatformIOビルド設定
└── partitions_atomclaw.csv       ← 8MBパーティションテーブル
```

## 付録: モード比較

| 機能 | CFあり（クラウドモード） | CFなし（ローカルモード） |
|------|--------------------------|--------------------------|
| 会話履歴の永続化 | Cloudflare KVに保存（90日間） | なし（再起動でリセット） |
| 参照できる過去履歴 | 全件（最大100ターン） | 直近2往復（4メッセージ） |
| 要約機能 | あり（ESP32側LLMで生成） | なし |
| 初回セットアップ | CFアカウント + wrangler必要 | 不要、即試用可能 |
| 起動時ログ | `CF history: enabled` | `CF history: disabled` |
