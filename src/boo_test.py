#!/usr/bin/env python3
"""
boo_test.py — T-25/T-26/T-27 手動テスト用スクリプト
COM ポートに直接接続し、approve リクエストを送信して結果を確認する。

使い方:
  python boo_test.py --port COM5
"""

import argparse
import json
import serial
import threading
import time
import sys


def open_with_retry(port: str, baud: int, max_retries: int = 5, delay: float = 3.0):
    """COM ポートが解放されるまでリトライしながら接続する"""
    for attempt in range(1, max_retries + 1):
        try:
            ser = serial.Serial(port, baud, timeout=1)
            print(f"[test] 接続成功 (試行 {attempt}/{max_retries})")
            return ser
        except serial.SerialException as e:
            if attempt < max_retries:
                print(f"[test] 接続待機中... ({attempt}/{max_retries}) : {e}")
                time.sleep(delay)
            else:
                print(f"[error] 接続失敗: {e}")
                sys.exit(1)


def read_loop(ser: serial.Serial):
    """受信スレッド: デバイスからの JSON レスポンスを表示"""
    while True:
        try:
            line = ser.readline()
            if line:
                text = line.decode("utf-8", errors="replace").strip()
                print(f"\n[device -> PC] {text}")
        except serial.SerialException:
            break


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM5", help="Bluetooth SPP COM ポート")
    args = parser.parse_args()

    print(f"[test] COM ポートに接続中: {args.port}")
    ser = open_with_retry(args.port, 115200)

    print("[test] BT 接続確立を待機中 (5秒)...")
    time.sleep(5)

    # 受信スレッド起動
    t = threading.Thread(target=read_loop, args=(ser,), daemon=True)
    t.start()

    # M5StickC が ST_IDLE になったことをユーザーが目視確認してから送信
    print("\n[test] M5StickC の画面が ST_IDLE（ブー + BT 表示）になったことを確認してください。")
    input("       確認できたら Enter を押してください... ")

    # T-25: approve リクエスト送信
    msg = {
        "type": "approve",
        "tool": "Bash",
        "details": "ls -la /tmp",
        "danger": False,
        "timeout": 30,
    }
    print(f"\n[test] T-25: approve リクエスト送信")
    print(f"[PC -> device] {json.dumps(msg)}")
    ser.write(json.dumps(msg).encode("utf-8") + b"\n")

    print("\n[test] M5StickC に承認画面が表示されたら:")
    print("       ボタン A → 承認 (approved=true  が返るはず) ← T-26 確認")
    print("       ボタン B → 否認 (approved=false が返るはず) ← T-26 確認")
    print("       30秒でタイムアウト自動否認")
    print("\n[test] 結果を待機中... (Ctrl+C で終了)\n")

    try:
        time.sleep(35)
    except KeyboardInterrupt:
        pass

    ser.close()
    print("\n[test] 完了")


if __name__ == "__main__":
    main()
