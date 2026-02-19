# AtomClaw インストール手順

**ビルドツールは PlatformIO と ESP-IDF (idf.py) のどちらでも使えます。**
お好きな方を選んでください。

---

## 必要なもの

- **ESP32-S3ボード（8MB Flash + 8MB PSRAM）**
  - 例: M5Stack AtomS3R、AtomS3R Cam
- **USB Type-Cケーブル**（データ通信対応）
- **Anthropic または OpenAI の APIキー**

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

その他の項目（Discord、Cloudflare等）は後から設定するので空文字のままで構いません。

> `atom_secrets.h` は `.gitignore` 登録済みです。コミットしないでください。

---

## 3. ビルド・書き込み

### パターンA: PlatformIO（簡単）

ESP-IDF のインストール不要。初回ビルド時に自動でダウンロードされます。

```bash
# PlatformIO のインストール（未インストールの場合）
pip install platformio

# ビルド
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

### パターンB: ESP-IDF / idf.py（既にインストール済みの場合）

ESP-IDF v5.x がインストール・セットアップ済みであればそのまま使えます。

```bash
# ターゲットを ESP32-S3 に設定
idf.py set-target esp32s3

# ビルド（AtomClaw 専用の sdkconfig を指定）
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.defaults.atomclaw" build

# 書き込み＆モニター
idf.py flash monitor
```

ポートを明示する場合:

```bash
idf.py -p /dev/cu.usbmodem11401 flash monitor
```

> **`-D SDKCONFIG_DEFAULTS` の指定は必須です。**
> これを省略すると 8MB Flash / PSA Crypto などの AtomClaw 固有設定が適用されず、
> ビルドエラーまたは実行時に Ed25519 検証が失敗します。

---

## 4. USBポートについて

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
I (xxx) atomclaw:   AtomClaw - ESP32-S3 8MB AI Agent
I (xxx) atomclaw: CF history: disabled (local-only, last 2 exchanges)
I (xxx) atomclaw: WiFi connected: 192.168.1.42
I (xxx) atomclaw: AtomClaw ready! Discord interaction endpoint: http://192.168.1.42/interactions
```

---

## 次のステップ

- **Discord 連携の設定** → Discord Developer PortalでBotを作成し、スラッシュコマンドを登録する
- **Cloudflare 連携の設定** → 会話履歴のクラウド保存を有効にする（オプション）
- **SOUL.md / USER.md の編集** → ボットの性格やユーザー情報を `spiffs_data/config/` 内のファイルで設定する
