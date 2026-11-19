// ============================================================
// HỆ THỐNG CẢNH BÁO RÒ RỈ GAS VÀ CHÁY THÔNG MINH
// Giai đoạn 2 - ESP32 + MQTT + Node-RED Dashboard
// ============================================================
//
// Nguyên tắc an toàn:
//  - Báo động tại chỗ (LED, còi, van, quạt) KHÔNG phụ thuộc WiFi/MQTT.
//  - Khi đã vào NGUY HIỂM hoặc CHÁY, hệ thống bị KHÓA (van đóng) cho tới
//    khi người dùng bấm RESET (nút vật lý hoặc nút trên Node-RED).
//  - Chỉ cho phép RESET khi cảm biến đã về mức BÌNH THƯỜNG.
//  - Ngưỡng có vùng trễ (hysteresis) để trạng thái không nhảy qua lại.
//  - Cảm biến nhiệt lỗi → coi là CẢNH BÁO (fail-safe), không coi là an toàn.

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ---- WiFi (Wokwi dùng mạng ảo) ----
#define WIFI_SSID     "Wokwi-GUEST"
#define WIFI_PASSWORD ""

// ---- MQTT Broker ----
#define MQTT_BROKER        "broker.hivemq.com"
#define MQTT_PORT          1883
#define MQTT_TOPIC         "gas-fire-alarm/sensors"  // dữ liệu cảm biến
#define MQTT_TOPIC_CMD     "gas-fire-alarm/cmd"      // lệnh từ dashboard (RESET)
#define MQTT_TOPIC_STATUS  "gas-fire-alarm/status"   // ONLINE / OFFLINE (LWT, retained)
#define MQTT_TOPIC_EVENT   "gas-fire-alarm/event"    // phản hồi lệnh RESET

// ---- PIN DEFINITIONS ----
#define MQ2_PIN       34   // Analog (ESP32)
#define DHT_PIN       15
#define FLAME_PIN     4    // Nút nhấn mô phỏng lửa (LOW = có lửa)
#define RESET_PIN     18   // Nút RESET mở khóa van (LOW = nhấn)

#define LED_GREEN     25
#define LED_YELLOW    26
#define LED_RED       27
#define BUZZER_PIN    14
#define SERVO_PIN     13
#define DCMOTOR_PIN   12

// ---- MQ-2: đổi giá trị ADC → ppm ----
// Mô hình cảm biến dòng MQ:  Rs/RL = MQ2_A * ppm^MQ2_B,  với  Rs/RL = ADC_MAX/adc - 1
// MQ2_A, MQ2_B hiệu chuẩn từ 12 điểm đo trên cảm biến Wokwi (0.1 → 100000 ppm),
// sai số < 5%. Dùng ADC 12-bit để đủ độ phân giải ở vùng 1000–10000 ppm.
#define ADC_BITS      12
#define ADC_MAX       4095.0
#define MQ2_A         1.4683
#define MQ2_B         -0.4063
#define GAS_PPM_MAX   100000

// ---- NGƯỠNG GAS (ppm) ----
// LPG có giới hạn cháy nổ dưới (LEL) ≈ 20000 ppm. Máy dò gas thường
// cảnh báo ở ~10% LEL và báo động ở ~25% LEL.
#define GAS_WARNING   2000   // ~10% LEL
#define GAS_DANGER    5000   // ~25% LEL
#define GAS_HYST      200    // gas phải xuống dưới ngưỡng - 200 ppm mới hạ mức
#define TEMP_WARNING  40.0
#define TEMP_DANGER   50.0
#define TEMP_HYST     2.0    // nhiệt phải xuống dưới ngưỡng - 2°C mới hạ mức

// ---- THỜI GIAN (ms) ----
#define TICK_MS          500    // chu kỳ đọc cảm biến / cập nhật
#define PUBLISH_MS       2000   // gửi MQTT định kỳ
#define WIFI_RETRY_MS    10000
#define MQTT_RETRY_MS    5000
#define DHT_TIMEOUT_MS   5000   // không đọc được DHT trong 5s → lỗi cảm biến
#define DEBOUNCE_MS      50
#define LCD_NOTICE_MS    2500

// ---- VAN / QUẠT ----
#define VALVE_OPEN    0
#define VALVE_CLOSED  90
#define FAN_OFF       0
#define FAN_SLOW      100    // ~40%: khi demo trên Wokwi phân biệt rõ với FAN_FAST
#define FAN_FAST      255

// ---- KHỞI TẠO ----
LiquidCrystal_I2C lcd(0x27, 16, 2);
DHT dht(DHT_PIN, DHT22);
Servo gasValve;
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
String mqttClientId;

// ---- BIẾN ----
int   gasRaw        = 0;      // giá trị ADC đọc từ MQ-2
long  gasPpm        = 0;      // nồng độ gas đã quy đổi (ppm)
float temperature   = 0;      // giá trị hợp lệ gần nhất
bool  flameDetected = false;
bool  dhtError      = false;
unsigned long lastDhtOk = 0;

enum SystemState { NORMAL = 0, WARNING = 1, DANGER = 2, FIRE = 3 };
SystemState rawState     = NORMAL;   // trạng thái tính từ cảm biến lúc này
SystemState latchedState = NORMAL;   // mức nguy hiểm cao nhất đang bị khóa
SystemState currentState = NORMAL;   // trạng thái thực thi = max(raw, latched)
SystemState lastPublishedState = NORMAL;
int gasLevel  = 0;                   // 0/1/2 có hysteresis
int tempLevel = 0;

bool valveClosed = false;
int  fanSpeed    = FAN_OFF;
int  currentToneFreq = 0;

unsigned long lastTick        = 0;
unsigned long lastMqttTime    = 0;
unsigned long lastWifiAttempt = 0;
unsigned long lastMqttAttempt = 0;
bool forcePublish = false;

volatile bool resetRequested = false;
const char*   resetSource    = "";

char lcdNotice[17] = "";
unsigned long lcdNoticeUntil = 0;

// ============================================================
void setup() {
  Serial.begin(115200);

  pinMode(LED_GREEN,   OUTPUT);
  pinMode(LED_YELLOW,  OUTPUT);
  pinMode(LED_RED,     OUTPUT);
  pinMode(BUZZER_PIN,  OUTPUT);
  pinMode(DCMOTOR_PIN, OUTPUT);
  pinMode(FLAME_PIN,   INPUT_PULLUP);
  pinMode(RESET_PIN,   INPUT_PULLUP);

  analogReadResolution(ADC_BITS);   // 0–4095

  gasValve.attach(SERVO_PIN);
  gasValve.write(VALVE_OPEN);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Khoi dong...");

  dht.begin();
  lastDhtOk = millis();

  // Client ID ngẫu nhiên: tránh trùng ID với thiết bị khác trên broker public
  // (Wokwi cho mọi người dùng cùng một địa chỉ MAC nên không dùng MAC được).
  mqttClientId = "esp32-gasalarm-" + String(random(0x7FFFFFFF), HEX);

  // Bắt đầu kết nối WiFi nhưng KHÔNG chờ: phần báo động tại chỗ
  // phải chạy ngay cả khi chưa có / mất mạng.
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWifiAttempt = millis();
  Serial.println("Dang ket noi WiFi (khong chan)...");

  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);
  mqtt.setSocketTimeout(3);
  mqtt.setBufferSize(512);

  lcd.clear();
}

// ============================================================
// loop() chạy liên tục (~10ms/vòng) để không bỏ sót nút nhấn và lệnh MQTT;
// phần đọc cảm biến chạy theo chu kỳ TICK_MS.
void loop() {
  maintainConnection();
  pollResetButton();

  if (resetRequested) {
    resetRequested = false;
    handleReset(resetSource);
  }

  unsigned long now = millis();
  if (now - lastTick >= TICK_MS) {
    lastTick = now;
    tick();
  }

  updateIndicators();   // nhấp nháy LED / beep còi theo millis()
  delay(10);
}

void tick() {
  readSensors();
  updateState();
  applyOutputs();
  updateLCD();
  printSerial();

  // Gửi ngay khi trạng thái đổi, ngoài ra gửi định kỳ mỗi PUBLISH_MS
  unsigned long now = millis();
  if (mqtt.connected() &&
      (forcePublish || currentState != lastPublishedState || now - lastMqttTime >= PUBLISH_MS)) {
    lastMqttTime = now;
    forcePublish = false;
    lastPublishedState = currentState;
    publishMQTT();
  }
}

// ============================================================
float adcToPpm(int adc) {
  if (adc <= 0)       return 0;
  if (adc >= ADC_MAX) return GAS_PPM_MAX;
  float ratio = ADC_MAX / adc - 1.0;              // Rs/RL
  float ppm   = pow(ratio / MQ2_A, 1.0 / MQ2_B);
  return (ppm > GAS_PPM_MAX) ? GAS_PPM_MAX : ppm;
}

void readSensors() {
  gasRaw        = analogRead(MQ2_PIN);
  gasPpm        = lround(adcToPpm(gasRaw));
  flameDetected = (digitalRead(FLAME_PIN) == LOW);

  float t = dht.readTemperature();
  if (!isnan(t)) {
    temperature = t;              // chỉ cập nhật khi đọc hợp lệ
    lastDhtOk   = millis();
  }
  dhtError = (millis() - lastDhtOk > DHT_TIMEOUT_MS);
}

// Trả về mức 0/1/2 có vùng trễ: muốn hạ mức thì giá trị phải xuống
// dưới (ngưỡng - hyst), tránh nhảy qua lại khi giá trị dao động quanh ngưỡng.
int levelWithHysteresis(float v, float warn, float danger, float hyst, int prev) {
  int lvl = (v >= danger) ? 2 : (v >= warn) ? 1 : 0;
  if (prev == 2 && lvl < 2 && v > danger - hyst) lvl = 2;
  if (prev >= 1 && lvl < 1 && v > warn - hyst)   lvl = 1;
  return lvl;
}

void updateState() {
  gasLevel = levelWithHysteresis(gasPpm, GAS_WARNING, GAS_DANGER, GAS_HYST, gasLevel);

  if (dhtError) {
    tempLevel = max(tempLevel, 1);   // cảm biến lỗi → ít nhất CẢNH BÁO
  } else {
    tempLevel = levelWithHysteresis(temperature, TEMP_WARNING, TEMP_DANGER, TEMP_HYST, tempLevel);
  }

  if (flameDetected) rawState = FIRE;
  else               rawState = (SystemState)max(gasLevel, tempLevel);

  // Khóa: NGUY HIỂM / CHÁY giữ nguyên cho tới khi RESET
  if (rawState >= DANGER && rawState > latchedState) {
    latchedState = rawState;
    Serial.println(">>> KHOA HE THONG: van gas dong, cho RESET");
  }

  currentState = (SystemState)max((int)rawState, (int)latchedState);
}

// ============================================================
void pollResetButton() {
  static bool lastReading = HIGH;
  static bool stableState = HIGH;
  static unsigned long lastChange = 0;

  bool reading = digitalRead(RESET_PIN);
  if (reading != lastReading) {
    lastReading = reading;
    lastChange  = millis();
  }
  if (millis() - lastChange > DEBOUNCE_MS && reading != stableState) {
    stableState = reading;
    if (stableState == LOW) {
      resetRequested = true;
      resetSource    = "NUT NHAN";
    }
  }
}

void handleReset(const char* source) {
  Serial.print("Yeu cau RESET tu "); Serial.println(source);

  if (latchedState == NORMAL) {
    showNotice("KHONG BI KHOA   ");
    publishEvent("RESET_KHONG_CAN");
    return;
  }
  if (rawState != NORMAL) {
    // Chưa an toàn: giữ khóa
    showNotice("CHUA AN TOAN!   ");
    publishEvent("RESET_TU_CHOI");
    Serial.println("RESET bi tu choi: cam bien chua ve muc binh thuong");
    return;
  }

  latchedState = NORMAL;
  showNotice("DA RESET-MO VAN ");
  publishEvent("RESET_OK");
  Serial.println("RESET thanh cong: mo khoa van gas");
  lastTick = 0;          // cập nhật ngay ở vòng loop kế tiếp
  forcePublish = true;
}

void showNotice(const char* text) {
  strncpy(lcdNotice, text, 16);
  lcdNotice[16] = '\0';
  lcdNoticeUntil = millis() + LCD_NOTICE_MS;
}

// ============================================================
void maintainConnection() {
  // Mỗi lần gọi chỉ thử kết nối lại 1 lần rồi trả về ngay,
  // không dùng vòng while nên loop() không bao giờ bị kẹt.
  unsigned long now = millis();
  static bool wifiWasConnected = false;

  if (WiFi.status() != WL_CONNECTED) {
    if (wifiWasConnected) {
      Serial.println("Mat ket noi WiFi!");
      wifiWasConnected = false;
    }
    if (now - lastWifiAttempt >= WIFI_RETRY_MS) {
      lastWifiAttempt = now;
      Serial.println("Thu ket noi lai WiFi...");
      WiFi.reconnect();
    }
    return;
  }

  if (!wifiWasConnected) {
    wifiWasConnected = true;
    Serial.println("WiFi connected: " + WiFi.localIP().toString());
  }

  if (!mqtt.connected()) {
    if (now - lastMqttAttempt >= MQTT_RETRY_MS) {
      lastMqttAttempt = now;
      Serial.print("Connecting MQTT as "); Serial.print(mqttClientId); Serial.print("...");
      // LWT: nếu ESP32 mất kết nối đột ngột, broker tự gửi "OFFLINE"
      if (mqtt.connect(mqttClientId.c_str(), MQTT_TOPIC_STATUS, 1, true, "OFFLINE")) {
        Serial.println("connected");
        mqtt.publish(MQTT_TOPIC_STATUS, "ONLINE", true);
        mqtt.subscribe(MQTT_TOPIC_CMD);
        forcePublish = true;
      } else {
        Serial.print("failed rc=");
        Serial.println(mqtt.state());
      }
    }
    return;
  }

  mqtt.loop();
}

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();
  msg.toUpperCase();

  if (strcmp(topic, MQTT_TOPIC_CMD) == 0 && msg == "RESET") {
    // Xử lý trong loop(), không xử lý trong callback
    resetRequested = true;
    resetSource    = "MQTT";
  }
}

const char* stateName(SystemState s) {
  switch (s) {
    case NORMAL:  return "BINH THUONG";
    case WARNING: return "CANH BAO";
    case DANGER:  return "NGUY HIEM";
    case FIRE:    return "CHAY";
  }
  return "?";
}

const char* fanName() {
  if (fanSpeed == FAN_OFF)  return "TAT";
  if (fanSpeed == FAN_SLOW) return "CHAM";
  return "NHANH";
}

void publishMQTT() {
  StaticJsonDocument<256> doc;
  doc["source"]   = "esp32";
  doc["gas"]      = gasPpm;     // ppm
  doc["gasRaw"]   = gasRaw;     // ADC 12-bit (để kiểm tra / hiệu chuẩn)
  doc["temp"]     = round(temperature * 10) / 10.0;
  doc["dhtError"] = dhtError;
  doc["flame"]    = flameDetected ? "CO LUA" : "KHONG";
  doc["state"]    = stateName(currentState);
  doc["valve"]    = valveClosed ? "DONG" : "MO";
  doc["fan"]      = fanName();
  doc["latched"]  = (latchedState != NORMAL);

  char buf[256];
  serializeJson(doc, buf);
  mqtt.publish(MQTT_TOPIC, buf);
  Serial.println("Published: " + String(buf));
}

void publishEvent(const char* event) {
  if (mqtt.connected()) mqtt.publish(MQTT_TOPIC_EVENT, event);
}

// ============================================================
// Đặt các ngõ ra "tĩnh" theo trạng thái. Phần nhấp nháy / beep
// nằm trong updateIndicators() để chạy đều theo millis().
void applyOutputs() {
  digitalWrite(LED_GREEN,  currentState == NORMAL  ? HIGH : LOW);
  digitalWrite(LED_YELLOW, currentState == WARNING ? HIGH : LOW);
  if (currentState != FIRE) digitalWrite(LED_RED, currentState == DANGER ? HIGH : LOW);

  switch (currentState) {
    case NORMAL:  valveClosed = false; fanSpeed = FAN_OFF;  break;
    case WARNING: valveClosed = false; fanSpeed = FAN_SLOW; break;
    case DANGER:  valveClosed = true;  fanSpeed = FAN_FAST; break;
    case FIRE:    valveClosed = true;  fanSpeed = FAN_FAST; break;
  }
  gasValve.write(valveClosed ? VALVE_CLOSED : VALVE_OPEN);
  analogWrite(DCMOTOR_PIN, fanSpeed);
}

void updateIndicators() {
  unsigned long now = millis();
  switch (currentState) {
    case NORMAL:
      setBuzzer(0);
      break;
    case WARNING:
      setBuzzer(((now / 500) % 2 == 0) ? 800 : 0);   // beep chậm
      break;
    case DANGER:
      setBuzzer(1000);                                // hú liên tục
      break;
    case FIRE:
      setBuzzer(2000);
      digitalWrite(LED_RED, ((now / 300) % 2 == 0) ? HIGH : LOW);  // nhấp nháy
      break;
  }
}

// Chỉ gọi tone()/noTone() khi tần số thay đổi
void setBuzzer(int freq) {
  if (freq == currentToneFreq) return;
  currentToneFreq = freq;
  if (freq > 0) tone(BUZZER_PIN, freq);
  else          noTone(BUZZER_PIN);
}

// ============================================================
// Ghi đè đủ 16 ký tự mỗi dòng thay vì lcd.clear() để màn hình không chớp
void updateLCD() {
  char line1[17];
  char line2[17];

  // G = ppm (tối đa 6 chữ số)
  if (dhtError) snprintf(line1, sizeof(line1), "G:%-6ld T:--C  ", gasPpm);
  else          snprintf(line1, sizeof(line1), "G:%-6ld T:%-3dC ", gasPpm, (int)temperature);

  if (millis() < lcdNoticeUntil) {
    snprintf(line2, sizeof(line2), "%-16s", lcdNotice);
  } else if (latchedState != NORMAL && rawState == NORMAL) {
    snprintf(line2, sizeof(line2), "%-16s", "AN TOAN-BAM RST");
  } else if (currentState == WARNING && dhtError && gasLevel == 0) {
    snprintf(line2, sizeof(line2), "%-16s", "LOI CAM BIEN DHT");
  } else {
    switch (currentState) {
      case NORMAL:  snprintf(line2, sizeof(line2), "%-16s", "BINH THUONG");      break;
      case WARNING: snprintf(line2, sizeof(line2), "%-16s", "CANH BAO!");        break;
      case DANGER:  snprintf(line2, sizeof(line2), "%-16s", "NGUY HIEM!!!");     break;
      case FIRE:    snprintf(line2, sizeof(line2), "%-16s", "*** CHAY !!! ***"); break;
    }
  }

  lcd.setCursor(0, 0); lcd.print(line1);
  lcd.setCursor(0, 1); lcd.print(line2);
}

void printSerial() {
  Serial.print("Gas: "); Serial.print(gasPpm);
  Serial.print(" ppm (ADC "); Serial.print(gasRaw); Serial.print(")");
  Serial.print(" | Temp: ");
  if (dhtError) Serial.print("LOI"); else Serial.print(temperature);
  Serial.print("C | Flame: "); Serial.print(flameDetected ? "CO" : "KHONG");
  Serial.print(" | State: ");  Serial.print(stateName(currentState));
  if (latchedState != NORMAL) Serial.print(" [KHOA]");
  Serial.println();
}
