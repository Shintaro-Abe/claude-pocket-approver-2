# ユビキタス言語定義（用語集）

> 本ドキュメントは設計書・コード・会話において**同じ言葉が同じ意味を持つ**ことを保証するための用語集。  
> 新しい用語を追加するときは必ずここに定義を加える。

---

## 1. プロダクト・ドメイン用語

### ブー / Boo

M5StickC PLUS2 上で動作するマスコットキャラクター。  
Claude Code が危険な操作を行う前にユーザーへ承認を求め、デジタルペット育成ゲームとして機能する。  
コード上では変数名・構造体名・デバイス名（BT デバイス名 `"BooDevice"`）として使われる。

### 育成思想

ブーが単なる「承認ボタン」ではなく、ユーザーの行動（承認・否認・放置）によって状態が変化するキャラクターである、というプロダクトコンセプト。  
承認 → ご飯、放置 → 空腹、作業 → 疲れ、スリープ → 回復、という感情的フィードバックを提供する。

### fed（ご飯ゲージ）

ブーの「空腹度」を表すパラメータ。値域: `0.0〜8.0`（ハート最大 8 個）。  
承認で増加、否認・タイムアウト・時間経過で減少する。  
コード変数: `gBoo.fed`（`float`）

| 閾値 | 意味 |
|------|------|
| `< 1.0` | 空腹（`ART_HUNGRY` 表示） |
| `1.0〜3.9` | ゲージ黄色（注意） |
| `4.0〜8.0` | 通常 |

### energy（エネルギーゲージ）

ブーの「体力」を表すパラメータ。値域: `0.0〜6.0`（ブロック最大 6 個）。  
Claude が作業するほど消費し、スリープ中に回復する。  
コード変数: `gBoo.energy`（`float`）

| 閾値 | 意味 |
|------|------|
| `< 0.5` | 疲弊（`ART_TIRED` 表示） |
| `0.5〜2.9` | ゲージ黄色（注意） |
| `3.0〜6.0` | 通常 |

### mood（機嫌ゲージ）

fed と energy から算出される**合成パラメータ**。値域: `0〜8`（整数）。  
アイドルアニメーションの表情を決定する。  
計算式: `(fed/8 × 0.6 + energy/6 × 0.4) × 8`  
コード関数: `calcMood()` → `uint8_t`

### 承認 / approve

ユーザーが A ボタンを押して Claude Code の操作要求を許可する行為。  
`fed += 2.0`（ご飯をあげる）。`gBoo.approved` をインクリメント。  
JSON 応答: `{"approved": true}`

### 否認 / deny

ユーザーが B ボタンを押して Claude Code の操作要求を拒否する行為。  
`fed -= 1.0`（ブーがさびしがる）。`gBoo.denied` をインクリメント。  
JSON 応答: `{"approved": false}`

### タイムアウト / timeout

`approve_request` の呼び出しから `timeout` 秒以内にボタンが押されなかった状態。  
`fed -= 2.0`（空腹で泣く）、`gBoo.denied` をインクリメント。  
自動的に `{"approved": false}` を送信する。

### なでる / pet

アイドル中に B ボタンを押すことでブーをなでる操作。  
`fed += 0.5`、`ART_EXCITED` を一時表示する。  
`approve_request` 時の否認ボタンとは**挙動が異なる**（状態依存）。

### danger（危険フラグ）

`approve_request` の引数。`true` のとき、`ART_DANGER`（赤い警告表情）を表示し視覚的な強調を行う。  
`rm`, `curl`, `git push` など不可逆操作に使用する。

---

## 2. システム・技術用語

### MCP（Model Context Protocol）

Claude Code が外部ツールと通信するためのプロトコル。  
`boo_bridge.py` は MCP **サーバー**として動作し、`stdio`（標準入出力）経由で Claude Code と通信する。  
ツール呼び出し（`approve_request` など）は MCP プロトコルで行われる。

### MCP サーバー

Claude Code から呼び出せるツールを提供するプロセス。  
本プロジェクトでは `boo_bridge.py` が MCP サーバーとして機能する。

### Bluetooth SPP（Serial Port Profile）

Bluetooth Classic の通信プロファイルの一つ。  
仮想シリアルポートを提供し、`pyserial` で通常のシリアルポートと同一の API で読み書きできる。  
ESP32 では `BluetoothSerial` ライブラリが SPP サーバーを実装する。

### BluetoothSerial

ESP32 の `BluetoothSerial` ライブラリ。  
`SerialBT.begin("BooDevice")` でアドバタイズを開始し、接続後は `SerialBT.available()` / `SerialBT.read()` / `SerialBT.write()` で双方向通信を行う。  
コード変数: グローバルインスタンス `SerialBT`

### BooDevice

M5StickC PLUS2 の Bluetooth デバイス名。Windows ペアリング時に `BooDevice` として表示される。  
PIN: `1234`

### WSL2（Windows Subsystem for Linux 2）

Windows 上で Linux 環境を動かす仮想化レイヤー。  
`boo_bridge.py` は WSL2 内で動作する。WSL2 は Bluetooth を直接扱えないため、Windows が作成した仮想 COM ポートを `/dev/ttyS<N>` 経由で使用する。

### 仮想 COM ポート（Virtual COM Port）

Windows がペアリング済み Bluetooth SPP デバイスに割り当てる仮想シリアルポート。  
デバイスマネージャーに `Standard Serial over Bluetooth link (COM5)` のように表示される。  
WSL2 では `/dev/ttyS5`（COM5 の場合）としてアクセスする。

### boo_bridge.py

PC（WSL2）側で動作する Python スクリプト。  
2 つの役割を持つ:
1. **MCP サーバー**: Claude Code との通信インターフェースを提供
2. **シリアルブリッジ**: `/dev/ttyS<N>` 経由でブーデバイスと JSON 通信

### boo_device.ino

M5StickC PLUS2 で動作する Arduino スケッチ。  
`BluetoothSerial SerialBT` で JSON を受信し、画面表示・ボタン処理・パラメータ管理を行う。  
USB Serial は**デバッグ出力専用**（JSON の送受信は行わない）。

---

## 3. ステートマシン用語

デバイスの動作状態を表す `enum DeviceState` の各値。

| コード値 | 日本語名 | 意味 |
|----------|----------|------|
| `ST_BOOT` | 起動中 | 電源投入直後。ブートアニメ実行後に `ST_BT_WAIT` へ遷移 |
| `ST_BT_WAIT` | BT 待機中 | Bluetooth 未接続状態。デバイス名・PIN を表示し接続を待つ |
| `ST_IDLE` | アイドル | Bluetooth 接続済み・待機中。mood に応じたアニメを表示 |
| `ST_SLEEP` | スリープ | `ST_IDLE` から一定時間経過後に遷移。energy を回復中 |
| `ST_APPROVAL` | 承認リクエスト中 | `approve_request` を受信。ボタン入力を待つ |
| `ST_WORKING` | 作業中 | `notify_working` を受信。energy を消費中 |
| `ST_APPROVED` | 承認済み | 承認ボタン押下後の結果表示（約 1.4 秒）。`ST_IDLE` へ遷移 |
| `ST_DENIED` | 否認済み | 否認ボタン押下またはタイムアウト後の結果表示（約 1.4 秒）。`ST_IDLE` へ遷移 |
| `ST_STATS` | スタッツ | アイドル中に A ボタンで遷移。統計情報を表示するページ 2 |

---

## 4. アスキーアート定数

`AsciiArt` 型の定数。`ART_` プレフィックスを持つ。

| 定数名 | 日本語名 | 表示条件 |
|--------|----------|---------|
| `ART_IDLE` | 通常（アイドル） | `ST_IDLE` 基本表情 |
| `ART_SLEEP` | スリープ | `ST_SLEEP` |
| `ART_ALERT` | アラート | `ST_APPROVAL`（通常リクエスト） |
| `ART_DANGER` | 危険 | `ST_APPROVAL`（`danger=true`） |
| `ART_WORK` | 処理中 | `ST_APPROVAL` アニメ交互 |
| `ART_BUSY` | ビジー | `ST_WORKING` |
| `ART_APPROVED` | 承認！ | `ST_APPROVED` |
| `ART_DENIED` | 否認 | `ST_DENIED`（fed 通常時） |
| `ART_HUNGRY` | 空腹 | fed < 1.0 時の優先表示 |
| `ART_TIRED` | 疲弊 | energy < 0.5 時の優先表示 |
| `ART_BT_WAIT` | BT 待機 | `ST_BT_WAIT`（Bluetooth 未接続待機画面） |
| `ART_CRUSH` | ときめき | mood 3〜5 のアイドルアニメ |
| `ART_LOVE` | 大好き | mood 6〜8 のアイドルアニメ |
| `ART_EXCITED` | ドキドキ | mood 6〜8 のアイドルアニメ / なでた直後 |
| `ART_PLEAD` | お願い | `ST_APPROVAL` タイムアウト 5 秒前 |
| `ART_BREAK` | 失恋 | 参考実装（現在未使用） |

---

## 5. カラー定数

TFT ディスプレイの RGB565 カラー定数。`COL_` プレフィックスを持つ。

| 定数名 | 色名 | RGB565 値 | 用途 |
|--------|------|-----------|------|
| `COL_BG` | 黒（背景） | `0x0000` | 背景色 |
| `COL_BOO` | シアン | `0x07FF` | ブー本体・通常テキスト |
| `COL_HEART` | マゼンタ | `0xF81F` | ハート・ときめき表情 |
| `COL_WARN` | 黄色 | `0xFFE0` | 警告・アラート表情 |
| `COL_OK` | 緑 | `0x07E0` | 承認・OK 表示 |
| `COL_NG` | 赤 | `0xF800` | 否認・危険表示 |
| `COL_INFO` | 白 | `0xFFFF` | 情報テキスト |
| `COL_DIM` | 灰色 | `0x4208` | 補助テキスト・空ゲージ |
| `COL_ORANGE` | オレンジ | `0xFD20` | ビジー（`ART_BUSY`）表示 |

---

## 6. MCP ツール一覧

Claude Code から呼び出せるツール。

| ツール名 | 引数 | 戻り値 | 説明 |
|----------|------|--------|------|
| `approve_request` | `tool`, `details`, `danger`, `timeout` | `{"approved": bool}` | 承認リクエストを表示し、ボタン入力を待つ |
| `notify_working` | `tool_name` | `{"ok": true}` | 作業中通知。ビジーアニメを表示 |
| `notify_idle` | なし | `{"ok": true}` | アイドル通知。作業終了を伝える |
| `update_tokens` | `total`（累計）, `today`（今日のトークン数） | `{"ok": true}` | トークン使用量を更新する |

---

## 7. 通信プロトコル用語

### JSON over Serial

本プロジェクトの通信方式。1 メッセージ = 1 行の JSON 文字列 + `\n` 改行。  
Bluetooth SPP（`SerialBT`）経由で 115200bps で通信する。

### approve メッセージ（PC → デバイス）

```json
{"type":"approve","tool":"Bash","details":"rm -rf /tmp","danger":false,"timeout":30}
```

### working メッセージ（PC → デバイス）

```json
{"type":"working","tool":"FileWrite"}
```

### idle メッセージ（PC → デバイス）

```json
{"type":"idle"}
```

### tokens メッセージ（PC → デバイス）

```json
{"type":"tokens","total":328500,"today":267}
```

> `total`: セッション累計トークン数（整数）  
> `today`: **今日のトークン使用量**（整数）。「今日の承認数」ではない。

### approved メッセージ（デバイス → PC）

```json
{"approved":true}
{"approved":false}
```

---

## 8. 英日対応表（コード用語）

コード中の英語とドキュメント・会話中の日本語の対応。

| 英語（コード） | 日本語 |
|---------------|--------|
| `approve` / `approved` | 承認 |
| `deny` / `denied` | 否認 |
| `timeout` | タイムアウト（放置） |
| `pet` | なでる |
| `fed` | ご飯ゲージ |
| `energy` | エネルギーゲージ |
| `mood` | 機嫌ゲージ |
| `working` | 作業中 |
| `idle` | アイドル（待機中） |
| `sleep` / `nap` | スリープ（お昼寝） |
| `danger` | 危険操作フラグ |
| `token` | トークン |
| `Boo` / `boo` | ブー |
| `BooDevice` | Bluetooth デバイス名 |
| `stat` / `stats` | 統計情報 |
| `lv` / `level` | レベル（承認回数 / 10） |
| `gauge` | ゲージ |
| `art` | アスキーアート表情 |
| `bridge` | ブリッジ（boo_bridge.py） |
| `bt` / `BT` | Bluetooth |
| `COM port` | COM ポート（仮想シリアルポート） |
| `ttyS` | WSL2 側の COM ポートマッピング |
