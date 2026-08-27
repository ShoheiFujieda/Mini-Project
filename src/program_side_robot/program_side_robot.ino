// ESP32側
#include <WiFi.h>

const char *ssid = "ESP32_AP";
const char *password = "1234";

WiFiServer server(8000);
WiFiClient client;

// モータ制御ピン
const int LEFT_IN1 = 26;
const int LEFT_IN2 = 27;
const int RIGHT_IN1 = 32;
const int RIGHT_IN2 = 33;

// タイムアウト設定
const unsigned long COMMAND_TIMEOUT_MS = 1000; // 最後の受信からのタイムアウト時間
unsigned long lastCommandMillis = 0; // 最後にコマンドを受信した時刻
char currentCommand = 'x'; // 現在の走行状態（デフォルトで停止中）

void setup() {
  Serial.begin(115200);

  pinMode(LEFT_IN1, OUTPUT);
  pinMode(LEFT_IN2, OUTPUT);
  pinMode(RIGHT_IN1, OUTPUT);
  pinMode(RIGHT_IN2, OUTPUT);
  stopMotors();

  WiFi.softAP(ssid, password);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());   // 通常 192.168.4.1

  server.begin();
  Serial.println("TCP server started. Waiting for connection...");
}

void loop() {
  // まだPCと接続されていなければ、新しい接続を探す
  if (!client || !client.connected()) {
    WiFiClient newClient = server.available();
    if (newClient) {
      client = newClient;
      lastCommandMillis = millis();
      Serial.println("[connected] PC client");
    }
  }

  // 接続中なら、届いているバイトを1つずつ処理する
  if (client && client.connected()) {
    while (client.available() > 0) {
      char c = client.read();
      handleCommand(c);
    }
  }

  // 一定時間コマンドが来なければ自動停止
  if (currentCommand != 'x' && (millis() - lastCommandMillis) > COMMAND_TIMEOUT_MS) {
    stopMotors();
    currentCommand = 'x';
    Serial.println("[timeout] auto stop");
  }
}

void handleCommand(char c) {
  // 改行やCRは無視
  if (c == '\n' || c == '\r') {
    return;
  }

  lastCommandMillis = millis();

  switch (c) {
    case 'w': case 'W':
      forward();
      currentCommand = 'w';
      break;
    case 's': case 'S':
      backward();
      currentCommand = 's';
      break;
    case 'a': case 'A':
      turnLeft();
      currentCommand = 'a';
      break;
    case 'd': case 'D':
      turnRight();
      currentCommand = 'd';
      break;
    case 'x': case 'X': case ' ':
      stopMotors();
      currentCommand = 'x';
      break;
    default:
      // 未対応の文字は無視（コマンド継続時間だけは更新済み）
      return;
  }

  Serial.print("[cmd] ");
  Serial.println(c);
}

// ----- 個別モータ制御 -----
void setLeftMotor(bool in1, bool in2) {
  digitalWrite(LEFT_IN1, in1 ? HIGH : LOW);
  digitalWrite(LEFT_IN2, in2 ? HIGH : LOW);
}

void setRightMotor(bool in1, bool in2) {
  digitalWrite(RIGHT_IN1, in1 ? HIGH : LOW);
  digitalWrite(RIGHT_IN2, in2 ? HIGH : LOW);
}

// ----- 走行パターン -----
void forward() {
  setLeftMotor(HIGH, LOW);   // 左モータ 正転
  setRightMotor(HIGH, LOW);  // 右モータ 正転
}

void backward() {
  setLeftMotor(LOW, HIGH);   // 左モータ 逆転
  setRightMotor(LOW, HIGH);  // 右モータ 逆転
}

void turnLeft() {
  // その場旋回: 左モータ逆転、右モータ正転
  setLeftMotor(LOW, HIGH);
  setRightMotor(HIGH, LOW);
}

void turnRight() {
  // その場旋回: 左モータ正転、右モータ逆転
  setLeftMotor(HIGH, LOW);
  setRightMotor(LOW, HIGH);
}

void stopMotors() {
  // 停止（IN1=IN2=LOW）
  setLeftMotor(LOW, LOW);
  setRightMotor(LOW, LOW);
}
