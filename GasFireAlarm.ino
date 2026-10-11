// ============================================================
// HỆ THỐNG CẢNH BÁO RÒ RỈ GAS VÀ CHÁY THÔNG MINH
// Giai đoạn 1 - Mô phỏng Wokwi + Arduino Uno
// ============================================================

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <Servo.h>

// ---- PIN DEFINITIONS ----
#define MQ2_PIN       A0   // Cảm biến gas (Analog)
#define DHT_PIN       2    // Cảm biến nhiệt độ
#define FLAME_PIN     3    // Cảm biến lửa (Digital, LOW = có lửa)

#define LED_GREEN     4
#define LED_YELLOW    5
#define LED_RED       6
#define BUZZER_PIN    7
#define SERVO_PIN     8
#define DCMOTOR_PIN   9    // Dùng PWM để điều khiển DC Motor

// ---- NGƯỠNG CẢM BIẾN ----
#define GAS_WARNING   300  // Ngưỡng cảnh báo gas
#define GAS_DANGER    500  // Ngưỡng nguy hiểm gas
#define TEMP_DANGER   50.0 // Nhiệt độ nguy hiểm (°C)

// ---- KHỞI TẠO THƯ VIỆN ----
LiquidCrystal_I2C lcd(0x27, 16, 2); // Địa chỉ I2C thường là 0x27
DHT dht(DHT_PIN, DHT22);
Servo gasValve;

// ---- BIẾN TRẠNG THÁI ----
int gasValue = 0;
float temperature = 0;
bool flameDetected = false;

// Enum trạng thái hệ thống
enum SystemState {
  NORMAL,
  WARNING,
  DANGER,
  FIRE
};
SystemState currentState = NORMAL;

// Biến buzzer beep
unsigned long lastBeepTime = 0;
bool buzzerOn = false;

// ============================================================
void setup() {
  Serial.begin(9600);

  // Cấu hình pin
  pinMode(LED_GREEN,  OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_RED,    OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(DCMOTOR_PIN, OUTPUT);
  pinMode(FLAME_PIN,  INPUT_PULLUP); // Nút nhấn: nhấn = LOW = có lửa

  // Servo - mô phỏng van gas
  gasValve.attach(SERVO_PIN);
  gasValve.write(0); // Van mở = 0 độ

  // LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("He thong khoi");
  lcd.setCursor(0, 1);
  lcd.print("dong...");

  // DHT
  dht.begin();

  delay(2000);
  lcd.clear();
}

// ============================================================
void loop() {
  // 1. ĐỌC CẢM BIẾN
  gasValue     = analogRead(MQ2_PIN);
  temperature  = dht.readTemperature();
  flameDetected = (digitalRead(FLAME_PIN) == LOW); // LOW = có lửa

  // Kiểm tra lỗi DHT
  if (isnan(temperature)) temperature = 0;

  // 2. XÁC ĐỊNH TRẠNG THÁI
  if (flameDetected) {
    currentState = FIRE;
  } else if (gasValue >= GAS_DANGER || temperature >= TEMP_DANGER) {
    currentState = DANGER;
  } else if (gasValue >= GAS_WARNING) {
    currentState = WARNING;
  } else {
    currentState = NORMAL;
  }

  // 3. THỰC HIỆN HÀNH ĐỘNG THEO TRẠNG THÁI
  handleState();

  // 4. HIỂN THỊ LCD
  updateLCD();

  // 5. IN RA SERIAL (để debug)
  printSerial();

  delay(1000); // Đọc mỗi 1 giây
}

// ============================================================
void handleState() {
  // Tắt tất cả LED trước
  digitalWrite(LED_GREEN,  LOW);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_RED,    LOW);

  switch (currentState) {

    case NORMAL:
      digitalWrite(LED_GREEN, HIGH);
      noTone(BUZZER_PIN);
      gasValve.write(0);       // Van mở
      analogWrite(DCMOTOR_PIN, 0); // Quạt tắt
      break;

    case WARNING:
      digitalWrite(LED_YELLOW, HIGH);
      // Buzzer beep theo chu kỳ 500ms
      beepBuzzer(500);
      gasValve.write(0);       // Van vẫn mở
      analogWrite(DCMOTOR_PIN, 200); // Quạt bật mức vừa
      break;

    case DANGER:
      digitalWrite(LED_RED, HIGH);
      tone(BUZZER_PIN, 1000);  // Hú liên tục
      gasValve.write(90);      // Servo đóng van (90 độ)
      analogWrite(DCMOTOR_PIN, 255); // Quạt tốc độ tối đa
      break;

    case FIRE:
      // LED đỏ nhấp nháy
      blinkRedLED();
      tone(BUZZER_PIN, 2000);  // Hú tần số cao
      gasValve.write(90);      // Servo đóng van
      analogWrite(DCMOTOR_PIN, 255);
      break;
  }
}

// ============================================================
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
  // Nhấp nháy theo millis để không block loop
  if ((millis() / 300) % 2 == 0) {
    digitalWrite(LED_RED, HIGH);
  } else {
    digitalWrite(LED_RED, LOW);
  }
}

// ============================================================
void updateLCD() {
  lcd.clear();

  // Dòng 1: Hiển thị giá trị cảm biến
  lcd.setCursor(0, 0);
  lcd.print("G:");
  lcd.print(gasValue);
  lcd.print(" T:");
  lcd.print((int)temperature);
  lcd.print("C");

  // Dòng 2: Hiển thị trạng thái
  lcd.setCursor(0, 1);
  switch (currentState) {
    case NORMAL:  lcd.print("BINH THUONG     "); break;
    case WARNING: lcd.print("CANH BAO GAS!   "); break;
    case DANGER:  lcd.print("NGUY HIEM!!!    "); break;
    case FIRE:    lcd.print("*** CHAY !!! ***"); break;
  }
}

// ============================================================
void printSerial() {
  Serial.print("Gas: ");  Serial.print(gasValue);
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
