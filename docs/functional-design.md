# 機能設計書

> PRD（`docs/product-requirements.md`）で決定した**What**を受け、
> 本ドキュメントでは実装者が迷わないための**How**を定義する。

---

## 1. システム全体アーキテクチャ

### 1-1. Dev Container 環境（SSE モード + PreToolUse フック）

```
┌──────────────────────────────────────────────────────────────────┐
│ Dev Container (Docker on WSL2)                                   │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │  Claude Code                                             │   │
│  │                                                          │   │
│  │  ① PreToolUse フック（Bash 実行前に発火）                 │   │
│  │    └─ boo_hook.sh                                        │   │
│  │         └─ POST http://host.docker.internal:8765/request │   │
│  │                                                          │   │
│  │  ② MCP ツール（手動呼び出し）                             │   │
│  │    └─ boo-approval MCP (SSE)                             │   │
│  │         └─ http://host.docker.internal:8765/sse          │   │
│  └──────────────────────────────────────────────────────────┘   │
└────────────────────────────┬─────────────────────────────────────┘
                             │ host.docker.internal:8765
┌────────────────────────────┼─────────────────────────────────────┐
│ Windows 11 (PowerShell)    │                                     │
│                            ▼                                     │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │  boo_bridge.py (SSE モード、ポート 8765)                 │    │
│  │                                                         │    │
│  │  /sse     ← MCP SSE エンドポイント                      │    │
│  │  /request ← REST 承認エンドポイント（フックから呼ばれる） │    │
│  │                                                         │    │
│  │  BooDevice                                              │    │
│  │  ・_approval_lock（同時リクエスト直列化）                 │    │
│  │  ・_send(json)                                           │    │
│  │  ・_reader (thread)                                      │    │
│  │  ・request_approval()                                    │    │
│  └──────────────────────────────┬──────────────────────────┘    │
│                                 │ pyserial (COM4)                │
│                    仮想 COM ポート (COM4)                         │
│                    Bluetooth SPP ドライバ                        │
└─────────────────────────────────┼────────────────────────────────┘
                                  │ Bluetooth Classic SPP
┌─────────────────────────────────┼────────────────────────────────┐
│ M5StickC PLUS2                  │                                │
│                                 │                                │
│  BluetoothSerial SerialBT ("BooDevice")◄───────────────────────  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │ boo_device.ino                                           │   │
│  │  pollBt()            ← JSON 受信                         │   │
│  │  sendJson()          → JSON 送信                         │   │
│  │  checkBtConnection() ← 接続断検知 (polling)              │   │
│  │  State Machine (ST_BT_WAIT / ST_IDLE / ...)              │   │
│  └──────────────────────────────────────────────────────────┘   │
│                                                                  │
│  USB Serial (115200) → デバッグログ出力のみ（受信なし）          │
└──────────────────────────────────────────────────────────────────┘
```

### 1-2. Windows ネイティブ環境（stdio モード）

```
┌──────────────────────────────────────────────────────┐
│ Windows 11                                           │
│                                                      │
│  ┌────────────────┐  MCP(stdio)  ┌────────────────┐  │
│  │  Claude Code   │◄────────────►│  boo_bridge.py │  │
│  │  (Windows)     │              │  (stdio モード) │  │
│  └────────────────┘              └───────┬────────┘  │
│                                          │ pyserial  │
│                              仮想 COM ポート (COM4)   │
└──────────────────────────────────────────┼───────────┘
                                           │ Bluetooth Classic SPP
                                    M5StickC PLUS2
```

---

## 2. シリアル通信の役割分担（重要設計決定）

| ポート | 方向 | 用途 |
|--------|------|------|
| `BluetoothSerial SerialBT` | 双方向 | **JSON プロトコルメッセージ**（本番通信） |
| `Serial`（USB） | デバイス→PC のみ | **デバッグログ**（`Serial.printf("[debug] ...")`） |

**デバイスは USB Serial からの JSON を読まない。**  
`pollSerial()` を `pollBt()` にリネームし、`SerialBT` のみを読む。  
USB Serial は `Serial.printf` によるデバッグ出力専用とする。

---

## 3. BluetoothSerial API マッピング

ESP32 の `BluetoothSerial` は `Serial` とほぼ同一の API を持つ。
変更量を最小限にするためのマッピングを以下に示す。

| 変更前（USB Serial） | 変更後（BT Serial） | 備考 |
|---------------------|--------------------|----- |
| `Serial.available()` | `SerialBT.available()` | 受信バイト数 |
| `Serial.read()` | `SerialBT.read()` | 1 バイト読み込み |
| `Serial.write(data)` | `SerialBT.write(data)` | バイト列送信 |
| `Serial.println()` | `SerialBT.println()` | 改行付き送信 |
| `Serial.begin(115200)` | `SerialBT.begin("BooDevice")` | 初期化（デバイス名を設定） |
| *(なし)* | `SerialBT.setPin("1234", 4)` | PIN 設定（begin の前に呼ぶ）。m5stack v3.3.7 は 2 引数版 |
| *(なし)* | `SerialBT.connected()` | 接続状態確認（`bool`） |

### setup() での初期化順序

```cpp
// 1. USB Serial（デバッグ用）を最初に初期化する
//    M5.begin() / SerialBT.begin() より前に呼ぶことで出力が確実に動作する
Serial.begin(SERIAL_BAUD);

// 2. M5 ハードウェア初期化
auto cfg = M5.config();
M5.begin(cfg);

// 3. BluetoothSerial（本番通信）を初期化する
//    SerialBT.setPin() は begin() より前に呼ぶ必要がある
//    m5stack v3.3.7 では setPin(const char*, uint8_t) の 2 引数版を使用する
SerialBT.setPin(BT_PIN, 4);       // PIN 文字列と文字数を指定
SerialBT.begin(BT_DEVICE_NAME);   // デバイス名でアドバタイズ開始
```

---

## 4. Bluetooth 接続断検知（ポーリング方式）

イベントコールバックではなく、`loop()` 内でポーリングする。  
コールバックはタスクコンテキストが異なりクラッシュしやすいため採用しない。

### checkBtConnection() の設計

```cpp
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

`checkBtConnection()` は `loop()` の先頭で毎フレーム呼び出す。

---

## 5. 新規ステート: ST_BT_WAIT

Bluetooth 未接続時の専用待機状態。起動直後と切断後に遷移する。

### 画面ワイヤフレーム

```
┌─────────────────┐
│                 │
│   .-""-.        │  ← ART_BT_WAIT（COL_DIM）
│  / - ~ - \      │
│ |   ...   |     │
│  \ _____ /      │
│  waiting...     │
│─────────────────│  ← y=82
│  BooDevice      │  ← y=88 COL_INFO
│                 │
│ waiting BT...   │  ← y=100 点滅（gFlash で ON/OFF）COL_DIM
│ pair PIN: 1234  │  ← y=112 COL_DIM
│─────────────────│  ← y=124
│ connect via     │  ← y=130 COL_DIM
│ Bluetooth SPP   │  ← y=140 COL_BOO
└─────────────────┘
```

### drawBtWaitScreen() の設計

```cpp
void drawBtWaitScreen() {
  cls();
  drawArt(ART_BT_WAIT, MASCOT_Y);

  hline(82);
  drawText(16, 88,  COL_INFO, 1, "BooDevice");
  drawText(16, 100, gFlash ? COL_DIM : COL_BG, 1, "waiting BT...");
  drawText(16, 112, COL_DIM, 1, "pair PIN: 1234");
  hline(124);
  drawText(16, 130, COL_DIM, 1, "connect via");
  drawText(16, 140, COL_BOO, 1, "Bluetooth SPP");
}
```

---

## 6. ステート遷移と主要関数の責務

### 状態ごとの loop() 処理フロー

```
loop() {
    M5.update()
    checkBtConnection()        ← 接続断検知（毎フレーム）
    pollBt()                   ← JSON 受信（BT接続中のみ処理）
    tickDecay(now)             ← 毎分減衰
    アニメーション更新 (ANIM_INTERVAL ごと)
        └─ ST_BT_WAIT    → drawBtWaitScreen()
           ST_IDLE        → drawIdleScreen()
           ST_SLEEP       → drawSleepScreen()
           ST_APPROVAL    → drawApprovalScreen() + タイムアウト処理
           ST_WORKING     → drawWorkScreen()
    ボタン処理（各 ST_* ごとの分岐）
}
```

### 関数責務一覧

| 関数名 | 責務 | 変更区分 |
|--------|------|---------|
| `setup()` | 初期化（BT 含む） | 変更 |
| `loop()` | メインループ | 変更 |
| `checkBtConnection()` | BT 接続断検知・状態遷移 | **新規** |
| `pollBt()` | BT から JSON 受信・処理 | 変更（旧 `pollSerial`） |
| `sendJson(bool)` | BT へ JSON 送信 | 変更 |
| `processMessage(char*)` | 受信 JSON を解析し状態遷移 | 変更なし |
| `drawBtWaitScreen()` | BT 待機画面 | **新規** |
| `drawIdleScreen()` | アイドル画面（BT インジケーター追加） | 変更 |
| `tickDecay(uint32_t)` | fed/energy 毎分減衰 | 変更なし |
| `bootAnimation()` | 起動アニメ → ST_BT_WAIT へ | 変更 |

---

## 7. シーケンス図

### 7-1. 正常系: 承認フロー（Bluetooth 接続済み）

```
Claude Code     boo_bridge.py       /dev/ttyS5(BT)    M5StickC PLUS2
    │                 │                    │                  │
    │ call            │                    │                  │
    │ approve_request │                    │                  │
    │────────────────►│                    │                  │
    │                 │ create Future      │                  │
    │                 │ _pending = fut     │                  │
    │                 │ _send(JSON)        │                  │
    │                 │───────────────────►│                  │
    │                 │                   │ BT SPP write      │
    │                 │                   │─────────────────► │
    │                 │                   │              processMessage()
    │                 │                   │              gState = ST_APPROVAL
    │                 │                   │              drawApprovalScreen()
    │                 │                   │                   │
    │                 │                   │     ユーザーがボタンA押下
    │                 │                   │                   │
    │                 │                   │              sendJson(true)
    │                 │                   │◄──────────────────│
    │                 │ _reader reads     │                   │
    │                 │◄──────────────────│                   │
    │                 │ fut.set_result(T) │                   │
    │                 │ return approved=T │                   │
    │◄────────────────│                   │                   │
    │ {approved:true} │                   │                   │
```

### 7-2. 異常系: 承認リクエスト中に Bluetooth 切断

```
Claude Code     boo_bridge.py       /dev/ttyS5(BT)    M5StickC PLUS2
    │                 │                    │                  │
    │ call            │                    │                  │
    │ approve_request │                    │                  │
    │────────────────►│                    │                  │
    │                 │ _send(JSON)        │                  │
    │                 │───────────────────►│                  │
    │                 │                   │ BT SPP write      │
    │                 │                   │─────────────────► │
    │                 │                   │              ST_APPROVAL 表示中
    │                 │                   │                   │
    │                 │          ★ Bluetooth 切断 ★           │
    │                 │                   │ (切断)            │
    │                 │                   │              checkBtConnection()
    │                 │                   │              gState = ST_BT_WAIT
    │                 │                   │              drawBtWaitScreen()
    │                 │                   │                   │
    │                 │ _reader: SerialException              │
    │                 │ _pending still set │                  │
    │                 │           ...                         │
    │                 │    (timeout + 5 秒後)                 │
    │                 │ asyncio.TimeoutError                  │
    │                 │ return approved=False                 │
    │◄────────────────│                   │                   │
    │{approved:false} │                   │                   │
```

### 7-3. PreToolUse フックによる自動承認フロー

```
Claude Code         boo_hook.sh      boo_bridge.py    M5StickC PLUS2
    │                    │                 │                 │
    │ Bash 実行前         │                 │                 │
    │ フック発火          │                 │                 │
    │ stdin: tool payload │                 │                 │
    │───────────────────►│                 │                 │
    │                    │ POST /request   │                 │
    │                    │────────────────►│                 │
    │                    │                 │ _send(JSON)     │
    │                    │                 │────────────────►│
    │                    │                 │            drawApprovalScreen()
    │                    │                 │                 │
    │                    │                 │   ユーザーがボタンA押下
    │                    │                 │◄────────────────│
    │                    │                 │ return T        │
    │                    │ {approved:true} │                 │
    │                    │◄────────────────│                 │
    │ permissionDecision │                 │                 │
    │  : "allow"         │                 │                 │
    │◄───────────────────│                 │                 │
    │ Bash コマンド実行   │                 │                 │
```

**boo_hook.sh 未起動時（フェイルオープン）:**  
curl が接続失敗 → `[boo_hook] Warning: boo server unreachable` を stderr に出力 → `allow` を返す → Bash 実行

**ボタンB（否認）時:**  
`{approved:false}` → `permissionDecision: "deny"` → Bash ブロック

---

### 7-4. 再接続フロー

```
boo_bridge.py               /dev/ttyS5(BT)    M5StickC PLUS2
    │                              │                  │
    │ ★ 切断検知 (SerialException) │                  │
    │ reconnect_loop() 開始        │                  │
    │                              │   ST_BT_WAIT 表示中
    │                              │   SerialBT.connected() = false をポーリング
    │                              │                  │
    │  (3秒待機 → 再接続試行)      │                  │
    │ serial.Serial(port, baud) ───►│                  │
    │                              │          Windows が BT 再接続
    │                              │   SerialBT.connected() = true
    │                              │   gState = ST_IDLE
    │                              │   drawIdleScreen()
    │ 接続成功 → _read_thread 再起動│                  │
    │ "[boo] Reconnected"          │                  │
```

---

## 8. boo_bridge.py コンポーネント設計

### 8-1. 変更点サマリー

| 変更箇所 | 変更内容 |
|----------|---------|
| `auto_detect_port()` | Bluetooth COM ポートを優先検出するキーワード追加 |
| `BooDevice.connect()` | 接続失敗時に `reconnect_loop()` を呼ぶ |
| `BooDevice._reader()` | 切断時に `reconnect_loop()` を呼んでリカバリ |
| `BooDevice.reconnect_loop()` | **新規**: 指数バックオフで再接続を試みる |

### 8-2. Bluetooth COM ポート自動検出

Windows では Bluetooth SPP ポートの Description が以下のいずれかになる：

```
"Standard Serial over Bluetooth link"
"Bluetooth Serial Port"
```

`auto_detect_port()` の検出優先順位：

1. `--port` 引数で明示指定された場合はそのまま使用
2. Description に `"Bluetooth"` を含むポートを優先選択
3. 次点: `usbserial`, `CH340`, `CP210` など従来キーワード

```python
def auto_detect_port() -> Optional[str]:
    BT_KEYWORDS   = ["bluetooth"]          # 大文字小文字無視
    USB_KEYWORDS  = ["usbserial", "usbmodem", "slab_usb",
                     "ch340", "cp210", "ft232"]

    bt_ports, usb_ports = [], []
    for port in serial.tools.list_ports.comports():
        desc = ((port.description or "") + (port.hwid or "")).lower()
        if any(k in desc for k in BT_KEYWORDS):
            bt_ports.append(port)
        elif any(k in desc for k in USB_KEYWORDS):
            usb_ports.append(port)

    for p in (bt_ports or usb_ports):
        print(f"[boo] Auto-detected: {p.device} ({p.description})",
              file=sys.stderr)
        return p.device
    return None
```

### 8-3. 再接続ロジック

```
reconnect_loop():
  delay = 3 秒
  max_delay = 60 秒
  最大試行回数: 無制限（接続するまで続ける）

  while True:
    try:
      serial.Serial(port, baud, timeout=0.1)
      接続成功 → _read_thread 再起動 → break
    except SerialException:
      "[boo] Reconnect failed, retry in {delay}s..."
      sleep(delay)
      delay = min(delay * 1.5, max_delay)   ← 指数バックオフ（上限60秒）
```

### 8-4. BooDevice クラスの状態管理

```
BooDevice
├── port: str
├── baud: int
├── _ser: serial.Serial | None
├── _lock: threading.Lock             # _send の排他制御
├── _approval_lock: asyncio.Lock      # 同時承認リクエストを直列化
├── _pending: asyncio.Future | None   # 応答待ちの承認リクエスト
├── _loop: asyncio.AbstractEventLoop
├── _read_thread: threading.Thread
└── _connected: bool                  # 再接続ループ制御用

主要フロー:
  connect() → _reader thread 起動
  _reader() → 切断検知 → _connected=False → reconnect_loop()
  reconnect_loop() → 接続成功 → _connected=True → _reader thread 再起動
  request_approval() → _pending に Future セット → _send(JSON)
                     → await Future (timeout秒+5)
  _on_line() → fut.set_result(approved) で Future を解決
```

---

## 9. 画面設計

### 9-1. アイドル画面（Bluetooth 接続インジケーター追加）

```
┌─────────────────┐
│ v  v  v         │  ← mood高い時のハートアニメ
│   .-""-.        │
│  / v w v \      │
│ | v~~~~~v |     │
│  \ _____ /      │
│─────────────────│
│ Boo  Lv6   BT  │  ← BT 接続中は "BT"(COL_BOO), 切断中は描画しない
│─────────────────│  ← (ST_BT_WAIT の場合この画面は表示されない)
│ mood  vvvvvv..  │
│ fed   vvvvv...  │
│ enrg  ####..    │
│─────────────────│
│ approved   148  │
│ denied      12  │
│ napped   6h03m  │
│ tokens   328.5K │
│ today      267  │
└─────────────────┘
```

**変更点**: ヘッダー行右端に `"BT"`（COL_BOO、シアン）を追加（x=111, y=0）。  
`SerialBT.connected()` が true のとき（= ST_IDLE 以上の状態）表示する。

### 9-2. BT 待機画面（新規）

PRD セクション 7 / 本ドキュメント セクション 5 参照。

### 9-3. 承認リクエスト画面（変更なし）

```
┌─────────────────┐
│      !          │
│   .-""-.        │
│  / o ! o \      │
│ |   ~~~   |     │
│  \ _____ /      │
│─────────────────│
│ approve? 28s    │
│ Bash            │
│ rm -rf /tmp     │
│ (called Bash)   │
│─────────────────│
│ A: approve      │
│          B: deny│
└─────────────────┘
```

### 9-4. 承認/否認 結果画面（変更なし）

```
┌─────────────────┐   ┌─────────────────┐
│   .-""-.        │   │   .-""-.        │
│  / ^ ^ \        │   │  / x   x \      │
│ |  \~/  |       │   │ |   nnn   |     │
│  \ _____ /      │   │  \ _____ /      │
│    yay!!        │   │   no way        │
│─────────────────│   │─────────────────│
│  APPROVED       │   │   DENIED        │
│ fed  +2 !       │   │ fed  -1 ...     │
│ vvvvvvv.        │   │ vvv.....        │
│ sent...         │   │ sent...         │
└─────────────────┘   └─────────────────┘
```

### 9-5. スリープ画面（変更なし）

```
┌─────────────────┐
│   .-""-.        │
│  / -   - \      │
│ |   ___   |     │
│  \ _____ /      │
│  zzZZ...        │
│─────────────────│
│ recovering...   │
│ enrg ####..     │
│ napped 0h12m    │
│─────────────────│
│ press A to wake │
└─────────────────┘
```

### 9-6. 統計画面（変更なし）

```
┌─────────────────┐
│   .-""-.    2/2 │
│  / o   o \      │
│ |   ~~~   |     │
│  \ _____ /      │
│   '-----'       │
│─────────────────│
│ -- Session Stats│
│ approved :  148 │
│ denied   :   12 │
│ approve% :  92% │
│ napped  : 6h03m │
│ tokens  :  328K │
│ uptime  : 8h15m │
│ today   :   267 │
│─────────────────│
│ A:back  B:reset │
└─────────────────┘
```

---

### 9-7. アスキーアート カラー設計（行ごとの色指定）

#### カラーレジェンド

| 記号 | 定数名 | RGB565 | 表示色 | 用途 |
|------|--------|--------|--------|------|
| `[C]` | `COL_BOO` | `0x07FF` | シアン | ブーの体・輪郭（標準色） |
| `[H]` | `COL_HEART` | `0xF81F` | マゼンタ | ハート・ときめき・高mood |
| `[W]` | `COL_WARN` | `0xFFE0` | 黄 | 警告・空腹・アラート |
| `[G]` | `COL_OK` | `0x07E0` | 緑 | 承認・OK |
| `[R]` | `COL_NG` | `0xF800` | 赤 | 否認・危険 |
| `[O]` | `COL_ORANGE` | `0xFD20` | 橙 | ビジー・処理中 |
| `[D]` | `COL_DIM` | `0x4208` | 暗灰 | スリープ・疲弊・控え目 |
| `[I]` | `COL_INFO` | `0xFFFF` | 白 | 目・強調テキスト |

---

#### ART_IDLE（通常）

```
[C]    .-""-.      ← 輪郭（シアン）
[I]   / o   o \    ← 目を白で強調
[C]  |   ~~~   |   ← 笑顔
[C]   \ _____ /    ← 顎
[C]    '-----'     ← 首
```

#### ART_SLEEP（スリープ）

```
[D]    .-""-.      ← 暗め（眠り中）
[D]   / -   - \    ← 閉じた目（暗）
[D]  |   ___   |   ← 眠り顔（暗）
[D]   \ _____ /
[C]   zzZZ...      ← Zは少し明るめで夢の中感
```

#### ART_ALERT（アラート）

```
[W]       !        ← 黄色！で注意喚起
[C]    .-""-.      ← 体はシアン
[W]   / o ! o \    ← 目と！を黄で強調
[C]  |   ~~~   |
[C]   \ _____ /
```

#### ART_DANGER（危険）

```
[R]  !! .-""-. !!  ← 赤の！！で危険強調
[W]  !(o  o  o)!   ← 見開き目を黄で強調
[R]  |  !!!  |     ← 赤の！！！
[R]  \-------/
[W]  !DANGER!      ← 黄で目立たせる
```

#### ART_WORK（処理中）

```
[C]    .-""-.
[C]   /->   - \    ← 集中顔
[W]  |  . . .  |   ← 処理中の...を黄で強調
[C]   \ _____ /
```

#### ART_BUSY（ビジー）

```
[O]  * .-""-. *    ← 全行オレンジ（忙しさ）
[O]   /o~v~o\
[O]  v|  ~  |v
[O]   \-----/
```

#### ART_APPROVED（承認！）

```
[G]    .-""-.      ← 全体緑（成功）
[I]   / ^ ^ \      ← 嬉しい目を白で強調
[G]  |  \~/  |     ← 満面の笑み
[G]   \ _____ /
[H]     yay!!      ← マゼンタで喜び爆発
```

#### ART_DENIED（否認）

```
[R]    .-""-.      ← 悲しい体（赤）
[W]   / x   x \    ← バツ目を黄で強調
[R]  |   nnn   |   ← 困り眉
[R]   \ _____ /
[D]    no way      ← 暗くしょんぼり
```

#### ART_CRUSH（ときめき）

```
[H]  v       v     ← ハート（マゼンタ）
[C]    .-""-.      ← 体はシアン
[H]   / v   v \    ← ハート目（マゼンタ）
[C]  |   ~~~   |
[C]   \ _____ /
```

#### ART_LOVE（大好き）

```
[C]    .-""-.
[H]   / v   v \    ← ハート目（マゼンタ）
[H]  |  v v v  |   ← ハートあふれる
[C]   \ _____ /
[H]   v  v  v      ← 足元もハート
```

#### ART_EXCITED（ドキドキ）

```
[H]  v  v  v       ← 頭上ハート（マゼンタ）
[C]    .-""-.
[H]   / v w v \    ← ハート目と口
[H]  | v~~~~~v |   ← ハートで溢れる
[C]   \ _____ /
```

#### ART_PLEAD（お願い）

```
[H]      v         ← ハート（マゼンタ）
[C]    .-""-.
[H]   />v   v\     ← ハート目でお願い顔
[W]  |  pleaz  |   ← 黄でお願い文字強調
[C]   \ _____ /
```

#### ART_HUNGRY（空腹）

```
[W]    .-""-.      ← 黄（空腹警告）
[W]   / o _ o \    ← ぐるぐる目
[W]  |  hungry |
[W]   \ _____ /
[R]    feed me     ← 赤で緊急SOS
```

#### ART_TIRED（疲弊）

```
[D]    .-""-.      ← 全行暗灰（疲弊）
[D]   / ~ _ ~ \
[D]  |  tired  |
[D]   \ _____ /
[D]    zzzZZZ
```

---

#### AsciiArt 構造体の拡張（`lineColors[]` 追加）

現行の `color`（全行一色）を維持しつつ、行ごとに上書きできる `lineColors[]` を追加する。  
`lineColors[i] == 0` の場合はデフォルトの `color` を使用する。

```cpp
#define ART_MAX_LINES 7

struct AsciiArt {
    const char* lines[ART_MAX_LINES];
    uint16_t    lineColors[ART_MAX_LINES]; // 0 = color（デフォルト）を使用
    uint8_t     count;
    uint16_t    color;  // デフォルト色（lineColors が 0 の行に適用）
};
```

#### drawArt() の更新

```cpp
void drawArt(const AsciiArt& art, int16_t yTop) {
    M5.Lcd.setTextSize(1);
    for (uint8_t i = 0; i < art.count; i++) {
        uint16_t col = (art.lineColors[i] != 0) ? art.lineColors[i] : art.color;
        M5.Lcd.setTextColor(col, COL_BG);
        uint8_t len = strlen(art.lines[i]);
        int16_t x   = (SCREEN_W - len * CHAR_W) / 2;
        M5.Lcd.setCursor(x, yTop + i * (CHAR_H + 2));
        M5.Lcd.print(art.lines[i]);
    }
}
```

#### 定義例（ART_EXCITED）

```cpp
const AsciiArt ART_EXCITED = {{
  " v  v  v     ",   // [H] マゼンタ
  "   .-\"\"-.   ", // [C] シアン（デフォルト）
  "  / v w v \\  ", // [H] マゼンタ
  " | v~~~~~v | ", // [H] マゼンタ
  "  \\ _____ /  ", // [C] シアン（デフォルト）
}, {
  COL_HEART, 0, COL_HEART, COL_HEART, 0,  // lineColors[5]
}, 5, COL_BOO};   // color = COL_BOO（デフォルト）
```

---

## 10. データモデル

永続ストレージなし。全データはデバイスのメモリ上（電源断でリセット）。

### BooParams 構造体

```cpp
struct BooParams {
  float    fed;          // ご飯ゲージ  [0.0, FED_MAX=8.0]
  float    energy;       // 体力ゲージ  [0.0, ENERGY_MAX=6.0]
  uint32_t approved;     // 累計承認回数
  uint32_t denied;       // 累計否認回数
  uint32_t nappedSec;    // 累計スリープ秒数
  uint32_t tokenTotal;   // 累計トークン数
  uint32_t today;        // 今日の承認数 (notify_tokens で更新)
  uint32_t sessionStart; // セッション開始 millis()
};
```

### ApprovalReq 構造体

```cpp
struct ApprovalReq {
  char     toolName[48];   // ツール名 (最大 47 文字 + NULL)
  char     details[96];    // 操作詳細 (最大 95 文字 + NULL)
  bool     isDanger;       // danger フラグ
  uint16_t timeoutSec;     // タイムアウト秒数 (デフォルト 30)
  int16_t  remaining;      // 残り秒数 (毎フレーム更新)
  uint32_t startMs;        // リクエスト受信時の millis()
};
```

### AsciiArt 構造体（拡張版）

```cpp
// セクション 9-7 参照
struct AsciiArt {
    const char* lines[ART_MAX_LINES];      // アスキーアート行（最大7行）
    uint16_t    lineColors[ART_MAX_LINES]; // 行ごとの色（0 = color を使用）
    uint8_t     count;                     // 有効な行数
    uint16_t    color;                     // デフォルト色
};
```

### グローバル変数

```cpp
DeviceState  gState;         // 現在のステート
BooParams    gBoo;           // 育成パラメータ
ApprovalReq  gReq;           // 承認リクエスト（ST_APPROVAL 中のみ有効）
char         gSerialBuf[256]; // BT 受信バッファ（最大 255 + NULL）
uint16_t     gSerialPos;

uint32_t     gLastFrameMs;
uint32_t     gLastIdleMs;
uint32_t     gLastDecayMs;
uint32_t     gSleepStartMs;
uint32_t     gResultMs;
uint8_t      gAnimIdx;
bool         gFlash;

BluetoothSerial SerialBT;    // BT Serial インスタンス（グローバル）
```

---

## 11. 通信プロトコル詳細

### メッセージ仕様（PC → デバイス）

| type | 必須フィールド | 任意フィールド | デバイス側処理 |
|------|--------------|--------------|----------------|
| `"approve"` | `tool` (string) | `details` (string, default `""`), `danger` (bool, default false), `timeout` (int, default 30) | ST_APPROVAL へ遷移 |
| `"working"` | なし | `tool` (string) | ST_APPROVAL 以外なら ST_WORKING へ |
| `"idle"` | なし | なし | ST_WORKING → ST_IDLE へ |
| `"tokens"` | `total` (int), `today` (int) | なし | energy 消費計算・カウンター更新 |

### メッセージ仕様（デバイス → PC）

| 条件 | 送信 JSON |
|------|-----------|
| ボタンA 押下（承認） | `{"approved":true}\n` |
| ボタンB 押下（否認） | `{"approved":false}\n` |
| タイムアウト | `{"approved":false}\n` |
| BT 切断中（切断時は送信不可） | — （送信しない） |

### バッファサイズ設計

```
デバイス受信バッファ: 256 バイト
最大受信メッセージ例:
  {"type":"approve","tool":"12345678901234567890123456789012345678901234567",
   "details":"1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234",
   "danger":true,"timeout":30}
  ≒ 約 220 バイト → 256 バイトで十分
```

---

## 12. ライブラリ・依存関係

### Arduino（デバイス側）

| ライブラリ | バージョン | 用途 |
|-----------|-----------|------|
| M5StickCPlus2 | 最新 | ディスプレイ・ボタン |
| ArduinoJson | v6.x | JSON 解析・生成 |
| BluetoothSerial | ESP32 Arduino 内蔵 | BT Classic SPP（追加インストール不要） |

> `BluetoothSerial` は ESP32 Arduino コア（`esp32` board package）に同梱。  
> `#include <BluetoothSerial.h>` で使用可能。

### Python（ブリッジ側）

| パッケージ | バージョン | 用途 |
|-----------|-----------|------|
| pyserial | >=3.5 | シリアル通信（BT COM ポート含む） |
| mcp | >=1.0 | MCP サーバーフレームワーク |

```bash
pip install pyserial mcp
```

---

## 13. テスト方針

### 単体テスト（mock モード）

`boo_bridge.py --mock` でデバイスなしにMCPツールの動作を検証する。

```bash
python boo_bridge.py --mock
```

モックデバイスは 2 秒後に `approved=True` を返す。  
MCP ツールの入出力・JSON フォーマットを検証できる。

### 結合テスト（デバイス接続）

| テストケース | 手順 | 期待結果 |
|-------------|------|---------|
| 正常承認 | `approve_request` 呼び出し → ボタンA | `approved=true` が返る |
| 正常否認 | `approve_request` 呼び出し → ボタンB | `approved=false` が返る |
| タイムアウト | `approve_request` 呼び出し → 30 秒放置 | `approved=false` が返る |
| 切断復旧 | 接続中に PC の BT をオフ → オンに戻す | デバイスが ST_BT_WAIT → ST_IDLE に戻る |
| 連続リクエスト | 承認リクエストを 5 連続送信 | 1 件ずつ順番に処理される |
| danger フラグ | `danger=true` で送信 | DANGER 顔が表示される |
