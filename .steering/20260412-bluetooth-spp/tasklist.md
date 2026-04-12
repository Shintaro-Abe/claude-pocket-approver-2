# タスクリスト — Bluetooth SPP 対応

## 進捗凡例
- `[ ]` 未着手
- `[~]` 作業中
- `[x]` 完了

---

## フェーズ 1: Arduino IDE 手動設定（コード変更前に必須）

- [ ] **T-01** Arduino IDE で Partition Scheme を変更する
  - Tools > Partition Scheme > **Huge APP (3MB no OTA)** を選択
  - ⚠️ この設定なしでは `<BluetoothSerial.h>` をインクルードするとコンパイルエラーになる

---

## フェーズ 2: `boo_device.ino` コード変更

### 2-1. ファイル先頭（ヘッダー・include・定数）

- [ ] **T-02** ファイルヘッダーのバージョン文字列と通信プロトコル説明を更新する
  - `v2.0` → `v2.1`
  - `JSON over USB Serial (115200bps)` → `JSON over Bluetooth Classic SPP`
  - `USB Serial (115200bps) はデバッグ出力専用` を追記

- [ ] **T-03** `#include <BluetoothSerial.h>` を追加する（`<ArduinoJson.h>` の直後）

- [ ] **T-04** BT 用定数を定数セクションに追加する
  ```cpp
  // ---- Bluetooth ----
  #define BT_DEVICE_NAME  "BooDevice"
  #define BT_PIN          "1234"
  ```

### 2-2. グローバル変数・列挙型

- [ ] **T-05** `BluetoothSerial SerialBT;` をグローバル変数の先頭に宣言する
  - 配置: `DeviceState` 列挙型の直前

- [ ] **T-06** `DeviceState` 列挙型に `ST_BT_WAIT` を追加する
  ```cpp
  enum DeviceState {
    ST_BOOT, ST_BT_WAIT,   // ST_BT_WAIT を追加
    ST_IDLE, ST_SLEEP,
    ...
  };
  ```

### 2-3. アスキーアート定数

- [ ] **T-07** `ART_BT_WAIT` 定数を追加する（`ART_TIRED` の直後）
  ```cpp
  const AsciiArt ART_BT_WAIT = {{
    "   .-\"\"-.   ", "  / - ~ - \\  ",
    " |   ...   | ", "  \\ _____ /  ", " waiting...  ",
  }, 5, COL_DIM};
  ```

### 2-4. 新規関数の追加

- [ ] **T-08** `drawBtWaitScreen()` 関数を新設する（`drawStatsScreen()` の直後）
  - `ART_BT_WAIT` を `MASCOT_Y` に描画
  - `"BooDevice"` を `COL_INFO` で y=88 に表示
  - `gFlash` を使って `"waiting BT..."` を 600ms 点滅（y=100、`COL_DIM`）
  - `"pair PIN: 1234"` を `COL_DIM` で y=112 に表示
  - `"connect via"` / `"Bluetooth SPP"` を y=130/140 に表示

- [ ] **T-09** `checkBtConnection()` 関数を新設する（`drawBtWaitScreen()` の直後）
  - 切断検知: `!SerialBT.connected() && gState != ST_BT_WAIT && gState != ST_BOOT`
    → `gState = ST_BT_WAIT; drawBtWaitScreen();`
  - 再接続検知: `SerialBT.connected() && gState == ST_BT_WAIT`
    → `gState = ST_IDLE; gLastIdleMs = millis(); drawIdleScreen();`
  - 各ブランチで `Serial.printf("[debug] ...")` デバッグログを出力

### 2-5. 既存関数の変更

- [ ] **T-10** `sendJson()` を変更する
  - `serializeJson(doc, Serial)` → `serializeJson(doc, SerialBT)`
  - `Serial.println()` → `SerialBT.println()`
  - デバッグログ `Serial.printf("[debug] sent approved=%s\n", ...)` を追加

- [ ] **T-11** `pollSerial()` を `pollBt()` にリネームし内部を変更する
  - 関数名: `pollSerial` → `pollBt`
  - 先頭に `if (gState == ST_BT_WAIT) return;` ガードを追加
  - `Serial.available()` → `SerialBT.available()`
  - `Serial.read()` → `SerialBT.read()`

- [ ] **T-12** `bootAnimation()` のバージョン文字列を変更する
  - `"v2.0  M5StickC+2"` → `"v2.1  M5StickC+2"`

- [ ] **T-13** `drawIdleScreen()` に BT インジケーターを追加する
  - `cls()` の直後（アスキーアート描画の前）に追記
  - `drawText(111, 0, COL_BOO, 1, "BT");`

- [ ] **T-14** `setup()` を変更する
  - `Serial.begin(SERIAL_BAUD)` の前に以下を追加:
    ```cpp
    SerialBT.setPin(BT_PIN);
    SerialBT.begin(BT_DEVICE_NAME);
    ```
  - `gState = ST_IDLE; drawIdleScreen();` → `gState = ST_BT_WAIT; drawBtWaitScreen();`

- [ ] **T-15** `loop()` を変更する
  - `M5.update()` の直後に `checkBtConnection();` を追加
  - `pollSerial()` の呼び出しを `pollBt()` に変更
  - アニメーション switch に `case ST_BT_WAIT: drawBtWaitScreen(); break;` を追加
  - アニメーションブロックの直後に `if (gState == ST_BT_WAIT) return;` ガードを追加

---

## フェーズ 3: ビルド・書き込み

- [ ] **T-16** コンパイルエラーがないことを確認する
  - Arduino IDE で「検証」（Verify）を実行
  - `BluetoothSerial` / `ST_BT_WAIT` / `pollBt` に関するエラーが出ないこと

- [ ] **T-17** M5StickC PLUS2 に書き込む
  - USB 接続してポートを選択
  - Upload Speed: `1500000`
  - 「マイコンボードに書き込む」を実行

---

## フェーズ 4: 実機テスト

### 接続テスト

- [ ] **T-18** 起動直後に `ST_BT_WAIT` 画面（`"waiting BT..."` 点滅）が表示されることを確認する（AC-CONN-04）

- [ ] **T-19** Windows 11 の Bluetooth 設定でデバイス名 `BooDevice`、PIN `1234` でペアリングできることを確認する（AC-CONN-01/03）

- [ ] **T-20** デバイスマネージャーで仮想 COM ポートが割り当てられていることを確認する（AC-CONN-02）

- [ ] **T-21** `boo_bridge.py --port /dev/ttyS<N>` を実行し `[boo] Connected` が表示されることを確認する（AC-CONN-02）

- [ ] **T-22** 接続後にデバイスが `ST_IDLE` 画面（BT インジケーター `BT` 表示）に遷移することを確認する（AC-CONN-05/07）

### 切断・再接続テスト

- [ ] **T-23** `boo_bridge.py` を Ctrl+C で終了し、5 秒以内に `ST_BT_WAIT` 画面へ遷移することを確認する（AC-CONN-06）

- [ ] **T-24** `boo_bridge.py` を再起動し、再ペアリングなしで `ST_IDLE` へ戻ることを確認する（AC-CONN-07）

### 通信テスト

- [ ] **T-25** `approve_request` ツールを呼び出し、承認画面が表示されることを確認する（AC-COMM-01）

- [ ] **T-26** ボタン A で `{"approved":true}`、ボタン B で `{"approved":false}` が返ることを確認する（AC-COMM-02/03）

- [ ] **T-27** Arduino Serial Monitor（115200bps）でデバッグログが表示され、JSON 文字列が混入しないことを確認する（AC-COMM-04/05）

### 点滅・アニメーションテスト

- [ ] **T-28** `ST_BT_WAIT` 画面で `"waiting BT..."` が 600ms 間隔で点滅することを確認する（AC-CONN-08）

### 状態遷移テスト

- [ ] **T-29** `ST_APPROVAL` 中に BT 切断（`boo_bridge.py` 強制終了）→ `ST_BT_WAIT` 画面に遷移し、
  再起動後に `ST_IDLE` へ戻ることを確認する（AC-TAMA-01）

- [ ] **T-30** T-29 の遷移前後で `gBoo.fed` / `gBoo.energy` のゲージ値が保持されていることを目視確認する（AC-TAMA-02）
  - 確認手順: 遷移前のゲージ量を覚えておき、再接続後のアイドル画面で同じ量が表示されること

---

## 完了条件

全タスク（T-01 〜 T-30）が `[x]` になり、
フェーズ 4 の全受け入れ条件（AC-CONN-01〜08、AC-COMM-01〜05、AC-TAMA-01〜02）をパスした状態。
