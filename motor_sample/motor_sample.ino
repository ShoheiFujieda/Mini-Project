/*
  二輪ローバー WASDキー操作 (ESP32 + TB67H450モータードライバ x2)
  --------------------------------------------------------------------------
  ・PCとUSBシリアル接続したまま、PC側から送られてくる 'w' 'a' 's' 'd' の1文字コマンドを受信して左右のモータを制御する。
  ・TB67H450FNG を、左モータ用・右モータ用として合計2個使用する想定。
  ・IN1/IN2の組み合わせによる制御方式
        IN1   IN2   動作
        H     L     正転 (Forward)
        L     H     逆転 (Reverse)
        L     L     停止 
        H     H     ブレーキ (ショートブレーキ)

  【配線】
    左モータドライバ  IN1 -> GPIO26 , IN2 -> GPIO27
    右モータドライバ  IN1 -> GPIO32 , IN2 -> GPIO33

  【操作方法】
    w : 前進   s : 後退
    a : その場左旋回   d : その場右旋回
    x or 半角スペース : 停止

  【機能】
    一定時間（COMMAND_TIMEOUT_MS）新しいコマンドを受信しない場合は自動的に停止する（USB切断やPC側の異常停止に備える）。
    PC側は、キーを押し続けている間は同じ文字を一定間隔（100ms毎）で送り続ける。
*/

// ----- ピン設定（配線に合わせて変更） -----
const int LEFT_IN1  = 26;
const int LEFT_IN2  = 27;
const int RIGHT_IN1 = 32;
const int RIGHT_IN2 = 33;

// ----- 安全タイムアウト -----
const unsigned long COMMAND_TIMEOUT_MS = 300; // これを超えて無入力なら自動停止
unsigned long lastCommandMillis = 0; // 最後のコマンド受信からの経過時間

// 現在の走行状態（デバッグ表示用）
char currentCommand = 'x';

void setup() {
  Serial.begin(115200);

  pinMode(LEFT_IN1, OUTPUT);
  pinMode(LEFT_IN2, OUTPUT);
  pinMode(RIGHT_IN1, OUTPUT);
  pinMode(RIGHT_IN2, OUTPUT);

  stopMotors(); // モーター停止

  Serial.println("=== Rover WASD Control Ready ===");
  Serial.println("w:forward s:backward a:turn-left d:turn-right x/space:stop");
}

void loop() {
  // シリアル受信バッファに届いている文字を全部処理する
  while (Serial.available() > 0) {
    char c = Serial.read();
    handleCommand(c);
  }

  // 一定時間コマンドが来なければ安全のため自動停止
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
