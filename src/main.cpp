// https://github.com/meganetaaan/m5stack-avatar/blob/master/README_ja.md
//
// #include <M5AtomS3.h> // ヘッダーファイル準備（別途FastLEDライブラリをインストール）
// #include <Arduino.h>
#include <M5Unified.h>
#include <Avatar.h>
#include <esp_now.h>
#include <WiFi.h>
#include <FS.h> // 書かないとコンパイルできなかったため記入、機能的には未使用
#include <faces/DogFace.h>
#include "formatString.hpp"     // https://gist.github.com/GOB52/e158b689273569357b04736b78f050d6
#include "Grove_Multi_Switch.h" // 5WaySwitchライブラリ

// esp_now 受信したコマンドを格納する変数
String receivedCommand = "";
// ESP-NOWの送信先のAtomS3のMACアドレスを設定してください
uint8_t remoteMacAddress[] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
bool EspnowEnable = false;

// DRV8835
bool OutputEnable = false;
const uint8_t AIN1 = 5; // A1:PHASE
const uint8_t AIN2 = 6; // A2:ENBL
const uint8_t BIN1 = 7; // B1:PHASE
const uint8_t BIN2 = 8; // B2:ENBL
GroveMultiSwitch mswitch[1];
const char *grove_5way_tactile_keys[] = {
    "KEY A",
    "KEY B",
    "KEY C",
    "KEY D",
    "KEY E",
};
const char *grove_6pos_dip_switch_keys[] = {
    "POS 1",
    "POS 2",
    "POS 3",
    "POS 4",
    "POS 5",
    "POS 6",
};

const char **key_names;
bool KeyEnable = false;

int deviceDetect(void)
{
  if (!mswitch->begin())
  {
    Serial.println("***** Device probe failed *****");
    return -1;
  }

  Serial.println("***** Device probe OK *****");
  if (PID_VAL(mswitch->getDevID()) == PID_5_WAY_TACTILE_SWITCH)
  {
    Serial.println("Grove 5-Way Tactile Switch Inserted!");
    key_names = grove_5way_tactile_keys;
  }
  else if (PID_VAL(mswitch->getDevID()) == PID_6_POS_DIP_SWITCH)
  {
    Serial.println("Grove 6-Position DIP Switch Inserted!");
    key_names = grove_6pos_dip_switch_keys;
  }

  // enable event detection
  mswitch->setEventMode(true);

  // report device model
  Serial.print("A ");
  Serial.print(mswitch->getSwitchCount());
  Serial.print(" Button/Switch Device ");
  Serial.println(mswitch->getDevVer());
  return 0;
}

using namespace m5avatar;

Avatar avatar;
float scale = 0.0f;
int8_t position_x = 0;
int8_t position_y = 0;
uint8_t display_rotation = 3; // ディスプレイの向き(0〜3)
uint8_t display_dif = 2; // grayとatoms3の差
Face *faces[2];
const int facesSize = sizeof(faces) / sizeof(Face *);
int faceIdx = 0;

void face_change(int fno)
{
  switch (fno)
  {
  case 0:avatar.setExpression(Expression::Neutral);break;
  case 1:avatar.setExpression(Expression::Happy);break;
  case 2:avatar.setExpression(Expression::Sleepy);break;
  case 3:avatar.setExpression(Expression::Doubt);break;
  case 4:avatar.setExpression(Expression::Sad);break;
  case 5:avatar.setExpression(Expression::Angry);break;
  default:avatar.setExpression(Expression::Neutral);break;
  }
}

void setup()
{
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  cfg.internal_imu = true; // default=true. use internal IMU.
  M5.begin(cfg);
  // M5.begin();

  //Serial.begin(115200);
  Serial.println("5WaySwitch test program start");
  delay(1000);

  // 送信先のAtomS3の設定
  esp_now_peer_info_t peerInfo;
  // WiFiの初期化
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  // ESP-NOWの初期化
  if (esp_now_init() != 0) {
    Serial.println("ESP-NOW initialization failed");
    EspnowEnable = false;
    //return;
  }
  else {
    Serial.println("ESP-NOW initialization OK");
    EspnowEnable = true;
  }

  // Initial device probe
  if (deviceDetect() < 0) {
    KeyEnable = false;
    Serial.println("Grove 5-Way Tactile Not Detected");
    // Serial.println("Insert Grove 5-Way Tactile");
    // Serial.println("or Grove 6-Position DIP Switch");
    // delay(1000);
    // for (;;);
  }
  else {
    Serial.println("Grove 5-Way Tactile Detected !!!");
    KeyEnable = true;
  }

  switch (M5.getBoard())
  {
  case m5::board_t::board_M5Stack:
    // esp now setting
    if(EspnowEnable){
      memset(&peerInfo, 0, sizeof(peerInfo)); // ←これを追加
      memcpy(peerInfo.peer_addr, remoteMacAddress, 6);
      peerInfo.channel = 0;
      peerInfo.encrypt = false;
      if (esp_now_add_peer(&peerInfo) != 0)
      {
        Serial.println("Failed to add peer");
        EspnowEnable = false;
        //return;
      }
      else{
        Serial.println("add peer OK");
        EspnowEnable = true;
      }
    }

    // avatar setting
    scale = 1.0f;
    position_x = 0;
    position_y = 0;
    display_rotation = 1;
    display_dif = 2; // grayとatoms3の差

    // GPIO output
    OutputEnable = false;

    break;

  case m5::board_t::board_M5AtomS3:
    // check mac address
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    Serial.printf("[Wi-Fi Station] Mac Address = %02X:%02X:%02X:%02X:%02X:%02X\r\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // esp now setting
    if(EspnowEnable){
      esp_now_register_recv_cb([](const uint8_t *mac, const uint8_t *data, int len)
                               {
          // 受信したデータを文字列に変換して格納
          receivedCommand = String((const char*)data);
          Serial.println("cb Received: " + receivedCommand); });
    }
    // ESP-NOWの受信コールバック関数を設定

    // avatar setting
    //scale = 0.55f;
    //position_x = -60;
    //position_y = -95;
    scale = 0.5f;
    position_x = 0;
    position_y = -10;
    display_rotation = 3;
    display_dif = 0; // grayとatoms3の差

    // GPIO output
    OutputEnable = true;
    // pin setting
    pinMode(AIN1, OUTPUT);
    pinMode(AIN2, OUTPUT);
    pinMode(BIN1, OUTPUT);
    pinMode(BIN2, OUTPUT);

    // pwm
    ledcSetup(0, 2000, 8);  // CH,Hz,bit
    ledcSetup(1, 2000, 8);  // CH,Hz,bit
    ledcSetup(2, 2000, 8);  // CH,Hz,bit
    ledcSetup(3, 2000, 8);  // CH,Hz,bit
    ledcAttachPin(AIN1, 0); // Pin, CH
    ledcAttachPin(AIN2, 1); // Pin, CH
    ledcAttachPin(BIN1, 2); // Pin, CH
    ledcAttachPin(BIN2, 3); // Pin, CH

    break;

  defalut:
    Serial.println("Invalid board.");
    break;
  }

  faces[0] = avatar.getFace();
  faces[1] = new DogFace();
  M5.Lcd.setRotation(display_rotation);
  avatar.setScale(scale);
  avatar.setPosition(position_x, position_y);
  avatar.setSpeechFont(&fonts::Font4);
  avatar.init(); // 描画を開始します。
  Serial.println("avatar start");
  // Serial.printf("avatar start %d %d %d\r\n", scale, position_x, position_y);

  delay(500);
}

int fno = 1;
uint8_t new_btn = 0;
uint8_t old_btn = 0;
int dtn = 0;
String command = "";

void loop()
{
  M5.update(); // ボタン状態初期化

  if (KeyEnable)
  {
    // 5WaySwitch
    GroveMultiSwitch::ButtonEvent_t *evt;
    delay(1);
    evt = mswitch->getEvent();
    if (!evt)
    {
      Serial.println("5WaySwitch can not getEvent");
    }
    else
    {
      if (evt->event & GroveMultiSwitch::BTN_EV_HAS_EVENT)
      {
        Serial.print("BTN_EV_HAS_EVENT : ");
        if (!(evt->button[0] & GroveMultiSwitch::BTN_EV_RAW_STATUS))     {new_btn = 1;Serial.println("Btn0");}
        else if (!(evt->button[1] & GroveMultiSwitch::BTN_EV_RAW_STATUS)){new_btn = 2;Serial.println("Btn1");}
        else if (!(evt->button[2] & GroveMultiSwitch::BTN_EV_RAW_STATUS)){new_btn = 3;Serial.println("Btn2");}
        else if (!(evt->button[3] & GroveMultiSwitch::BTN_EV_RAW_STATUS)){new_btn = 4;Serial.println("Btn3");}
        else if (!(evt->button[4] & GroveMultiSwitch::BTN_EV_RAW_STATUS)){new_btn = 5;Serial.println("Btn4");}
        else {new_btn = 0;Serial.println("HOME");}

        if (new_btn != old_btn)
        {
          old_btn = new_btn;
          switch (new_btn)
          {
            case 0: command = "stop" ; break; // 停止
            case 3: command = "go"   ; break; // 前進
            case 4: command = "right"; break; // 右回転
            case 1: command = "back" ; break; // 後退
            case 2: command = "left" ; break; // 左回転
            case 5: command = "pwm"  ; break; // 出力変更
            default:command = "stop" ; break;
          }
        }
        else
        {
          command = "";
        }
      }
      else
      {
        // Serial.println("No event");
        // Serial.print("No event, errno = ");
        // Serial.println(mswitch->errno);
      }
    }
  }

  switch (M5.getBoard())
  {
  case m5::board_t::board_M5Stack:
    if (EspnowEnable & (command != ""))
    {
      // ボタンやセンサーなどから"go"、"back"、"right"、"left"のいずれかのコマンドを取得
      //command = "go"; // 例: ボタンから取得したコマンド
      // コマンドを送信
      esp_now_send(remoteMacAddress, (uint8_t *)command.c_str(), command.length());
      Serial.println("Send Command : " + command);
    }
    break;

  case m5::board_t::board_M5AtomS3:
    if (EspnowEnable & (receivedCommand != "")) {
      //strcpy(command, receivedCommand);
      command = String(receivedCommand);
      Serial.println("Received Command : " + command);
      receivedCommand = "";
    }
    break;

  defalut:
    // Serial.println("Invalid board.");
    break;
  }

  if (command != "") {
    int La1, La2, Lb1, Lb2 = LOW;
    if      (command == "stop" ) {display_rotation = 3;faceIdx = 0;La1 = La2 = Lb1 = Lb2 = LOW;} // 停止
    else if (command == "go"   ) {display_rotation = 1;faceIdx = 1;La1 = HIGH;La2 = LOW;Lb1 = LOW;Lb2 = HIGH;} // 前進
    else if (command == "right") {display_rotation = 0;faceIdx = 1;La1 = HIGH;La2 = LOW;Lb1 = HIGH;Lb2 = LOW;} // 右回転
    else if (command == "back" ) {display_rotation = 3;faceIdx = 1;La1 = LOW;La2 = HIGH;Lb1 = HIGH;Lb2 = LOW;} // 後退
    else if (command == "left" ) {display_rotation = 2;faceIdx = 1;La1 = LOW;La2 = HIGH;Lb1 = LOW;Lb2 = HIGH;} // 左回転
    else if (command == "pwm"  ) {display_rotation = 3;faceIdx = 1;La1 = La2 = Lb1 = Lb2 = LOW; dtn = (dtn + 1) % 3;} // 出力変更
    else {
      display_rotation = 3;faceIdx = 0;La1 = La2 = Lb1 = Lb2 = LOW;
    }

    int dt = 128 + dtn * 64;
    if (OutputEnable){
      Serial.printf("Output : %d, %d, %d, %d\r\n", La1, La2, Lb1, Lb2);
      // digitalWrite(AIN1, La1);
      // digitalWrite(AIN2, La2);
      // digitalWrite(BIN1, Lb1);
      // digitalWrite(BIN2, Lb2);
      ledcWrite(0, dt * La1); // CH,Duty デューティー比0.25(64/256)でPWM制御
      ledcWrite(1, dt * La2); // CH,Duty デューティー比0.25(64/256)でPWM制御
      ledcWrite(2, dt * Lb1); // CH,Duty デューティー比0.25(64/256)でPWM制御
      ledcWrite(3, dt * Lb2); // CH,Duty デューティー比0.25(64/256)でPWM制御
    }

    display_rotation = (display_rotation + display_dif) % 4;
    M5.Lcd.setRotation(display_rotation);
    //avatar.setScale(scale);
    //avatar.setPosition(position_x, position_y);
    //Serial.printf("position %d, %d\r\n", position_x, position_y);
    avatar.setFace(faces[faceIdx]);
    //avatar.init();
    // delay(100);
    if (command == "pwm"){
      std::string s;
      s = formatString("PW:%d", int(dt / 256.0 * 100));
      avatar.setSpeechText(s.c_str());
    }
    else
      avatar.setSpeechText("");

    Serial.printf("display_rotation %d, duty %d\r\n", display_rotation, int(dt / 256.0 * 100));

    command = "";
  }

  // 本体ボタン処理
  if (M5.BtnA.wasPressed())
  { // ボタンが押されていれば
    face_change(fno);
    fno++;
    if (fno > 5)
      fno = 0;
    Serial.println("face change");

    //uint8_t mac[6];
    //esp_read_mac(mac, ESP_MAC_WIFI_STA);
    //Serial.printf("[Wi-Fi Station] Mac Address = %02X:%02X:%02X:%02X:%02X:%02X\r\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    WiFi.mode(WIFI_MODE_STA);
    Serial.println(WiFi.macAddress());
  }

  delay(100);
}
