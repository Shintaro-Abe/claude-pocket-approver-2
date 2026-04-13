# 技術仕様書

> システム設計は `functional-design.md` を参照。  
> 本ドキュメントは「**何を使って、どう構築するか**」の実装可能な仕様を定義する。

---

## 1. テクノロジースタック

### 1-1. デバイス側（M5StickC PLUS2）

| 項目 | 採用技術 | 採用理由 |
|------|---------|---------|
| MCU | ESP32-PICO-V3-02 | M5StickC PLUS2 内蔵。Bluetooth Classic 4.2 対応 |
| 開発環境 | Arduino IDE 2.x | M5Stack 公式サポート。初学者に馴染みやすい |
| Arduino コア | **esp32 by Espressif 3.x系**（最新安定版） | `BluetoothSerial` の安定性は 2.x 系と 3.x 系で差異あり。3.x 系を推奨 |
| ボードパッケージ | M5Stack（M5Stack 公式 Board Manager） | M5StickC PLUS2 のピン・ペリフェラル定義を含む |
| ボード選択 | `M5StickCPlus2` | M5Stack ボードパッケージ内の選択肢 |
| BT ライブラリ | `BluetoothSerial`（ESP32 コア同梱） | 追加インストール不要。SPP サーバーモードで動作 |
| 表示ライブラリ | `M5StickCPlus2`（M5Stack 公式） | TFT・ボタン・電源管理を統合 |
| JSON | `ArduinoJson` **v6.x**（v7.x は API が異なるため非推奨） | `StaticJsonDocument` API が安定。v7 の動的確保は不要 |
| アップロード速度 | `1500000` bps | M5StickC PLUS2 の推奨値 |

> **⚠️ Arduino コアのバージョン注意**  
> `BluetoothSerial` の `connected()` 関数の挙動が esp32 core 2.x と 3.x で異なる場合がある。  
> 特に、3.x 系では `SerialBT.begin()` のシグネチャ変更に注意。  
> **esp32 core 3.x 系の最新安定版**（Arduino IDE のボードマネージャで確認）を使用すること。

---

### 1-2. PC 側（WSL2 + Python）

| 項目 | 採用技術 | バージョン | 採用理由 |
|------|---------|-----------|---------|
| 実行環境 | Python | **3.10 以上** | `match` 文・型ヒント強化。asyncio の安定版 |
| シリアル通信 | `pyserial` | **>=3.5** | COM ポート（/dev/ttyS*）の読み書き。BT COM ポートも同一 API |
| MCP フレームワーク | `mcp` | **>=1.0** | Claude Code との MCP stdio/SSE 連携 |
| 非同期 | `asyncio`（標準ライブラリ） | Python 3.10 以上 | 承認待機の `await` に使用 |
| スレッド | `threading`（標準ライブラリ） | — | シリアル受信をバックグラウンドで監視 |
| HTTP サーバー（SSE モード） | `starlette` + `uvicorn` | 最新安定版 | Dev Container から接続する SSE トランスポート |

```bash
# 依存インストール（stdio モード）
pip install "pyserial>=3.5" "mcp>=1.0"

# 依存インストール（SSE モード：Dev Container 環境）
pip install "pyserial>=3.5" "mcp>=1.0" starlette uvicorn
```

---

## 2. フラッシュ・パーティション設計

### 2-1. なぜパーティション設定が重要か

ESP32 の Bluetooth Classic スタック（BT Classic SPP）はフラッシュ上に約 **1.2 MB** 以上を要求する。  
Arduino IDE のデフォルトパーティションスキームでは APP 領域が不足しスケッチが書き込めないことがある。

### 2-2. 推奨パーティションスキーム

| パーティション名 | APP 領域 | OTA | 推奨度 |
|-----------------|---------|-----|--------|
| `Huge APP (3MB No OTA/1MB SPIFFS)` | **3.0 MB** | なし | ◎ 推奨 |
| `No OTA (Large APP)` | **2.0 MB** | なし | ○ 可 |
| デフォルト（`Default 4MB with spiffs`） | 1.4 MB | あり | ✗ 不足 |

**Arduino IDE での設定箇所：**  
`ツール` → `Partition Scheme` → `Huge APP (3MB No OTA/1MB SPIFFS)` を選択

> **⚠️ 実装時に確認が必要**  
> M5Stack ボードパッケージのバージョンによってパーティション選択肢の名称が異なる場合がある。  
> `Huge APP` が見当たらない場合は `Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)` を試すこと。

---

## 3. メモリ使用量

### 3-1. RAM（SRAM ~520 KB）

| 用途 | 使用量 | 備考 |
|------|--------|------|
| Bluetooth Classic スタック | ~50 KB | ESP32 BT Classic SPP のヒープ使用量 |
| FreeRTOS + システム | ~30 KB | ESP32 Arduino の基本オーバーヘッド |
| TFT ライブラリ（M5StickCPlus2） | ~10 KB | フレームバッファなし構成 |
| gSerialBuf（受信バッファ） | 384 B | JSON 1 メッセージ分 |
| BooParams + ApprovalReq | ~200 B | 構造体 2 つ |
| ArduinoJson `StaticJsonDocument<512>` | 512 B | 受信 JSON 解析用 |
| **合計見積** | **~91 KB** | 残り ~429 KB → 余裕あり |

### 3-2. Flash（4 MB）

| 用途 | 使用量 | 備考 |
|------|--------|------|
| Bootloader + パーティションテーブル | ~64 KB | ESP32 固定 |
| Bluetooth Classic スタック（NVS 含む） | ~1.2 MB | Huge APP なら APP 領域に収まる |
| スケッチ本体（boo_device.ino） | ~350-500 KB | BT + M5Stack ライブラリ込みの推定値（実装後に確認） |
| AsciiArt 構造体（15 アート分） | ~700 B | 文字列定数 + lineColors 配列 |
| **合計見積** | **~1.8 MB** | Huge APP (3MB) で余裕あり |

> **AsciiArt 構造体のメモリ計算（参考）**  
> 拡張後: `7×4`(pointers) + `7×2`(lineColors) + `1`(count) + `2`(color) = 45 bytes/art  
> 15 アート × 45 bytes = 675 bytes（RAM 上のオーバーヘッドは無視できる水準）

---

## 4. Arduino IDE 環境構築手順

### Step 1. Arduino IDE インストール

[Arduino IDE 2.x](https://www.arduino.cc/en/software) をダウンロードしてインストールする。

### Step 2. ESP32 ボードパッケージ追加

1. `ファイル` → `基本設定` → `追加のボードマネージャのURL` に以下を追加：

```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```

2. `ツール` → `ボード` → `ボードマネージャ` で `esp32 by Espressif Systems` を検索してインストール

### Step 3. M5Stack ボードパッケージ追加

1. `ファイル` → `基本設定` → `追加のボードマネージャのURL` に追記（カンマ区切り）：

```
https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/arduino/package_m5stack_index.json
```

2. `ツール` → `ボード` → `ボードマネージャ` で `M5Stack` を検索してインストール

### Step 4. ライブラリインストール

`ツール` → `ライブラリを管理` で以下を検索してインストール：

| ライブラリ名 | バージョン |
|------------|-----------|
| `M5StickCPlus2` | 最新版 |
| `ArduinoJson` | **6.x 系（7.x は不可）** |

### Step 5. ボード設定

| 設定項目 | 値 |
|---------|-----|
| ボード | `M5Stack` > `M5StickCPlus2` |
| Upload Speed | `1500000` |
| Partition Scheme | `Huge APP (3MB No OTA/1MB SPIFFS)` |

### Step 6. 書き込み

1. M5StickC PLUS2 を USB-C ケーブルで PC に接続
2. `ツール` → `シリアルポート` で COM ポートを選択
3. `スケッチ` → `マイコンボードに書き込む`

---

## 5. Python 環境構築手順（WSL2）

```bash
# Python バージョン確認（3.10 以上が必要）
python3 --version

# 仮想環境を作成（推奨）
python3 -m venv .venv
source .venv/bin/activate

# 依存パッケージインストール
pip install "pyserial>=3.5" "mcp>=1.0"

# インストール確認
python3 -c "import serial, mcp; print('OK')"
```

---

## 6. Bluetooth ペアリングと COM ポート確認手順

### Step 1. Windows 11 でペアリング

1. `設定` → `Bluetooth とデバイス` → `デバイスの追加`
2. M5StickC PLUS2 の電源を入れる（`BooDevice` が表示されることを確認）
3. `BooDevice` を選択 → PIN `1234` を入力
4. ペアリング完了

### Step 2. COM ポート番号の確認

1. `デバイスマネージャー`（`Win + X` → `デバイスマネージャー`）を開く、  
   または「Bluetooth とデバイス」→「その他の Bluetooth オプション」→「COM ポート」タブを開く
2. **発信 (Outgoing)** 側の `BooDevice 'ESP32SPP'` の COM 番号を確認する（例: `COM4`）

> **ペアリング後に COM ポートが 2 つ割り当てられる**  
> Windows は Bluetooth SPP デバイスに対して「着信 (Incoming)」と「発信 (Outgoing)」の 2 ポートを割り当てる。  
> **発信 (Outgoing)** 側のポートを使用すること。再ペアリング後にポート番号が入れ替わる場合があるため、  
> 使用前に必ず「発信」側の番号を確認する。

> **WSL2/Docker から BT COM ポートへのアクセス**  
> Docker コンテナ（Dev Container）内からは `/dev/ttyS*` 経由での BT COM ポートアクセスができない。  
> `boo_bridge.py` は **Windows PowerShell 上で直接実行**する必要がある。

---

## 7. ビルド・書き込み・起動の全体フロー

```
1. Arduino IDE でスケッチを開く
        src/boo_device/boo_device.ino
        ↓
2. M5StickC PLUS2 を USB 接続
        ↓
3. ボード設定確認（M5StickCPlus2 / Huge APP / 1500000bps）
        ↓
4. スケッチを書き込む
        → 起動すると "BooDevice" が BT アドバタイズを開始
        → 画面に ST_BT_WAIT（Bluetooth 待機画面）が表示される
        ↓
5. Windows 11 でペアリング（PIN: 1234）
        → 発信 (Outgoing) COM ポートが発行される（例: COM4）
        ↓
6. Windows PowerShell で boo_bridge.py を起動
        # stdio モード（Windows ネイティブ Claude Code 用）
        pip install pyserial mcp   # 初回のみ
        python src\boo_bridge.py --port COM4

        # SSE モード（Dev Container 内 Claude Code 用）
        pip install pyserial mcp starlette uvicorn   # 初回のみ
        python src\boo_bridge.py --port COM4 --transport sse

        → "[boo] Connected to COM4" が出力される
        → デバイスが ST_IDLE 画面へ遷移することを確認
        ↓
7. Claude Code に MCP サーバーとして登録
        → 次セクション参照
        ↓
8. PreToolUse フックを設定（Dev Container のみ）
        → .claude/settings.json に hooks 設定を追記（次セクション参照）
        ↓
9. Claude Code を起動して動作確認
        MCP: /mcp で boo-approval が connected と表示されることを確認
        フック: Bash コマンド実行時に M5StickC PLUS2 に承認画面が表示されることを確認
```

---

## 8. Claude Code への MCP 登録

### 方法 A: stdio トランスポート（Windows ネイティブ Claude Code）

`~/.claude/settings.json` に追記：

```json
{
  "mcpServers": {
    "boo-approval": {
      "command": "python",
      "args": ["C:\\path\\to\\boo_bridge.py", "--port", "COM4"],
      "env": {}
    }
  }
}
```

または `claude mcp add` で登録：

```bash
claude mcp add boo-approval python "C:\path\to\boo_bridge.py" --port COM4
```

### 方法 B: SSE トランスポート（Dev Container 内 Claude Code）

Dev Container 環境では stdio MCP から COM ポートへのアクセスができない。  
代わりに Windows PowerShell で `boo_bridge.py` を SSE モードで起動し、  
Dev Container 内の Claude Code から `host.docker.internal` 経由で接続する。

**Step 1. Windows PowerShell で SSE サーバーを起動**

```powershell
python src\boo_bridge.py --port COM4 --transport sse
# → [boo] MCP server starting (SSE on 0.0.0.0:8765)...
```

**Step 2. Dev Container 内の `~/.claude.json` を編集**

`projects["/workspaces/claude-pocket-approver-2"]["mcpServers"]` に追記：

```json
"mcpServers": {
  "boo-approval": {
    "type": "sse",
    "url": "http://host.docker.internal:8765/sse"
  }
}
```

> **注意：** MCP サーバーは `~/.claude/settings.json` ではなく `~/.claude.json` の  
> `projects.<project-path>.mcpServers` に登録する。`settings.json` は `mcpServers` フィールドを受け付けない。

**Step 3. Claude Code を再起動して MCP 接続を確認**

```
/mcp
# boo-approval が "connected" と表示されることを確認
```

### 動作確認（モック）

デバイスなしで MCP ツールが呼び出せるかを確認する場合は `--mock` を使用：

```json
{
  "mcpServers": {
    "boo-approval": {
      "command": "python",
      "args": ["C:\\path\\to\\boo_bridge.py", "--mock"],
      "env": {}
    }
  }
}
```

SSE モックで確認する場合（デバイスなし）：

```powershell
python src\boo_bridge.py --mock --transport sse
```

---

## 9. PreToolUse フック設定（Dev Container 環境）

Claude Code が Bash ツールを実行する前に自動的に Boo デバイスで承認を求めるフックを設定する。

### 仕組み

```
Claude Code が Bash 実行しようとする
  → boo_hook.sh が呼ばれる（PreToolUse フック）
  → POST http://host.docker.internal:8765/request
  → boo_bridge.py が承認リクエストをデバイスに送信
  → ユーザーがボタンA（承認）またはボタンB（否認）を押す
  → 承認: Bash 実行 / 否認: Bash ブロック
```

### Step 1. `.claude/settings.json` を作成

プロジェクトルートの `.claude/settings.json` に以下を記述：

```json
{
  "hooks": {
    "PreToolUse": [
      {
        "matcher": "Bash",
        "hooks": [
          {
            "type": "command",
            "command": "bash /workspaces/claude-pocket-approver-2/src/boo_hook.sh",
            "timeout": 35,
            "statusMessage": "Boo に承認を要求中..."
          }
        ]
      }
    ]
  }
}
```

> **注意：** `.claude/` は `.gitignore` に含まれるため、`settings.json` はコミット対象外。  
> また Claude Code は `settings.local.json` を権限管理に使用するため、フック設定には `settings.json` を使うこと。

### Step 2. 動作確認

Claude Code を再起動後、Bash コマンドの実行を依頼するとフックが発火する。

### 依存スクリプト: `src/boo_hook.sh`

| 項目 | 値 |
|------|----|
| ファイル | `src/boo_hook.sh` |
| 依存コマンド | `curl`, `jq`（Dev Container に標準インストール済み） |
| 危険パターン検出 | `rm `, `git push --force`, `git reset --hard` など |
| フェイルオープン | boo_bridge.py 未起動時は警告を出して操作を許可 |

---

## 10. パフォーマンス要件と検証方法

| 要件 | 目標値 | 検証方法 |
|------|--------|---------|
| approve_request 呼び出し → 画面表示 | **500 ms 以内** | ストップウォッチ or ログのタイムスタンプ差分 |
| ボタン押下 → JSON 送信 | **200 ms 以内** | デバイス側 `Serial.printf("[ts] %lu", millis())` で計測 |
| BT 切断 → ST_BT_WAIT 遷移 | **5 秒以内** | `checkBtConnection()` は `loop()` の毎フレームで実行 |
| 連続動作（BT 接続維持） | **4 時間以上** | タイマー計測 |
| JSON 受信バッファ容量 | 最大 220 bytes のメッセージを処理 | 最大長メッセージを送信して正常処理を確認 |

---

## 11. 技術的制約と既知の注意点

| 制約 | 内容 | 対応策 |
|------|------|--------|
| ESP32 Bluetooth Classic は BLE との同時使用が制限される | BT Classic と BLE の同時動作は公式非推奨 | BLE は本プロジェクトで使用しないため問題なし |
| WSL2 にネイティブ Bluetooth がない | BT デバイスを WSL2 から直接操作できない | Windows の仮想 COM ポート経由で pyserial から接続 |
| COM ポート番号はペアリングごとに変わりうる | 固定されない | `--port` 引数で明示、または自動検出ロジックを使用 |
| ArduinoJson v7 は API 非互換 | `StaticJsonDocument` が廃止 | 必ず v6.x をインストール |
| `BluetoothSerial` は ESP32 コアの Bluetooth enabled ビルドが必要 | デフォルトは有効だが、一部環境で無効化される場合がある | コンパイルエラーが出た場合は `#include <BluetoothSerial.h>` のあとに `#if !defined(CONFIG_BT_ENABLED)` チェックを追加 |
| M5StickC PLUS2 のバッテリーは ~120mAh | BT + ディスプレイで消費が大きい | スリープ活用・輝度 160 設定で 4 時間を目指す |
