# 要求内容 — Bluetooth SPP 対応

## 1. 背景・目的

`boo_device.ino` は `v2.0` として USB Serial のみで動作している。
ファイル先頭にも明記されている通り、現在の通信プロトコルは `JSON over USB Serial (115200bps)` である。

```cpp
// ■ 通信プロトコル: JSON over USB Serial (115200bps)
```

`product-requirements.md` で定義された中核要件（Bluetooth Classic SPP によるワイヤレス接続）が未実装のため、本作業で対応する。
`boo_bridge.py` は `pyserial` で任意のシリアルポートを読み書きするため、**変更不要**。

---

## 2. 現状分析（変更前コード）

### 変更が必要な箇所

| 対象 | 現状 | 必要な変更 |
|------|------|-----------|
| `#include` | `<M5StickCPlus2.h>` と `<ArduinoJson.h>` のみ | `<BluetoothSerial.h>` を追加 |
| グローバル変数 | `BluetoothSerial` インスタンスなし | `BluetoothSerial SerialBT;` を追加 |
| `DeviceState` 列挙型 | `ST_BOOT, ST_IDLE, ST_SLEEP, ST_APPROVAL, ST_WORKING, ST_APPROVED, ST_DENIED, ST_STATS` | `ST_BT_WAIT` を追加 |
| `setup()` | `Serial.begin(SERIAL_BAUD)` のみ | `SerialBT.begin("BooDevice")` と PIN 設定を追加、初期ステートを `ST_BT_WAIT` に変更 |
| `sendJson()` | `serializeJson(doc, Serial); Serial.println();` | `SerialBT` に変更 |
| `pollSerial()` | `Serial.available()` / `Serial.read()` | `SerialBT.available()` / `SerialBT.read()` に変更 |
| `loop()` | BT 接続チェックなし | `checkBtConnection()` の呼び出しを追加 |
| 起動シーケンス | 直接 `ST_IDLE` へ遷移 | BT 初期化 → `ST_BT_WAIT` → 接続待ち → `ST_IDLE` へ変更 |

### 変更不要な箇所

- 全描画関数（`drawIdleScreen()` / `drawApprovalScreen()` など）
- たまごっちパラメータ処理（`tickDecay()` / `addFed()` / `addEnergy()`）
- `processMessage()` の中身（JSON の解釈ロジック）
- `gSerialBuf[384]` / `gSerialPos` のバッファ管理ロジック
- ボタン操作ハンドリング

---

## 3. 変更・追加する機能の詳細

### 3-1. Bluetooth Classic SPP 接続の初期化

`setup()` に以下を追加する。

```cpp
// SerialBT 初期化（USB Serial より前に呼ぶ）
SerialBT.setPin("1234");
SerialBT.begin("BooDevice");   // デバイス名: "BooDevice"
Serial.begin(SERIAL_BAUD);     // USB Serial はデバッグ専用
```

`#define` 定数として以下を追加する。

```cpp
#define BT_DEVICE_NAME  "BooDevice"
#define BT_PIN          "1234"
```

### 3-2. `ST_BT_WAIT` ステートの追加

`DeviceState` 列挙型を以下に変更する。

```cpp
enum DeviceState {
  ST_BOOT, ST_BT_WAIT,                          // 追加
  ST_IDLE, ST_SLEEP,
  ST_APPROVAL, ST_WORKING,
  ST_APPROVED, ST_DENIED, ST_STATS
};
```

### 3-3. 状態遷移の追加・変更

| 遷移 | トリガー | 備考 |
|------|---------|------|
| `ST_BOOT` → `ST_BT_WAIT` | `setup()` 完了 | 現在は `ST_IDLE` へ直遷移していたものを変更 |
| `ST_BT_WAIT` → `ST_IDLE` | `SerialBT.connected()` が `true` | `checkBtConnection()` 内で検知 |
| `ST_IDLE` → `ST_BT_WAIT` | `SerialBT.connected()` が `false` | `checkBtConnection()` 内で検知 |
| `ST_WORKING` → `ST_BT_WAIT` | `SerialBT.connected()` が `false` | `checkBtConnection()` 内で検知 |
| `ST_APPROVAL` → `ST_BT_WAIT` | `SerialBT.connected()` が `false` | 承認結果は未送信のまま。`gReq` はリセットしない |
| `ST_SLEEP` → `ST_BT_WAIT` | `SerialBT.connected()` が `false` | スリープ中でも切断を検知する |

**切断時の `gReq` の扱い：**  
`ST_APPROVAL` 中に切断が起きた場合、承認リクエストは応答なしのまま残る。  
PC 側 `boo_bridge.py` は `asyncio.TimeoutError` により `approved=false` を返す（既存実装）。  
`gReq` は `ST_BT_WAIT` 遷移時にリセット不要（`ST_APPROVAL` へは `ST_IDLE` からしか遷移しないため、再接続後に古い `gReq` が表示されることはない）。

### 3-4. `checkBtConnection()` 関数の新設

`loop()` の冒頭で毎フレーム呼び出す。

```cpp
// ============================================================
// Bluetooth 接続状態チェック
// ============================================================
void checkBtConnection() {
  bool connected = SerialBT.connected();

  // 切断を検知 → ST_BT_WAIT へ
  if (!connected && gState != ST_BT_WAIT && gState != ST_BOOT) {
    Serial.println("[debug] BT disconnected");
    gState = ST_BT_WAIT;
    drawBtWaitScreen();
    return;
  }

  // 再接続を検知 → ST_IDLE へ
  if (connected && gState == ST_BT_WAIT) {
    Serial.println("[debug] BT reconnected");
    gState = ST_IDLE;
    gLastIdleMs = millis();
    drawIdleScreen();
  }
}
```

**呼び出し位置：** `loop()` の最初（`M5.update()` の直後、`pollSerial()` の前）。

### 3-5. `pollSerial()` の変更

`Serial` → `SerialBT` に置き換える。

```cpp
void pollSerial() {
  while (SerialBT.available()) {
    char c = (char)SerialBT.read();
    // ... バッファ処理は変更なし
  }
}
```

`ST_BT_WAIT` 中は受信をスキップする（接続していないため `SerialBT.available()` は常に 0 だが、念のため `gState == ST_BT_WAIT` のガードを先頭に入れる）。

```cpp
void pollSerial() {
  if (gState == ST_BT_WAIT) return;   // 接続待ち中はスキップ
  while (SerialBT.available()) { ... }
}
```

### 3-6. `sendJson()` の変更

`Serial` → `SerialBT` に置き換える。

```cpp
void sendJson(bool approved) {
  StaticJsonDocument<64> doc;
  doc["approved"] = approved;
  serializeJson(doc, SerialBT);
  SerialBT.println();
  Serial.printf("[debug] sent approved=%s\n", approved ? "true" : "false");
}
```

### 3-7. `ST_BT_WAIT` 画面の描画

`drawBtWaitScreen()` 関数を新設する。

**画面レイアウト（135×240 px）：**

```
y=  8  [アスキーアート ART_BT_WAIT]
y= 82  [水平線]
y= 90  "BooDevice" (COL_INFO)
y=102  "waiting BT..." (COL_DIM, 点滅アニメ)
y=114  "pair PIN: 1234" (COL_DIM)
y=126  [水平線]
y=134  "connect via" (COL_DIM)
y=144  "Bluetooth SPP" (COL_BOO)
```

`ART_BT_WAIT` として以下のアスキーアートを新設する。

```cpp
const AsciiArt ART_BT_WAIT = {{
  "   .-\"\"-.   ", "  / - ~ - \\  ",
  " |   ...   | ", "  \\ _____ /  ", " waiting...  ",
}, 5, COL_DIM};
```

点滅は `gFlash` フラグを使い、`"waiting BT..."` と `"              "` を切り替える。  
`gFlash` は `loop()` の `ANIM_INTERVAL` (600ms) ごとにすでにトグルされているので流用する。

### 3-8. `setup()` の起動シーケンス変更

```
現在: bootAnimation() → gState = ST_IDLE → drawIdleScreen()
変更後: SerialBT.begin() → bootAnimation() → gState = ST_BT_WAIT → drawBtWaitScreen()
```

起動アニメの末尾テキスト `"v2.0  M5StickC+2"` を `"v2.1  M5StickC+2"` に変更してバージョンを更新する。

### 3-9. BT 接続インジケーター

アイドル画面（`drawIdleScreen()`）の右上に BT インジケーターを追加する。

```
接続中:  x=111, y=0  "BT"  COL_BOO (シアン)
接続なし: 表示しない（ST_BT_WAIT 中は BtWait 画面を表示するため不要）
```

---

## 4. Arduino IDE 設定（変更必要）

| 設定項目 | 現状 | 変更後 |
|---------|------|-------|
| ボード | `M5StickC` | 変更なし |
| Partition Scheme | デフォルト (1.2MB APP) | **`Huge APP (3MB no OTA)`** に変更（BT Classic スタック ~1.2MB のため必須） |
| Upload Speed | `1500000` | 変更なし |

---

## 5. ユーザーストーリー

```
Story-02: ワイヤレス接続
As a 開発者,
I want to USB ケーブルを刺さずに Bluetooth で使いたい
So that デスクのケーブルを減らしてすっきりした環境で作業できる。
```

```
Story-04: 切断からの自動復帰
As a 開発者,
I want to Bluetooth 接続が切れたあと再接続待ちになってほしい
So that 接続切断のたびにデバイスを再起動しなくていい。
```

---

## 6. 受け入れ条件

### 接続・切断

| AC-ID | 条件 | 測定方法 |
|-------|------|---------|
| AC-CONN-01 | Windows 11 でデバイス名 `BooDevice`、PIN `1234` でペアリングできる | Bluetooth 設定画面で確認 |
| AC-CONN-02 | ペアリング後、Windows に仮想 COM ポートが割り当てられる | デバイスマネージャー > ポート (COM と LPT) で確認 |
| AC-CONN-03 | `boo_bridge.py --port /dev/ttyS<N>` で `[boo] Connected` が表示される | ターミナル確認 |
| AC-CONN-04 | 起動直後に `ST_BT_WAIT` 画面（"waiting BT..."）が表示される | 目視確認 |
| AC-CONN-05 | PC から接続すると `ST_BT_WAIT` → `ST_IDLE` へ遷移する | 目視確認 |
| AC-CONN-06 | `boo_bridge.py` を強制終了すると 5 秒以内に `ST_BT_WAIT` 画面へ遷移する | ストップウォッチ計測 |
| AC-CONN-07 | `boo_bridge.py` を再起動すると再ペアリングなしで `ST_IDLE` へ戻る | 目視確認 |
| AC-CONN-08 | `ST_BT_WAIT` 中に "waiting BT..." が点滅する（600ms 間隔） | 目視確認 |

### 通信

| AC-ID | 条件 | 測定方法 |
|-------|------|---------|
| AC-COMM-01 | `approve_request` ツール呼び出しで承認画面が表示される | MCP ツール呼び出し後に目視確認 |
| AC-COMM-02 | ボタンA 押下で `{"approved":true}` が返る | MCP ツール呼び出し結果確認 |
| AC-COMM-03 | ボタンB 押下で `{"approved":false}` が返る | MCP ツール呼び出し結果確認 |
| AC-COMM-04 | USB Serial モニター（115200bps）にデバッグログが出力される | Arduino Serial Monitor で確認 |
| AC-COMM-05 | USB Serial モニターに JSON 文字列が**混入しない** | Arduino Serial Monitor で確認 (`{"approved":...}` が出ないこと) |

### たまごっち・状態遷移

| AC-ID | 条件 | 測定方法 |
|-------|------|---------|
| AC-TAMA-01 | `ST_APPROVAL` 中に Bluetooth 切断 → `ST_BT_WAIT` へ遷移し、再接続後は `ST_IDLE` へ戻る | 強制切断して確認 |
| AC-TAMA-02 | `ST_BT_WAIT` → `ST_IDLE` 遷移時、`gBoo` のパラメータ（fed/energy）が保持されている | 遷移前後でゲージ目視確認 |

---

## 7. 制約事項

- Arduino ライブラリ: `BluetoothSerial` は ESP32 Arduino Core に同梱。追加インストール不要。
- `boo_bridge.py` は変更不要（`pyserial` で任意の COM ポートを読み書きするため）。
- `docs/` の変更不要（`product-requirements.md` に BT 要件が既記載）。
- `gSerialBuf` の 384 バイトはそのまま流用する（Bluetooth SPP の MTU は通常 672 バイトのため問題なし）。
- `Serial.available()` / `Serial.read()` の USB Serial 読み取りは**削除**し、デバッグ出力（`Serial.printf`）のみ残す。
