// ============================================================
// HỆ THỐNG CẢNH BÁO RÒ RỈ GAS VÀ CHÁY THÔNG MINH
// Giai đoạn 1 - Mô phỏng Wokwi + Arduino Uno
// ============================================================
//
// - NGUY HIỂM / CHÁY → khóa van gas cho tới khi bấm nút RESET.
// - Chỉ cho phép RESET khi cảm biến đã về mức BÌNH THƯỜNG.
// - Ngưỡng có vùng trễ (hysteresis) để trạng thái không nhảy qua lại.
// - Cảm biến nhiệt lỗi → coi là CẢNH BÁO (fail-safe).

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <Servo.h>

// ---- PIN DEFINITIONS ----
// Lưu ý: thư viện Servo trên Uno dùng Timer1 nên analogWrite() ở chân 9, 10
// KHÔNG hoạt động. Quạt (cần PWM) đặt ở chân 6; chân 9 chỉ dùng bật/tắt LED.
#define MQ2_PIN       A0   // Cảm biến gas (Analog)
#define DHT_PIN       2    // Cảm biến nhiệt độ
#define FLAME_PIN     3    // Cảm biến lửa (Digital, LOW = có lửa)
#define RESET_PIN     12   // Nút RESET mở khóa van (LOW = nhấn)

#define LED_GREEN     4
#define LED_YELLOW    5
#define LED_RED       9
#define BUZZER_PIN    7
#define SERVO_PIN     8
#define DCMOTOR_PIN   6    // PWM (Timer0) điều khiển quạt

// ---- NGƯỠNG CẢM BIẾN ----
// MQ-2: đổi ADC → ppm theo mô hình  Rs/RL = MQ2_A * ppm^MQ2_B,  Rs/RL = ADC_MAX/adc - 1
// (hiệu chuẩn từ 12 điểm đo trên cảm biến Wokwi, sai số < 5%)
#define ADC_MAX       1023.0
#define MQ2_A         1.4683
#define MQ2_B         -0.4063
#define GAS_PPM_MAX   100000

#define GAS_WARNING   2000 // ppm, ~10% giới hạn cháy nổ dưới (LEL) của LPG
#define GAS_DANGER    5000 // ppm, ~25% LEL
#define GAS_HYST      200  // ppm
#define TEMP_WARNING  40.0 // Nhiệt độ cảnh báo (°C)
#define TEMP_DANGER   50.0 // Nhiệt độ nguy hiểm (°C)
#define TEMP_HYST     2.0

// ---- THỜI GIAN (ms) ----
#define TICK_MS         500
#define DHT_TIMEOUT_MS  5000
#define DEBOUNCE_MS     50
#define LCD_NOTICE_MS   2500

// ---- VAN / QUẠT ----
#define VALVE_OPEN    0
#define VALVE_CLOSED  90
#define FAN_OFF       0
#define FAN_SLOW      100    // ~40%: khi demo trên Wokwi phân biệt rõ với FAN_FAST
#define FAN_FAST      255

// ---- KHỞI TẠO THƯ VIỆN ----
LiquidCrystal_I2C lcd(0x27, 16, 2); // Địa chỉ I2C thường là 0x27
DHT dht(DHT_PIN, DHT22);
Servo gasValve;

// ---- BIẾN TRẠNG THÁI ----
int   gasRaw        = 0;   // ADC 10-bit
long  gasPpm        = 0;   // ppm
float temperature   = 0;   // giá trị hợp lệ gần nhất
bool  flameDetected = false;
bool  dhtError      = false;
unsigned long lastDhtOk = 0;

enum SystemState { NORMAL = 0, WARNING = 1, DANGER = 2, FIRE = 3 };
SystemState rawState     = NORMAL;
SystemState latchedState = NORMAL;
SystemState currentState = NORMAL;
int gasLevel  = 0;
int tempLevel = 0;

int currentToneFreq = 0;
unsigned long lastTick = 0;

char lcdNotice[17] = "";
unsigned long lcdNoticeUntil = 0;

// ============================================================
void setup() {
  Serial.begin(9600);

  pinMode(LED_GREEN,   OUTPUT);
  pinMode(LED_YELLOW,  OUTPUT);
  pinMode(LED_RED,     OUTPUT);
  pinMode(BUZZER_PIN,  OUTPUT);
  pinMode(DCMOTOR_PIN, OUTPUT);
  pinMode(FLAME_PIN,   INPUT_PULLUP); // Nút nhấn: nhấn = LOW = có lửa
  pinMode(RESET_PIN,   INPUT_PULLUP);

  gasValve.attach(SERVO_PIN);
  gasValve.write(VALVE_OPEN);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("He thong khoi");
  lcd.setCursor(0, 1);
  lcd.print("dong...");

  dht.begin();

  delay(2000);
  lastDhtOk = millis();
  lcd.clear();
}

// ============================================================
void loop() {
  pollResetButton();

  unsigned long now = millis();
  if (now - lastTick >= TICK_MS) {
    lastTick = now;
    readSensors();
    updateState();
    applyOutputs();
    updateLCD();
    printSerial();
  }

  updateIndicators();
  delay(10);
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
    temperature = t;
    lastDhtOk   = millis();
  }
  dhtError = (millis() - lastDhtOk > DHT_TIMEOUT_MS);
}

int levelWithHysteresis(float v, float warn, float danger, float hyst, int prev) {
  int lvl = (v >= danger) ? 2 : (v >= warn) ? 1 : 0;
  if (prev == 2 && lvl < 2 && v > danger - hyst) lvl = 2;
  if (prev >= 1 && lvl < 1 && v > warn - hyst)   lvl = 1;
  return lvl;
}

void updateState() {
  gasLevel = levelWithHysteresis(gasPpm, GAS_WARNING, GAS_DANGER, GAS_HYST, gasLevel);

  if (dhtError) tempLevel = max(tempLevel, 1);
  else          tempLevel = levelWithHysteresis(temperature, TEMP_WARNING, TEMP_DANGER, TEMP_HYST, tempLevel);

  if (flameDetected) rawState = FIRE;
  else               rawState = (SystemState)max(gasLevel, tempLevel);

  if (rawState >= DANGER && rawState > latchedState) {
    latchedState = rawState;
    Serial.println(F(">>> KHOA HE THONG: van gas dong, cho RESET"));
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
    if (stableState == LOW) handleReset();
  }
}

void handleReset() {
  if (latchedState == NORMAL) {
    showNotice("KHONG BI KHOA   ");
    return;
  }
  if (rawState != NORMAL) {
    showNotice("CHUA AN TOAN!   ");
    Serial.println(F("RESET bi tu choi: cam bien chua ve muc binh thuong"));
    return;
  }
  latchedState = NORMAL;
  showNotice("DA RESET-MO VAN ");
  Serial.println(F("RESET thanh cong: mo khoa van gas"));
  lastTick = 0;   // cập nhật ngay
}

void showNotice(const char* text) {
  strncpy(lcdNotice, text, 16);
  lcdNotice[16] = '\0';
  lcdNoticeUntil = millis() + LCD_NOTICE_MS;
}

// ============================================================
void applyOutputs() {
  digitalWrite(LED_GREEN,  currentState == NORMAL  ? HIGH : LOW);
  digitalWrite(LED_YELLOW, currentState == WARNING ? HIGH : LOW);
  if (currentState != FIRE) digitalWrite(LED_RED, currentState == DANGER ? HIGH : LOW);

  switch (currentState) {
    case NORMAL:  gasValve.write(VALVE_OPEN);   analogWrite(DCMOTOR_PIN, FAN_OFF);  break;
    case WARNING: gasValve.write(VALVE_OPEN);   analogWrite(DCMOTOR_PIN, FAN_SLOW); break;
    case DANGER:  gasValve.write(VALVE_CLOSED); analogWrite(DCMOTOR_PIN, FAN_FAST); break;
    case FIRE:    gasValve.write(VALVE_CLOSED); analogWrite(DCMOTOR_PIN, FAN_FAST); break;
  }
}

void updateIndicators() {
  unsigned long now = millis();
  switch (currentState) {
    case NORMAL:  setBuzzer(0); break;
    case WARNING: setBuzzer(((now / 500) % 2 == 0) ? 800 : 0); break;  // beep chậm
    case DANGER:  setBuzzer(1000); break;                              // hú liên tục
    case FIRE:
      setBuzzer(2000);
      digitalWrite(LED_RED, ((now / 300) % 2 == 0) ? HIGH : LOW);       // nhấp nháy
      break;
  }
}

void setBuzzer(int freq) {
  if (freq == currentToneFreq) return;
  currentToneFreq = freq;
  if (freq > 0) tone(BUZZER_PIN, freq);
  else          noTone(BUZZER_PIN);
}

// ============================================================
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
  Serial.print(F("Gas: ")); Serial.print(gasPpm);
  Serial.print(F(" ppm (ADC ")); Serial.print(gasRaw); Serial.print(F(")"));
  Serial.print(F(" | Temp: "));
  if (dhtError) Serial.print(F("LOI")); else Serial.print(temperature);
  Serial.print(F("C | Flame: ")); Serial.print(flameDetected ? "CO" : "KHONG");
  Serial.print(F(" | State: "));
  switch (currentState) {
    case NORMAL:  Serial.print(F("BINH THUONG")); break;
    case WARNING: Serial.print(F("CANH BAO"));    break;
    case DANGER:  Serial.print(F("NGUY HIEM"));   break;
    case FIRE:    Serial.print(F("CHAY"));        break;
  }
  if (latchedState != NORMAL) Serial.print(F(" [KHOA]"));
  Serial.println();
}
