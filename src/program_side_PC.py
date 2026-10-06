# PC側: キーボード操作でESP32(二輪ローバー)を遠隔操作するプログラム
#
# 【使い方】
#   1. PCのWiFiをESP32のアクセスポイント(SSID: ESP32_AP)に接続する
#   2. python program_side_PC.py を実行する
#
# 【操作方法】
#   W / ↑ : 前進          S / ↓ : 後退
#   A / ← : その場左旋回  D / → : その場右旋回
#   W+A / W+D : 前進しながら左右にカーブ
#   S+A / S+D : 後退しながら左右にカーブ
#   1〜9 : 速度レベル（1が最も遅く、9が最も速い）
#   ESC : 終了
#
# キーを押している間は SEND_INTERVAL 毎に同じコマンドを送り続ける。
# ESP32側は一定時間受信がないと自動停止するので、通信が途切れても暴走しない。
# ※キー入力はウィンドウのフォーカスに関係なく拾われる。日本語入力(IME)はオフにしておくこと。
import socket
import threading
import time

from pynput import keyboard

# ESP32のアクセスポイントへ接続するためのIPアドレスと待受ポート
ESP32_IP = "192.168.4.1"
PORT = 8000

# 押しっぱなしの時の再送信間隔[秒]（ESP32側の自動停止時間 300ms より十分短くする）
SEND_INTERVAL = 0.1
# 接続に失敗したときに再接続を試みるまでの待ち時間[秒]
RECONNECT_INTERVAL = 1.0

# 矢印キーもWASDと同じように扱う
ARROW_KEYS = {
    keyboard.Key.up: "w",
    keyboard.Key.left: "a",
    keyboard.Key.down: "s",
    keyboard.Key.right: "d",
}
WASD_KEYS = {"w", "a", "s", "d"}
SPEED_KEYS = set("123456789")

# キーボード監視スレッドとメインスレッドで共有する状態
lock = threading.Lock()
pressed_keys = set()     # 現在押されている移動キー
pending_speed = []       # 送信待ちの速度レベル
key_changed = threading.Event()
running = True


def key_to_char(key):
    """pynputのキーを 'w' などの1文字に変換する（対象外のキーはNone）"""
    if key in ARROW_KEYS:
        return ARROW_KEYS[key]
    ch = getattr(key, "char", None)
    return ch.lower() if ch else None


def decide_command(keys):
    """押されているキーの組み合わせから、送信するコマンド1文字を決める"""
    forward = "w" in keys and "s" not in keys
    backward = "s" in keys and "w" not in keys
    left = "a" in keys and "d" not in keys
    right = "d" in keys and "a" not in keys

    if forward:
        return "q" if left else "e" if right else "w"
    if backward:
        return "z" if left else "c" if right else "s"
    if left:
        return "a"
    if right:
        return "d"
    return "x"


# ----- キーボード監視（別スレッドで呼ばれる） -----
def on_press(key):
    """キーが押されたときの処理"""
    global running
    if key == keyboard.Key.esc:
        print("ESCキーが押されたため終了します。")
        running = False
        key_changed.set()
        return False  # キーボード監視を終了

    ch = key_to_char(key)
    with lock:
        if ch in WASD_KEYS:
            pressed_keys.add(ch)
        elif ch in SPEED_KEYS:
            pending_speed.append(ch)
        else:
            return
    key_changed.set()


def on_release(key):
    """キーが離されたときの処理"""
    ch = key_to_char(key)
    if ch in WASD_KEYS:
        with lock:
            pressed_keys.discard(ch)
        key_changed.set()


# ----- 通信 -----
def connect():
    """ESP32に接続できるまで再試行する。終了操作をされたらNoneを返す"""
    while running:
        try:
            sock = socket.create_connection((ESP32_IP, PORT), timeout=3.0)
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)  # 1文字ずつすぐ送る
            sock.settimeout(1.0)
            print(f"ESP32に接続しました: {ESP32_IP}:{PORT}")
            return sock
        except OSError as e:
            print(f"接続できません({e})。{RECONNECT_INTERVAL}秒後に再試行します…")
            time.sleep(RECONNECT_INTERVAL)
    return None


def main():
    listener = keyboard.Listener(on_press=on_press, on_release=on_release)
    listener.start()
    print("操作: WASD/矢印キーで移動, 1〜9で速度変更, ESCで終了")

    sock = connect()
    last_command = None

    while running and sock is not None:
        key_changed.wait(SEND_INTERVAL)
        key_changed.clear()

        with lock:
            command = decide_command(pressed_keys)
            speeds = pending_speed[:]
            pending_speed.clear()

        msg = "".join(speeds)
        # 走行中は毎回送る（ESP32の自動停止を防ぐ）。停止は切り替わったときだけ送る
        if command != "x" or last_command != "x":
            msg += command

        if not msg:
            continue
        try:
            sock.sendall(msg.encode())
        except OSError as e:
            print(f"通信が切れました({e})。再接続します…")
            sock.close()
            sock = connect()
            last_command = None
            continue

        for s in speeds:
            print(f"[speed] {s}")
        if command != last_command:
            print(f"[send] {command}")
        last_command = command

    # 終了時は停止コマンドを送ってから切断する
    if sock is not None:
        try:
            sock.sendall(b"x")
        except OSError:
            pass
        sock.close()
    listener.stop()


if __name__ == "__main__":
    main()
