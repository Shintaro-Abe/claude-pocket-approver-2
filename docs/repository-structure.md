# リポジトリ構造定義書

> プロダクトビジョンは `product-requirements.md`、機能設計は `functional-design.md`、技術仕様は `architecture.md` を参照。  
> 本ドキュメントは「**どこに何を置くか**」のファイル配置ルールを定義する。

---

## 1. ディレクトリ全体構成

```
claude-pocket-approver-2/
│
├── CLAUDE.md                        # Claude Code プロジェクトメモリ（開発プロセス定義）
│
├── docs/                            # 永続的ドキュメント（設計・仕様）
│   ├── product-requirements.md      # プロダクト要求定義書
│   ├── functional-design.md         # 機能設計書
│   ├── architecture.md              # 技術仕様書
│   ├── repository-structure.md      # リポジトリ構造定義書（本ドキュメント）
│   ├── development-guidelines.md    # 開発ガイドライン
│   └── glossary.md                  # ユビキタス言語定義
│
├── README.md                        # ユーザー向けセットアップ・操作ガイド
│
├── src/                             # ソースコード
│   ├── boo_bridge.py                # PC 側 MCP ブリッジ（Python）
│   ├── boo_hook.sh                  # Claude Code PreToolUse フック（bash）
│   ├── boo_test.py                  # 手動テスト用スクリプト
│   └── boo_device/
│       └── boo_device.ino           # M5StickC PLUS2 Arduino スケッチ
│
├── .claude/                         # Claude Code プロジェクト設定（gitignore）
│   ├── settings.json                # フック設定（PreToolUse）
│   └── settings.local.json          # 権限設定（Claude Code が自動管理）
│
└── .steering/                       # 作業単位ドキュメント（実装履歴）
    └── [YYYYMMDD]-[タイトル]/
        ├── requirements.md          # 作業要求
        ├── design.md                # 実装設計
        └── tasklist.md              # タスクリスト・進捗
```

---

## 2. ディレクトリ別の役割

### 2-1. `docs/` — 永続的ドキュメント

プロジェクト全体の「北極星」。アプリケーションの基本設計・方針が変わらない限り更新されない。

| ファイル | 役割 | 更新タイミング |
|----------|------|---------------|
| `product-requirements.md` | プロダクトビジョン・要件・受け入れ条件 | 要件変更時のみ |
| `functional-design.md` | 機能アーキテクチャ・画面遷移・通信プロトコル | 機能仕様変更時のみ |
| `architecture.md` | 技術スタック・環境構築・制約事項 | ライブラリ変更・環境変更時のみ |
| `repository-structure.md` | ファイル配置ルール（本ドキュメント） | 構成変更時のみ |
| `development-guidelines.md` | コーディング規約・命名規則・Git 規約 | 規約変更時のみ |
| `glossary.md` | ドメイン用語・英日対応表 | 用語追加・変更時のみ |

### 2-2. `src/` — ソースコード

実装の本体。2つのターゲット環境に分かれる。

| ファイル | ターゲット | 役割 |
|----------|-----------|------|
| `boo_device/boo_device.ino` | M5StickC PLUS2（ESP32） | デバイス側ファームウェア。Bluetooth SPP サーバー、表示、ボタン処理、JSON 通信 |
| `boo_bridge.py` | Windows（Python） | PC 側 MCP ブリッジ。MCP stdio/SSE、REST `/request` エンドポイント、Bluetooth COM ポート接続 |
| `boo_hook.sh` | Dev Container（bash） | Claude Code PreToolUse フック。Bash 実行前に `/request` を呼び出して承認を要求 |
| `boo_test.py` | Windows（Python） | 手動テスト用スクリプト。MCP を介さず COM ポートへ直接接続して通信確認 |

### 2-3. `.steering/` — 作業単位ドキュメント

特定の実装作業における意図・設計・進捗を記録する。作業完了後も履歴として保持する。

```
.steering/
├── 20260412-bluetooth-spp/   # Bluetooth 対応実装
│   ├── requirements.md
│   ├── design.md
│   └── tasklist.md
├── 20260415-add-feature-x/              # 例：将来の機能追加
│   └── ...
└── ...
```

---

## 3. ファイル配置ルール

### 3-1. 新しいソースファイルの配置

| ファイルの種類 | 配置先 | 例 |
|--------------|--------|-----|
| Arduino スケッチ（`.ino`） | `src/boo_device/` | `src/boo_device/boo_device.ino` |
| Python スクリプト（`.py`） | `src/` | `src/boo_bridge.py` |
| シェルスクリプト（`.sh`） | `src/` | `src/boo_hook.sh` |
| ユーザー向けドキュメント | ルート `README.md` に統合 | — |
| テストスクリプト | `src/` | `src/boo_test.py` |

> **Note:** Arduino スケッチは Arduino IDE の仕様上、スケッチ名と同名のディレクトリが必要なため `src/boo_device/` にのみサブディレクトリを作成する。Python スクリプトは `src/` 直下のフラット構成を維持する。

### 3-2. ドキュメントの配置

| ドキュメントの種類 | 配置先 |
|------------------|--------|
| プロジェクト全体の設計・仕様 | `docs/` |
| 特定の作業・変更の記録 | `.steering/[YYYYMMDD]-[タイトル]/` |
| ユーザー操作ガイド | ルート `README.md` |
| Claude Code 用プロジェクト設定 | `CLAUDE.md`（ルート直下） |

### 3-3. 配置してはいけないもの

| 種類 | 理由 |
|------|------|
| `.env` / シークレットファイル | 認証情報の漏洩リスク |
| コンパイル済みバイナリ（`.bin`） | バイナリは再ビルドで生成可能。Git 管理対象外 |
| Arduino IDE の一時ファイル（`build/`） | IDE が自動生成。`.gitignore` で除外 |
| Python 仮想環境（`.venv/`） | `pip install` で再現可能。`.gitignore` で除外 |
| `__pycache__/` | Python が自動生成。`.gitignore` で除外 |

---

## 4. 命名規則

### 4-1. ファイル名

| 対象 | 規則 | 例 |
|------|------|-----|
| Python スクリプト | `snake_case.py` | `boo_bridge.py`, `test_bridge.py` |
| Arduino スケッチ | `snake_case.ino` | `boo_device.ino` |
| Markdown ドキュメント | `kebab-case.md` | `product-requirements.md`, `functional-design.md` |
| `.steering/` ディレクトリ | `YYYYMMDD-kebab-case` | `20260412-bluetooth-spp` |

### 4-2. `.steering/` ディレクトリ名

```
[YYYYMMDD]-[作業内容を表す英語タイトル]
```

| 良い例 | 悪い例 |
|--------|--------|
| `20260412-bluetooth-spp` | `work1` |
| `20260415-add-sleep-animation` | `fix` |
| `20260420-improve-reconnect-logic` | `20260420` |

---

## 5. `.gitignore` 推奨設定

```gitignore
# Python
.venv/
__pycache__/
*.pyc
*.pyo

# Arduino IDE
build/
*.bin
*.elf

# エディタ
.vscode/
*.swp
*~

# OS
.DS_Store
Thumbs.db
```

---

## 6. ファイル依存関係

```
CLAUDE.md
    ↓ 参照
docs/
    product-requirements.md
        ↓ 要件を受ける
    functional-design.md
        ↓ 設計を受ける
    architecture.md
        ↓ 技術仕様を受ける
    development-guidelines.md

src/
    boo_device.ino      ←→     src/boo_bridge.py  ←─── src/boo_hook.sh
    （ESP32 BT SPP）   JSON     （Windows Python）  REST  （Dev Container）
         ↑                              ↑                       ↑
    functional-design.md          functional-design.md    architecture.md
    architecture.md               architecture.md         (セクション 9)
```

---

## 7. ファイルサイズの目安

| ファイル | 現在 | 上限目安 |
|----------|------|---------|
| `boo_device/boo_device.ino` | ~700 行 | 1,000 行 |
| `boo_bridge.py` | ~410 行 | 500 行 |
| `boo_hook.sh` | ~70 行 | 150 行 |
| `README.md`（ルート） | ~400 行 | 制限なし |

> **上限を超える場合：**  
> `boo_device.ino` はタブ分割（`.h` / `.cpp`）を検討する。ただし Arduino IDE での分割には制約があるため、まず単一ファイルで完結させることを優先する。
