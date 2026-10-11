// ============================================================
// HỆ THỐNG CẢNH BÁO RÒ RỈ GAS VÀ CHÁY THÔNG MINH
// Giai đoạn 2 - ESP32 + MQTT + Node-RED Dashboard
// ============================================================

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
#define MQTT_BROKER   "broker.hivemq.com"
#define MQTT_PORT     1883
#define MQTT_TOPIC    "gas-fire-alarm/sensors"
#define MQTT_CLIENT   "esp32-gasalarm-001"

// ---- PIN DEFINITIONS ----
#define MQ2_PIN       34   // Analog (ESP32)
#define DHT_PIN       15
#define FLAME_PIN     4    // Nút nhấn mô phỏng lửa

#define LED_GREEN     25
#define LED_YELLOW    26
#define LED_RED       27
#define BUZZER_PIN    14
#define SERVO_PIN     13
#define DCMOTOR_PIN   12

// ---- NGƯỠNG ----
#define GAS_WARNING   300
#define GAS_DANGER    500
#define TEMP_DANGER   50.0

// ---- KHỞI TẠO ----
LiquidCrystal_I2C lcd(0x27, 16, 2);
DHT dht(DHT_PIN, DHT22);
Servo gasValve;
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

// ---- BIẾN ----
int gasValue = 0;
float temperature = 0;
bool flameDetected = false;

enum SystemState { NORMAL, WARNING, DANGER, FIRE };
SystemState currentState = NORMAL;

unsigned long lastBeepTime = 0;
unsigned long lastMqttTime = 0;
bool buzzerOn = false;

// ============================================================
void setup() {
  Serial.begin(115200);

  pinMode(LED_GREEN,   OUTPUT);
  pinMode(LED_YELLOW,  OUTPUT);
  pinMode(LED_RED,     OUTPUT);
  pinMode(BUZZER_PIN,  OUTPUT);
  pinMode(DCMOTOR_PIN, OUTPUT);
  pinMode(FLAME_PIN,   INPUT_PULLUP);

  gasValve.attach(SERVO_PIN);
  gasValve.write(0);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Dang ket noi...");

  dht.begin();

  // Kết nối WiFi
  connectWiFi();

  // Cấu hình MQTT
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);

  lcd.clear();
}

// ============================================================
void loop() {
  // Duy trì kết nối MQTT
  if (!mqtt.connected()) reconnectMQTT();
  mqtt.loop();

  // Đọc cảm biến
  gasValue     = analogRead(MQ2_PIN);
  temperature  = dht.readTemperature();
  flameDetected = (digitalRead(FLAME_PIN) == LOW);

  if (isnan(temperature)) temperature = 0;

  // Xác định trạng thái
  if (flameDetected) {
    currentState = FIRE;
  } else if (gasValue >= GAS_DANGER || temperature >= TEMP_DANGER) {
    currentState = DANGER;
  } else if (gasValue >= GAS_WARNING) {
    currentState = WARNING;
  } else {
    currentState = NORMAL;
  }

  handleState();
  updateLCD();
  printSerial();

  // Gửi MQTT mỗi 2 giây
  if (millis() - lastMqttTime >= 2000) {
    lastMqttTime = millis();
    publishMQTT();
  }

  delay(500);
}

// ============================================================
void connectWiFi() {
  Serial.print("Connecting WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected: " + WiFi.localIP().toString());
}

void reconnectMQTT() {
  while (!mqtt.connected()) {
    Serial.print("Connecting MQTT...");
    if (mqtt.connect(MQTT_CLIENT)) {
      Serial.println("connected");
    } else {
      Serial.print("failed rc=");
      Serial.println(mqtt.state());
      delay(2000);
    }
  }
}

void publishMQTT() {
  StaticJsonDocument<200> doc;
  doc["gas"]   = gasValue;
  doc["temp"]  = temperature;
  doc["flame"] = flameDetected ? "CO LUA" : "KHONG";

  String stateStr;
  switch (currentState) {
    case NORMAL:  stateStr = "BINH THUONG"; break;
    case WARNING: stateStr = "CANH BAO";    break;
    case DANGER:  stateStr = "NGUY HIEM";   break;
    case FIRE:    stateStr = "CHAY";        break;
  }
  doc["state"] = stateStr;

  char buf[200];
  serializeJson(doc, buf);
  mqtt.publish(MQTT_TOPIC, buf);
  Serial.println("Published: " + String(buf));
}

// ============================================================
void handleState() {
  digitalWrite(LED_GREEN,  LOW);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_RED,    LOW);

  switch (currentState) {
    case NORMAL:
      digitalWrite(LED_GREEN, HIGH);
      noTone(BUZZER_PIN);
      gasValve.write(0);
      ledcWrite(0, 0);
      break;
    case WARNING:
      digitalWrite(LED_YELLOW, HIGH);
      beepBuzzer(500);
      gasValve.write(0);
      analogWrite(DCMOTOR_PIN, 200);
      break;
    case DANGER:
      digitalWrite(LED_RED, HIGH);
      tone(BUZZER_PIN, 1000);
      gasValve.write(90);
      analogWrite(DCMOTOR_PIN, 255);
      break;
    case FIRE:
      blinkRedLED();
      tone(BUZZER_PIN, 2000);
      gasValve.write(90);
      analogWrite(DCMOTOR_PIN, 255);
      break;
  }
}

void beepBuzzer(int interval) {
  unsigned long now = millis();
  if (now - lastBeepTime >= interval) {
    lastBeepTime = now;
    buzzerOn = !buzzerOn;
    if (buzzerOn) tone(BUZZER_PIN, 800);
    else          noTone(BUZZER_PIN);
  }
}

void blinkRedLED() {
  if ((millis() / 300) % 2 == 0) digitalWrite(LED_RED, HIGH);
  else                            digitalWrite(LED_RED, LOW);
}

void updateLCD() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("G:");
  lcd.print(gasValue);
  lcd.print(" T:");
  lcd.print((int)temperature);
  lcd.print("C");
  lcd.setCursor(0, 1);
  switch (currentState) {
    case NORMAL:  lcd.print("BINH THUONG     "); break;
    case WARNING: lcd.print("CANH BAO GAS!   "); break;
    case DANGER:  lcd.print("NGUY HIEM!!!    "); break;
    case FIRE:    lcd.print("*** CHAY !!! ***"); break;
  }
}

void printSerial() {
  Serial.print("Gas: "); Serial.print(gasValue);
  Serial.print(" | Temp: "); Serial.print(temperature);
  Serial.print("C | Flame: "); Serial.print(flameDetected ? "CO" : "KHONG");
  Serial.print(" | State: ");
  switch (currentState) {
    case NORMAL:  Serial.println("BINH THUONG"); break;
    case WARNING: Serial.println("CANH BAO");    break;
    case DANGER:  Serial.println("NGUY HIEM");   break;
    case FIRE:    Serial.println("CHAY");         break;
  }
}
