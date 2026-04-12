# 手順書 — Arduino IDE 事前設定（Bluetooth SPP 対応）

## 概要

`boo_device.ino` に `<BluetoothSerial.h>` を追加する前に、Arduino IDE の
Partition Scheme を変更する必要がある。この設定を行わないとコンパイルエラーになる。

> **理由**: ESP32 の Bluetooth Classic スタックは約 1.2MB を消費する。
> デフォルトの Partition Scheme（APP 領域 1.2MB）ではスタックと
> スケッチが共存できないため、APP 領域 3MB の設定に切り替える。

---

## 対象環境

| 項目 | 値 |
|------|-----|
| ボード | M5StickC PLUS2 |
| Arduino IDE | 2.x |
| 接続 | USB-C ケーブルで PC に接続済み |

---

## 手順

### ステップ 1: ボードを選択する

1. Arduino IDE を起動する
2. メニューバーから **Tools（ツール）** を開く
3. **Board（ボード）** > **M5Stack Arduino** > **M5StickC PLUS2** を選択する

> 既に選択済みの場合はスキップ

---

### ステップ 2: Partition Scheme を変更する

1. メニューバーから **Tools（ツール）** を開く
2. **Partition Scheme** をクリックする
3. **Huge APP (3MB No OTA/1MB SPIFFS)** を選択する

```
変更前: Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)
変更後: Huge APP (3MB No OTA/1MB SPIFFS)
```

> ⚠️ この設定は Arduino IDE が保持するため、ボードを再選択するとリセットされる場合がある。
> コンパイル前に毎回 Tools メニューで確認することを推奨する。

---

### ステップ 3: 設定を確認する

Tools メニューを再度開き、以下のとおりになっていることを確認する。

| 項目 | 設定値 |
|------|--------|
| Board | M5StickC PLUS2 |
| Partition Scheme | Huge APP (3MB No OTA/1MB SPIFFS) |
| Upload Speed | 1500000 |

---

### ステップ 4: 動作確認（任意）

既存のスケッチ（変更前の `boo_device.ino`）を **Verify（検証）** して、
現時点でコンパイルエラーがないことを確認しておくと、後の切り分けが容易になる。

1. `src/boo_device.ino` を Arduino IDE で開く
2. ✓ ボタン（Verify）をクリックする
3. `Compilation complete.` が表示されることを確認する

---

## 完了条件

- [ ] Tools > Partition Scheme が **Huge APP (3MB No OTA/1MB SPIFFS)** になっている
- [ ] 既存スケッチの Verify が成功する（任意）

完了したら `boo_device.ino` の実装（T-02〜T-15）に進む。
