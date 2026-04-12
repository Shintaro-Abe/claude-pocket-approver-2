# 手順書 — Arduino IDE 事前設定（Bluetooth SPP 対応）

## 概要

`boo_device.ino` に `<BluetoothSerial.h>` を追加する前に、Arduino IDE の
Partition Scheme を変更する必要がある。この設定を行わないとコンパイルエラーになる。

> **理由**: ESP32 の Bluetooth Classic スタックは約 1.2MB を消費する。
> デフォルトの Partition Scheme（APP 領域 1.2MB）ではスタックと
> スケッチが共存できないため、APP 領域 3MB の設定に切り替える。

---

## 作業環境

| 項目 | 値 |
|------|-----|
| 作業端末 | **Windows 11**（Arduino IDE は Windows 上で動作する。WSL/devcontainer では動作しない） |
| ボード | M5StickC PLUS2 |
| Arduino IDE | 2.x（Windows にインストール済みであること） |
| 接続 | USB-C ケーブルで Windows PC に直接接続する |

> ⚠️ **WSL 上では作業しない。** Arduino IDE はシリアルポートや USB を直接扱うため、
> Windows ネイティブアプリとして起動する必要がある。

---

## 事前確認

作業を始める前に以下をすべて満たしていることを確認する。

### A. Arduino IDE のインストール確認

Windows の スタートメニュー から **Arduino IDE** を検索し、起動できることを確認する。

インストールされていない場合は https://www.arduino.cc/en/software からインストールする。

---

### B. M5StickC PLUS2 ボードパッケージの確認

1. Arduino IDE を起動する
2. **Tools > Board > Boards Manager** を開く
3. 検索欄に `M5Stack` と入力する
4. **M5Stack** パッケージが **INSTALLED** になっていることを確認する

インストールされていない場合:
1. **M5Stack** の右側にある **Install** ボタンをクリックする
2. インストール完了まで待つ（数分かかる場合がある）

---

### C. USB 接続と COM ポートの確認

1. M5StickC PLUS2 を USB-C ケーブルで Windows PC に接続する
2. Arduino IDE のメニューバーから **Tools > Port** を開く
3. `COM3` や `COM5` のようなポートが表示されていることを確認する

> ⚠️ **Port に何も表示されない場合:**
> - ケーブルが「充電専用」ではなく「データ通信対応」であることを確認する
> - デバイスマネージャー（Win + X > デバイスマネージャー）で **ポート (COM と LPT)** を確認する
> - ドライバがインストールされていない場合は CP210x ドライバをインストールする

---

### D. スケッチファイルの場所を確認する

スケッチは WSL 内の以下のパスにある。

```
/workspaces/claude-pocket-approver-2/src/boo_device.ino
```

Windows の Arduino IDE からこのファイルを開くには、
エクスプローラーのアドレスバーに以下を入力してアクセスする。

```
\\wsl.localhost\Ubuntu\workspaces\claude-pocket-approver-2\src
```

> **WSL ディストリビューション名が `Ubuntu` でない場合:**
> WSL のターミナルで以下を実行すると、エクスプローラーがそのフォルダで開く。
> ```bash
> explorer.exe .
> ```
> 表示されたフォルダのアドレスバーでディストリビューション名を確認できる。

---

## 手順

### ステップ 1: 変更前の状態を記録する

**変更前の Partition Scheme を確認し、メモしておく。**

1. Arduino IDE を起動する
2. **Tools > Board > M5Stack Arduino > M5StickC PLUS2** を選択する
3. **Tools > Partition Scheme** を開く
4. 現在選択されている値を確認してメモする（例: `Default 4MB with spiffs`）

> 変更前の状態を記録しておくと、問題が発生したときに元に戻せる。

---

### ステップ 2: スケッチを開く

1. Arduino IDE のメニューバーから **File > Open** を選択する
2. エクスプローラーのアドレスバーに以下を入力して Enter を押す
   ```
   \\wsl.localhost\Ubuntu\workspaces\claude-pocket-approver-2\src
   ```
3. `boo_device.ino` を選択して **開く** をクリックする
4. スケッチがエディタに表示されることを確認する

---

### ステップ 3: Partition Scheme を変更する

1. メニューバーから **Tools** を開く
2. **Partition Scheme** をクリックする
3. **Huge APP (3MB No OTA/1MB SPIFFS)** を選択する

```
変更前: Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)
変更後: Huge APP (3MB No OTA/1MB SPIFFS)          ← これを選択
```

---

### ステップ 4: 変更後の状態を確認する

**Tools メニューを再度開き**、以下の設定値になっていることをすべて確認する。

| 項目 | 確認する設定値 |
|------|----------------|
| Board | M5StickC PLUS2 |
| Partition Scheme | **Huge APP (3MB No OTA/1MB SPIFFS)** |
| Upload Speed | 1500000 |
| Port | COM\<N\>（接続済みのポート番号） |

> ⚠️ **Partition Scheme が変わっていない場合:**
> ボードの選択が正しくないと Partition Scheme の選択肢が変わる。
> 先に **Tools > Board > M5Stack Arduino > M5StickC PLUS2** を再選択してから、
> 改めて Partition Scheme を設定する。

---

### ステップ 5: コンパイルで動作確認する

現時点のスケッチ（`boo_device.ino` v2.0、未変更）が正しくコンパイルできることを確認する。

1. Arduino IDE 左上の **✓（Verify）** ボタンをクリックする
2. 下部のコンソールに以下が表示されることを確認する
   ```
   Compilation complete.
   ```

> **エラーが出た場合の確認ポイント:**
> - `fatal error: M5StickCPlus2.h: No such file or directory`
>   → M5Stack ボードパッケージが正しくインストールされていない（事前確認 B を再実施）
> - `multiple definition of ...` や `region 'iram0_0_seg' overflowed`
>   → Partition Scheme が変更されていない（ステップ 3〜4 を再確認）

---

## 注意事項

| 注意点 | 詳細 |
|--------|------|
| ボード切り替え時の設定リセット | **Tools > Board** で別のボードを選択すると Partition Scheme がデフォルトに戻る。M5StickC PLUS2 を再選択したら必ず Partition Scheme も確認すること |
| ファイルの編集場所 | コードの編集は **WSL（devcontainer）上** で行い、Arduino IDE はコンパイル・書き込み専用として使う。Arduino IDE 側でファイルを編集すると WSL 側に反映されないことがある |
| スケッチの再読み込み | WSL 側でファイルを変更した後、Arduino IDE でコンパイルするとエディタ上の表示と実際のファイルが異なる場合がある。**File > Revert** または Arduino IDE を再起動して最新の状態を読み込む |

---

## 完了条件

以下がすべて満たされたら完了。

- [ ] Tools > Board: **M5StickC PLUS2** になっている
- [ ] Tools > Partition Scheme: **Huge APP (3MB No OTA/1MB SPIFFS)** になっている
- [ ] Tools > Port: COM ポートが選択されている
- [ ] Verify（コンパイル検証）が `Compilation complete.` で完了する

完了したら `boo_device.ino` の実装（T-02〜T-15）に進む。
