# AtomClaw デバイスセットアップ

ESP32-S3ボードへのビルド・書き込み手順です。

**ビルドツールは PlatformIO と ESP-IDF (idf.py) のどちらでも使えます。**

> サービス設定（Discord・Cloudflare・APIキー等）は [ATOMCLAW_SETUP.md](ATOMCLAW_SETUP.md) を参照してください。

---

## 必要なハードウェア

| 品目 | 仕様 | 例 |
|------|------|----|
| ESP32-S3ボード | **8MB Flash + 8MB PSRAM** | M5Stack AtomS3R、AtomS3R Cam |
| USB Type-Cケーブル | データ通信対応のもの | — |

> AtomClawは8MB Flash専用です。16MBボードはMimiClaw（`env:mimiclaw`）を使用してください。

---

## 1. リポジトリのクローン

```bash
git clone https://github.com/n0bisuke/atomclaw.git
cd atomclaw
```

---

## 2. シークレットファイルの作成

```bash
cp main/atom_secrets.h.template main/atom_secrets.h
```

`main/atom_secrets.h` を編集して最低限の4項目を設定します:

```c
/* WiFi */
#define ATOM_SECRET_WIFI_SSID       "your-wifi-ssid"
#define ATOM_SECRET_WIFI_PASS       "your-wifi-password"

/* LLM API */
#define ATOM_SECRET_API_KEY         "sk-ant-..."         // Anthropic または OpenAI のキー
#define ATOM_SECRET_MODEL_PROVIDER  "anthropic"          // "anthropic" または "openai"
```

Discord・Cloudflare等のその他の項目は後から設定できます（空文字のままで起動可能）。

> `atom_secrets.h` は `.gitignore` 登録済みです。コミットしないでください。

---

## 3. ビルド・書き込み

### パターンA: PlatformIO（推奨・簡単）

ESP-IDF のインストール不要。初回ビルド時に自動でダウンロードされます。

```bash
# PlatformIO のインストール（未インストールの場合）
pip install platformio

# ビルド（初回は10〜20分かかります）
pio run -e atomclaw

# 書き込み
pio run -e atomclaw -t upload

# シリアルモニター
pio device monitor -e atomclaw
```

ポートを明示する場合:

```bash
pio run -e atomclaw -t upload --upload-port /dev/cu.usbmodem11401
```

---

### パターンB: ESP-IDF / idf.py

ESP-IDF v5.x がインストール・セットアップ済みであればそのまま使えます。

```bash
# ターゲットを ESP32-S3 に設定
idf.py set-target esp32s3

# ビルド（CMakeLists.txt が AtomClaw 用 sdkconfig を自動で適用）
idf.py build

# 書き込み＆モニター
idf.py flash monitor
```

ポートを明示する場合:

```bash
idf.py -p /dev/cu.usbmodem11401 flash monitor
```

> MimiClaw をビルドする場合のみ `-DBUILD_MIMICLAW=1` を明示してください:
> ```bash
> idf.py -DBUILD_MIMICLAW=1 build
> ```

---

## 4. USBポートの確認

```bash
# macOS
ls /dev/cu.usb*

# Linux
ls /dev/ttyACM*
```

> ESP32-S3ボードにはUSBポートが2つある場合があります。
> **「USB」または「UART」と書かれているポート**（シリアル/JTAGポート）を使ってください。
> 電源専用ポートに接続するとフラッシュに失敗します。

---

## 5. 起動確認

シリアルモニターを開くと以下のようなログが出れば成功です:

```
I (1234) atomclaw: ========================================
I (1234) atomclaw:   AtomClaw - ESP32-S3 8MB AI Agent
I (1234) atomclaw:   Discord + Cloudflare Hybrid
I (1234) atomclaw: ========================================
I (1345) atomclaw: CF history: disabled (local-only, last 2 exchanges)
I (5678) atomclaw: WiFi connected: 192.168.1.42
I (5700) atomclaw: AtomClaw ready! Discord interaction endpoint: http://192.168.1.42/interactions
```

表示された **IPアドレスをメモ**しておきます（Discord連携時に使用します）。

---

## 次のステップ

デバイスが起動したら、サービスとの連携設定に進みます。

**[ATOMCLAW_SETUP.md](ATOMCLAW_SETUP.md)** を参照して以下を設定してください:

- **Discord連携** → Interactions Endpoint URLの登録とスラッシュコマンドの登録
- **Cloudflare連携** → 会話履歴のクラウド保存（オプション）
- **SOUL.md / USER.md** → ボットの性格やユーザー情報のカスタマイズ
