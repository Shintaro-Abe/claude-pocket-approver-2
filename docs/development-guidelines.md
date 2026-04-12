# 開発ガイドライン

> 本ドキュメントは `boo_device.ino`（Arduino/C++）と `boo_bridge.py`（Python）の  
> コーディング規約・命名規則・Git 規約を定義する。

---

## 1. Arduino / C++ コーディング規約（`boo_device.ino`）

### 1-1. 命名規則

| 種別 | 規則 | 例 |
|------|------|-----|
| グローバル変数 | `g` プレフィックス + `camelCase` | `gState`, `gBoo`, `gAnimIdx` |
| ローカル変数 | `camelCase` | `elapsed`, `filled`, `col` |
| 関数名 | `camelCase` | `drawIdleScreen()`, `tickDecay()`, `addFed()` |
| 構造体名 | `PascalCase` | `BooParams`, `ApprovalReq`, `AsciiArt` |
| 列挙型名 | `PascalCase` | `DeviceState` |
| 列挙型の値 | `ST_` プレフィックス + `SCREAMING_SNAKE_CASE` | `ST_IDLE`, `ST_SLEEP`, `ST_BT_WAIT` |
| `#define` 定数 | `SCREAMING_SNAKE_CASE` | `FED_MAX`, `ANIM_INTERVAL`, `COL_BG` |
| アスキーアート定数 | `ART_` プレフィックス | `ART_IDLE`, `ART_APPROVED`, `ART_HUNGRY` |
| カラー定数 | `COL_` プレフィックス | `COL_BOO`, `COL_HEART`, `COL_NG` |

### 1-2. `#define` グループの整理

定数は意味ごとに `#define` グループを分ける。グループ間に空行を入れてコメントで見出しを付ける。

```cpp
// ---- ゲージ上限 ----
#define FED_MAX     8.0f
#define ENERGY_MAX  6.0f

// ---- fed 変化量 ----
#define FED_APPROVE    2.0f
#define FED_DENY      -1.0f

// TFT カラー (RGB565)
#define COL_BG      0x0000
#define COL_BOO     0x07FF
```

### 1-3. 関数のサイズとコメント

- 1 関数は **1 つの責務** に限定する
- 描画関数（`draw*`）は画面の内容をすべて自分で描画し、副作用（`gState` の変更など）を持たない
- 80 行を超える関数は分割を検討する
- セクション区切りには `// ====` ブロックコメントを使う

```cpp
// ============================================================
// 毎分の減衰処理
// ============================================================
void tickDecay(uint32_t now) {
  // ...
}
```

### 1-4. グローバル変数の扱い

- グローバル変数は **ファイル上部にまとめて宣言**する
- `loop()` 内でのみ状態を変更する（割り込みハンドラやコールバックから `gState` を変更しない）
- `const` にできるものは `const` にする（アスキーアート定数など）

```cpp
// 良い例
const AsciiArt ART_IDLE = {{ ... }, 5, COL_BOO};

// 避ける例（グローバルが散在する）
// ... 関数の間に突然 int gCounter = 0; が現れる
```

### 1-5. ArduinoJson の使い方

- **必ず v6.x** を使用する（v7.x は `StaticJsonDocument` が廃止されており互換なし）
- 受信用は `StaticJsonDocument<512>`、送信用は `StaticJsonDocument<64>` を使う
- `doc["key"] | defaultValue` でデフォルト値付き取得を行う

```cpp
// 良い例
StaticJsonDocument<512> doc;
if (deserializeJson(doc, msg)) return;   // parse 失敗は即 return
const char* type = doc["type"] | "";     // デフォルト値付き

// 避ける例
const char* type = doc["type"];          // null チェックなしは危険
```

### 1-6. 文字列の扱い

- 固定長バッファへのコピーは `strlcpy()` を使う（`strcpy()` は禁止）
- 画面表示の切り詰めは `"%.21s"` のような `printf` 書式で行う

```cpp
strlcpy(gReq.toolName, doc["tool"] | "unknown", sizeof(gReq.toolName));
drawText(4, 98, COL_WARN, 1, "%.21s", gReq.toolName);
```

### 1-7. タイミング処理

- `delay()` は起動アニメのみで使用可。`loop()` 内の通常処理では使用禁止
- タイミングには `millis()` を使い、前回時刻との差分で判定する

```cpp
// 良い例
if (now - gLastFrameMs >= ANIM_INTERVAL) { ... }

// 避ける例（loop() 全体がブロックされる）
delay(600);
```

### 1-8. Bluetooth 通信の注意点

- JSON の読み書きは必ず `SerialBT`（`BluetoothSerial`）を使う
- USB `Serial` はデバッグ出力（`Serial.printf("[debug] ...")`）専用とし、**JSON を読み書きしない**
- `SerialBT.connected()` のポーリングは `checkBtConnection()` に集約する

```cpp
// 良い例（デバッグ出力は Serial、JSON は SerialBT）
Serial.printf("[debug] state=%d\n", gState);   // デバッグ
SerialBT.println(jsonStr);                      // JSON 送信（\n 付き）

// 避ける例
Serial.println(jsonStr);                        // JSON を USB に送ってしまう
```

---

## 2. Python コーディング規約（`boo_bridge.py`）

### 2-1. 命名規則

| 種別 | 規則 | 例 |
|------|------|-----|
| クラス名 | `PascalCase` | `BooDevice`, `MockDevice` |
| 関数・メソッド名 | `snake_case` | `connect()`, `notify_working()`, `auto_detect_port()` |
| 変数名 | `snake_case` | `read_stream`, `approved`, `tool_name` |
| プライベートメンバー | `_` プレフィックス + `snake_case` | `_ser`, `_lock`, `_pending`, `_reader()` |
| 定数（モジュールレベル） | `SCREAMING_SNAKE_CASE` | `DEFAULT_BAUD = 115200` |
| 型ヒント | 積極的に使用 | `def connect(self) -> bool:` |

### 2-2. 非同期処理

- `asyncio` と `threading` の境界を明確にする
- シリアル受信スレッド（`_reader`）から `asyncio.Future` に結果を渡すには `loop.call_soon_threadsafe()` を使う
- `await asyncio.wait_for(fut, timeout=...)` でタイムアウトを管理する

```python
# 良い例（スレッドから Future に結果を渡す）
self._loop.call_soon_threadsafe(fut.set_result, obj["approved"])

# 避ける例（スレッドから直接 set_result を呼ぶとスレッドセーフでない場合がある）
fut.set_result(obj["approved"])
```

### 2-3. エラーハンドリング

- `serial.SerialException` は `connect()` で捕捉してログ出力し、`False` を返す
- `_reader()` の無限ループ内では `Exception` を捕捉してログ出力後に `sleep(0.5)` で継続する（クラッシュ禁止）
- JSON デコードエラー（不正行）は `_on_line()` で `pass` する（ノイズ耐性）

```python
def _reader(self):
    while True:
        try:
            ...
        except Exception as e:
            print(f"[boo] Reader error: {e}", file=sys.stderr)
            time.sleep(0.5)   # 暴走せず継続
```

### 2-4. ログ出力

- ログはすべて `stderr` に出力する（`file=sys.stderr`）
- MCP の `stdout` はプロトコル通信専用のため、`print()` をデフォルト（`stdout`）に向けない

```python
# 良い例
print(f"[boo] Connected to {self.port}", file=sys.stderr)

# 避ける例（MCP プロトコルが壊れる）
print(f"Connected to {self.port}")
```

### 2-5. JSON 送受信

- 送信 JSON には `ensure_ascii=False` を指定して日本語をそのまま送れるようにする
- 送信文字列は `+ "\n"` で改行を付け、デバイス側の行区切り受信に対応する

```python
data = json.dumps(obj, ensure_ascii=False) + "\n"
self._ser.write(data.encode("utf-8"))
```

### 2-6. MCP ツール定義

- ツール名はすべて `snake_case`（例: `approve_request`, `notify_working`）
- `inputSchema` の `required` には必須パラメータのみを列挙する
- 未知のツール名には `{"error":"unknown tool"}` を返す

---

## 3. コメント規約

### 3-1. Arduino / C++

- ファイル先頭に **ブロックコメント**でファイル概要・通信プロトコルを記載する
- セクション区切りは `// ====` の区切り線 + 見出しコメント
- 副作用や注意事項がある場合のみインラインコメントを付ける
- 自明なコードにコメントを付けない

```cpp
// 良い例（意図が自明でないロジック）
// タイムアウト → 自動否認 + fed 大幅減
addFed(FED_TIMEOUT);

// 避ける例（コードをそのまま言葉にしているだけ）
// fed に FED_TIMEOUT を加算する
addFed(FED_TIMEOUT);
```

### 3-2. Python

- モジュール先頭に **docstring** でスクリプトの概要・使い方・MCP ツール一覧・依存ライブラリを記載する
- クラスのメソッドには **1 行 docstring** を付ける（自明なもの以外）
- 長い関数は `# ---- ここから何の処理 ----` でセクションを分ける

---

## 4. Git 規約

### 4-1. コミットメッセージ

**フォーマット:**
```
<type>: <summary（日本語可）>
```

| type | 用途 |
|------|------|
| `feat` | 新機能の追加 |
| `fix` | バグ修正 |
| `refactor` | 動作を変えないリファクタリング |
| `docs` | ドキュメントのみの変更 |
| `chore` | ビルド・設定・ツールの変更 |
| `test` | テストの追加・修正 |

**例:**
```
feat: Bluetooth SPP 対応（BluetoothSerial に切り替え）
fix: BT 切断時に ST_BT_WAIT へ遷移しない不具合を修正
docs: architecture.md にパーティション設計を追記
refactor: AsciiArt に lineColors[] を追加して per-line 色付け対応
```

### 4-2. ブランチ戦略

このプロジェクトは **1 名開発**のためシンプルな構成を採用する。

```
main                ← 動作確認済みのコード
└── feature/xxx     ← 機能開発ブランチ（任意）
```

- `main` には動作確認済みのコードのみをプッシュする
- 実験的な変更はブランチを切って作業する（小規模なら直接 `main` でも可）
- タグ: 実機動作確認済みの節目に `v2.x` 形式でタグを付ける

### 4-3. コミット単位

- 1 コミット = 1 つの論理的変更
- 複数ファイルをまたぐ場合でも「Bluetooth 対応」など 1 つの目的に収める
- デバッグコード・`Serial.printf` のコメントアウトをコミットに含めない

---

## 5. テスト規約

### 5-1. 実機テスト

Arduino スケッチはユニットテストが困難なため、**実機テストを正とする**。

| テスト項目 | 確認方法 |
|-----------|---------|
| Bluetooth 接続 | `boo_bridge.py --port /dev/ttyS5` で `[boo] Connected` が出力される |
| 承認フロー | `approve_request` ツール呼び出し → デバイス画面確認 → ボタン押下 → `{"approved":true}` 返却 |
| タイムアウト | timeout=5 で呼び出し、5 秒待機後に `{"approved":false}` が返る |
| BT 切断検知 | ブリッジ停止後、デバイスが 5 秒以内に `ST_BT_WAIT` 画面へ遷移する |
| energy 消費 | `notify_working` 送信後、スリープ → energy ゲージが増加する |

### 5-2. モックテスト

デバイスなしで MCP ツールのインターフェースを確認する場合は `--mock` フラグを使用する。

```bash
python boo_bridge.py --mock
# → approve_request 呼び出しで 2 秒後に approved=true が返る
```

### 5-3. シリアル通信の単体確認

デバイスの JSON 受信を ArduinoSerial Monitor で確認する場合は、  
USB Serial を `Serial.begin(115200)` で開き、Bluetooth の JSON デバッグ出力を確認する。

```
送信テスト（Arduino Serial Monitor で直接送信してもデバイスは無視する）
→ デバッグ確認は boo_bridge.py 経由（Bluetooth）のみ
```

---

## 6. フォーマット規約

### 6-1. Arduino / C++

| 項目 | 規則 |
|------|------|
| インデント | スペース 2 つ |
| 行末 | 末尾スペースなし |
| 1 行の最大長 | 100 文字（目安） |
| 波括弧 | 開き括弧は同じ行に置く（K&R スタイル） |

```cpp
// 良い例（K&R スタイル）
void tickDecay(uint32_t now) {
  if (now - gLastDecayMs < 60000UL) return;
  ...
}

// 避ける例（Allman スタイルは使わない）
void tickDecay(uint32_t now)
{
  ...
}
```

### 6-2. Python

| 項目 | 規則 |
|------|------|
| インデント | スペース 4 つ（PEP 8） |
| 行末 | 末尾スペースなし |
| 1 行の最大長 | 88 文字（Black デフォルト。目安） |
| 文字列 | ダブルクォート優先 |
| 型ヒント | 公開メソッドの引数・戻り値に付ける |

```python
# 良い例
def connect(self) -> bool:
    ...

async def request_approval(
    self,
    tool: str,
    details: str = "",
    danger: bool = False,
    timeout: int = 30
) -> bool:
    ...
```

---

## 7. セキュリティ考慮事項

| 項目 | 対応 |
|------|------|
| Bluetooth PIN | ハードコード `"1234"`（開発者ツールとして割り切る。変更は `#define BT_PIN` で） |
| JSON インジェクション | `ArduinoJson` と Python `json` モジュールが適切にエスケープするため問題なし |
| バッファオーバーフロー | `strlcpy` 使用・`sizeof` ガードで防止 |
| COM ポート権限 | `dialout` グループへの追加または `chmod 666` を運用ドキュメントに明記 |
| `--mock` フラグ | 本番運用では使用しない（テスト専用） |
