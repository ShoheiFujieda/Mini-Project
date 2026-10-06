/*
  二輪ローバー 遠隔操作プログラム（ESP32側）
  ESP32 + TB67H450FNG x2（左右モータ独立 / PWM速度制御）
  --------------------------------------------------------------------------
  ・ESP32自身がWiFiアクセスポイントになり、TCPサーバ(ポート8000)でPCからの1文字コマンドを受信する。
  ・TB67H450のIN1/IN2の両方にPWMを割り当て、前進・後退・旋回すべてで速度を制御する。
        IN1   IN2   動作
        PWM   L     正転（デューティ比で速度調整）
        L     PWM   逆転（デューティ比で速度調整）
        L     L     停止（空転）
  ・急発進・急反転を防ぐため、目標速度に向けて少しずつデューティを変化させる（ランプ制御）。
    → モータの突入電流を抑え、荷台の箱も落ちにくくなる。

  【配線】
    左モータドライバ  IN1 -> GPIO26 , IN2 -> GPIO27
    右モータドライバ  IN1 -> GPIO32 , IN2 -> GPIO33

  【コマンド】（PC側 program_side_PC.py が送信する）
    w : 前進          s : 後退
    a : その場左旋回  d : その場右旋回
    q : 前進しながら左カーブ   e : 前進しながら右カーブ
    z : 後退しながら左カーブ   c : 後退しながら右カーブ
    x / 半角スペース : 停止
    1〜9 : 速度レベル（1が最も遅く、9が最も速い）

  【安全機能】
    ・COMMAND_TIMEOUT_MS の間なにも受信しなければ自動停止する（PC側は走行中100ms毎にコマンドを再送する）。
    ・PCとの接続が切れたら即座に停止する。

  ※USBシリアル(115200bps)から同じコマンドを送っても操作できる（WiFiなしでの動作確認用）。
*/

#include <WiFi.h>

// ----- WiFi設定 -----
const char *AP_SSID     = "ESP32_AP";
const char *AP_PASSWORD = "rover2team";  // 8文字以上でないとアクセスポイントを起動できない
const uint16_t SERVER_PORT = 8000;

WiFiServer server(SERVER_PORT);
WiFiClient client;

// ----- ピン設定（配線に合わせて変更） -----
const int LEFT_IN1  = 26;
const int LEFT_IN2  = 27;
const int RIGHT_IN1 = 32;
const int RIGHT_IN2 = 33;

// ----- PWM設定 -----
const uint32_t PWM_FREQ       = 20000;  // 20kHz（可聴域外にしてモータの「キーン」音を防ぐ）
const uint8_t  PWM_RESOLUTION = 8;      // 8bit → デューティ 0〜255
const int      PWM_MAX        = 255;

// ----- 速度設定（実機で調整する） -----
const int SPEED_LEVEL_MIN = 1;
const int SPEED_LEVEL_MAX = 9;
int speedLevel = 5;                 // 起動時の速度レベル
const int MIN_RUN_DUTY = 80;        // 速度レベル1のデューティ（これ未満だとモータが回り出さない想定）
const float TURN_RATIO        = 0.6;  // その場旋回の速さ（基本速度に対する比率）
const float CURVE_INNER_RATIO = 0.4;  // カーブ時の内側の車輪の速さ（基本速度に対する比率）

// 左右モータの個体差の補正（まっすぐ進まないときに片側を1.0未満にする）
const float LEFT_GAIN  = 1.0;
const float RIGHT_GAIN = 1.0;

// ----- ランプ制御（加減速の緩やかさ） -----
const int RAMP_STEP = 15;                    // 1回あたりのデューティ変化量
const unsigned long RAMP_INTERVAL_MS = 10;   // 変化させる間隔 → 0から255まで約170ms

// ----- 安全タイムアウト -----
const unsigned long COMMAND_TIMEOUT_MS = 300;  // 最後の受信からこの時間を超えたら自動停止
unsigned long lastCommandMillis = 0;
char currentCommand = 'x';  // 現在の走行状態（デフォルトで停止中）

// ----- モータ -----
struct Motor {
  int in1Pin;
  int in2Pin;
  int in1Channel;  // ESP32 Arduinoコア2.x用のPWMチャンネル番号
  int in2Channel;
  float gain;
  int current;     // 現在のデューティ（-255〜255、負は逆転）
  int target;      // 目標のデューティ
};

Motor leftMotor  = {LEFT_IN1,  LEFT_IN2,  0, 1, LEFT_GAIN,  0, 0};
Motor rightMotor = {RIGHT_IN1, RIGHT_IN2, 2, 3, RIGHT_GAIN, 0, 0};

// =================================================
// PWM（Arduinoコア 3.x と 2.x の両方に対応）
// =================================================

void pwmAttach(int pin, int channel) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  (void)channel;
  ledcAttach(pin, PWM_FREQ, PWM_RESOLUTION);
#else
  ledcSetup(channel, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(pin, channel);
#endif
}

void pwmWrite(int pin, int channel, int duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  (void)channel;
  ledcWrite(pin, duty);
#else
  (void)pin;
  ledcWrite(channel, duty);
#endif
}

// =================================================
// セットアップ / メインループ
// =================================================

void setup() {
  Serial.begin(115200);

  setupMotor(leftMotor);
  setupMotor(rightMotor);

  if (!WiFi.softAP(AP_SSID, AP_PASSWORD)) {
    Serial.println("[error] WiFiアクセスポイントを起動できませんでした（パスワードは8文字以上必要）");
  }
  Serial.print("AP SSID: ");
  Serial.println(AP_SSID);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());   // 通常 192.168.4.1

  server.begin();
  Serial.println("TCP server started. Waiting for connection...");
}

void loop() {
  acceptClient();

  // PCから届いている文字を全部処理する
  if (client && client.connected()) {
    while (client.available() > 0) {
      handleCommand((char)client.read());
    }
  }

  // USBシリアルからの文字も同じように処理する（動作確認用）
  while (Serial.available() > 0) {
    handleCommand((char)Serial.read());
  }

  // 一定時間コマンドが来なければ自動停止
  if (currentCommand != 'x' && (millis() - lastCommandMillis) > COMMAND_TIMEOUT_MS) {
    setCommand('x');
    Serial.println("[timeout] auto stop");
  }

  updateMotors();
}

// =================================================
// 通信
// =================================================

void acceptClient() {
  // 新しい接続が来たら、古い接続を捨てて乗り換える
  // （PC側が異常終了して古い接続が残っていても、再接続できるようにするため）
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  WiFiClient newClient = server.accept();
#else
  WiFiClient newClient = server.available();
#endif
  if (newClient) {
    if (client) {
      client.stop();
    }
    client = newClient;
    client.setNoDelay(true);  // 1文字ずつ遅延なく受信する
    lastCommandMillis = millis();
    Serial.println("[connected] PC client");
    return;
  }

  // 接続が切れたらすぐ止める
  static bool wasConnected = false;
  bool connected = client && client.connected();
  if (wasConnected && !connected) {
    setCommand('x');
    Serial.println("[disconnected] stop");
  }
  wasConnected = connected;
}

void handleCommand(char c) {
  // 改行やCRは無視
  if (c == '\n' || c == '\r') {
    return;
  }

  // 速度レベル変更
  if (c >= '0' + SPEED_LEVEL_MIN && c <= '0' + SPEED_LEVEL_MAX) {
    lastCommandMillis = millis();
    speedLevel = c - '0';
    Serial.print("[speed] level ");
    Serial.println(speedLevel);
    applyCommand(currentCommand);  // 走行中なら新しい速度をすぐ反映する
    return;
  }

  if (c == ' ') {
    c = 'x';
  }
  c = (char)tolower(c);

  switch (c) {
    case 'w': case 's': case 'a': case 'd':
    case 'q': case 'e': case 'z': case 'c':
    case 'x':
      lastCommandMillis = millis();
      setCommand(c);
      break;
    default:
      // 未対応の文字は無視
      break;
  }
}

void setCommand(char c) {
  // PC側は走行中に同じコマンドを再送してくるので、変化したときだけ表示する
  if (c != currentCommand) {
    Serial.print("[cmd] ");
    Serial.println(c);
  }
  currentCommand = c;
  applyCommand(c);
}

// =================================================
// 走行パターン（左右の目標速度を決める）
// =================================================

void applyCommand(char c) {
  int base  = MIN_RUN_DUTY + (PWM_MAX - MIN_RUN_DUTY) * (speedLevel - SPEED_LEVEL_MIN) / (SPEED_LEVEL_MAX - SPEED_LEVEL_MIN);
  int turn  = (int)(base * TURN_RATIO);
  int inner = (int)(base * CURVE_INNER_RATIO);

  int left = 0;
  int right = 0;

  switch (c) {
    case 'w': left =  base;  right =  base;  break;  // 前進
    case 's': left = -base;  right = -base;  break;  // 後退
    case 'a': left = -turn;  right =  turn;  break;  // その場左旋回
    case 'd': left =  turn;  right = -turn;  break;  // その場右旋回
    case 'q': left =  inner; right =  base;  break;  // 前進左カーブ
    case 'e': left =  base;  right =  inner; break;  // 前進右カーブ
    case 'z': left = -inner; right = -base;  break;  // 後退左カーブ
    case 'c': left = -base;  right = -inner; break;  // 後退右カーブ
    default:  left =  0;     right =  0;     break;  // 停止
  }

  setTarget(leftMotor, left);
  setTarget(rightMotor, right);
}

// =================================================
// モータ制御
// =================================================

void setupMotor(Motor &m) {
  pwmAttach(m.in1Pin, m.in1Channel);
  pwmAttach(m.in2Pin, m.in2Channel);
  writeMotor(m, 0);
}

void setTarget(Motor &m, int duty) {
  m.target = constrain((int)(duty * m.gain), -PWM_MAX, PWM_MAX);
}

// 目標速度に向けて現在の速度を少しずつ近づける
void updateMotors() {
  static unsigned long lastRampMillis = 0;
  if (millis() - lastRampMillis < RAMP_INTERVAL_MS) {
    return;
  }
  lastRampMillis = millis();

  stepMotor(leftMotor);
  stepMotor(rightMotor);
}

void stepMotor(Motor &m) {
  if (m.current == m.target) {
    return;
  }
  if (m.current < m.target) {
    m.current = min(m.current + RAMP_STEP, m.target);
  } else {
    m.current = max(m.current - RAMP_STEP, m.target);
  }
  writeMotor(m, m.current);
}

// duty: 正なら正転、負なら逆転、0なら停止
void writeMotor(Motor &m, int duty) {
  if (duty > 0) {
    pwmWrite(m.in2Pin, m.in2Channel, 0);
    pwmWrite(m.in1Pin, m.in1Channel, duty);
  } else if (duty < 0) {
    pwmWrite(m.in1Pin, m.in1Channel, 0);
    pwmWrite(m.in2Pin, m.in2Channel, -duty);
  } else {
    pwmWrite(m.in1Pin, m.in1Channel, 0);
    pwmWrite(m.in2Pin, m.in2Channel, 0);
  }
}
