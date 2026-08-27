# PC側: ESP32に文字列を送信し、返答を表示するプログラム
import socket
from pynput import keyboard

# ESP32のアクセスポイントへ接続するためのIPアドレスと待受ポート
ESP32_IP = "192.168.4.1"
PORT = 8000

# 押しっぱなしの時の再送信間隔
WASD_KEYS = {"w", "a", "s", "d"}

# 1.ESP32のアクセスポイントに接続する
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.connect((ESP32_IP, PORT))
print(f"ESP32のアクセスポイントに接続しました: {ESP32_IP}:{PORT}")

def send_msg(msg):
    """1文字の文字列をESP32に送信する関数"""
    sock.sendall(msg.encode())
    print(f"[send] {msg}")

# 2. キーが押されたときの処理
def on_press(key):
    """キーが押されたときの処理"""
    ch = getattr(key, 'char', None)
    if ch in WASD_KEYS:
        send_msg(ch)

# 3. キーが離されたときの処理
def on_release(key):
    """キーが離されたときの処理"""
    if key == keyboard.Key.esc:
        # ESCキーが押されたら終了
        print("ESCキーが押されたため終了します。")
        send_msg("x")
        sock.close()
        return False
    
    ch = getattr(key, 'char', None)
    if ch in WASD_KEYS:
        send_msg("x") 
    
# 4. キーボードの監視を開始
with keyboard.Listener(on_press=on_press, on_release=on_release) as listener:
    listener.join()