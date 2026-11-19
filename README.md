# 🔥 Hệ Thống Cảnh Báo Rò Rỉ Gas và Cháy Thông Minh
**Smart Gas Leak and Fire Alert System**

Hệ thống IoT giám sát nồng độ gas, nhiệt độ và phát hiện lửa theo thời gian thực. Gửi cảnh báo qua Telegram, hiển thị dashboard Node-RED và mô phỏng sa bàn bếp 2D.

---

## 📋 Tính năng

- Đọc cảm biến MQ-2 (gas), DHT22 (nhiệt độ), cảm biến lửa
- 4 mức trạng thái: **BÌNH THƯỜNG → CẢNH BÁO → NGUY HIỂM → CHÁY**
- Gửi dữ liệu lên MQTT broker (HiveMQ)
- Dashboard Node-RED với gauge, biểu đồ, log cảnh báo
- Cảnh báo Telegram tự động khi trạng thái thay đổi
- Sa bàn bếp 2D mô phỏng trực quan
- Bảng điều khiển web để kiểm tra hệ thống

---

## 🗂️ Cấu trúc dự án

```
GasFireAlarm/
├── index.html               # Trang chủ liên kết các công cụ
├── GasFireAlarm.ino         # Giai đoạn 1: Arduino Uno (không có mạng)
├── diagram.json             # Sơ đồ mạch Wokwi cho Uno
├── GasFireAlarm_ESP32.ino   # Giai đoạn 2: ESP32 + MQTT
├── diagram_esp32.json       # Sơ đồ mạch Wokwi cho ESP32
├── libraries.txt            # Thư viện cho Wokwi (bản ESP32) – dán thẳng vào tab libraries.txt
├── nodered-flow.json        # Flow Node-RED (dashboard + Telegram)
├── simulate.js              # Script giả lập dữ liệu cảm biến
├── control.html             # Bảng điều khiển web
└── kitchen.html             # Sa bàn bếp 2D mô phỏng
```

---

## 🔧 Phần cứng (ESP32)

| Linh kiện | Chân ESP32 |
|-----------|-----------|
| Cảm biến MQ-2 (Gas) | GPIO 34 |
| Cảm biến DHT22 (Nhiệt độ) | GPIO 15 |
| Cảm biến lửa / Nút nhấn | GPIO 4 |
| Nút RESET (mở khoá van) | GPIO 18 |
| LED Xanh (Bình thường) | GPIO 25 |
| LED Vàng (Cảnh báo) | GPIO 26 |
| LED Đỏ (Nguy hiểm/Cháy) | GPIO 27 |
| Buzzer | GPIO 14 |
| Servo (Van gas) | GPIO 13 |
| Quạt (LED xanh dương trong Wokwi) | GPIO 12 |
| LCD I2C 16x2 | SDA=21, SCL=22 |

---

## 🚦 Ngưỡng cảnh báo

| Trạng thái | Điều kiện | LED | Buzzer | Van gas |
|------------|-----------|-----|--------|---------|
| ✅ BÌNH THƯỜNG | Gas < 2000 ppm và Nhiệt < 40°C | Xanh | Tắt | Mở |
| ⚠️ CẢNH BÁO | Gas 2000–4999 ppm hoặc Nhiệt 40–49°C, hoặc **lỗi cảm biến nhiệt** | Vàng | Beep chậm | Mở |
| 🚨 NGUY HIỂM | Gas ≥ 5000 ppm hoặc Nhiệt ≥ 50°C | Đỏ | Liên tục | **Đóng và khoá** |
| 🔥 CHÁY | Phát hiện lửa | Đỏ nhấp nháy | Báo động | **Đóng và khoá** |

**Vì sao 2000 / 5000 ppm:** LPG có giới hạn cháy nổ dưới (LEL) khoảng 20.000 ppm. Máy dò gas thường cảnh báo ở ~10% LEL và báo động ở ~25% LEL. Không khí bình thường trong Wokwi (mặc định 400 ppm) nằm ở mức BÌNH THƯỜNG.

**Quy đổi ADC → ppm:** MQ-2 cho ra điện áp, firmware đổi sang ppm theo mô hình chuẩn của cảm biến dòng MQ:

```
Rs/RL = ADC_MAX / ADC − 1        (điện trở cảm biến / điện trở tải)
Rs/RL = A · ppm^B   →   ppm = (Rs/RL ÷ A)^(1/B)
A = 1.4683,  B = −0.4063
```

Hai hệ số A, B được hiệu chuẩn bằng hồi quy log–log trên 12 điểm đo của cảm biến Wokwi (0.1 → 100.000 ppm), sai số dưới 5%. ESP32 đọc ADC 12-bit để đủ độ phân giải ở vùng 1.000–10.000 ppm (ở vùng này đường cong rất dẹt).

| ppm (Wokwi) | 0.1 | 1 | 10 | 100 | 400 | 1096 | 1660 | 4786 | 9550 | 100000 |
|---|---|---|---|---|---|---|---|---|---|---|
| ADC 10-bit đo được | 211 | 423 | 650 | 834 | 906 | 943 | 954 | 978 | 988 | 1009 |
| ppm tính lại | 0.1 | 1.1 | 10.1 | 99 | 397 | 1115 | 1652 | 5027 | 9568 | 96092 |

**Vùng trễ (hysteresis):** để hạ mức, gas phải xuống dưới ngưỡng − 200 ppm (VD: dưới 1800 ppm mới hết CẢNH BÁO), nhiệt phải xuống dưới ngưỡng − 2°C. Nhờ vậy trạng thái không nhảy qua lại khi giá trị dao động quanh ngưỡng.

**Khoá van:** khi đã vào NGUY HIỂM hoặc CHÁY, hệ thống giữ nguyên trạng thái đó (van đóng, còi kêu) kể cả khi gas giảm hoặc hết lửa. Muốn mở lại van:
- bấm nút **RESET** trên mạch, hoặc
- bấm **RESET** trên dashboard Node-RED (gửi lệnh qua MQTT).

RESET chỉ được chấp nhận khi cảm biến đã về mức BÌNH THƯỜNG; nếu chưa, LCD hiện `CHUA AN TOAN!` và dashboard báo từ chối. Khi đã an toàn nhưng chưa reset, LCD hiện `AN TOAN-BAM RST`.

**Mất mạng:** báo động tại chỗ (LED, còi, van, quạt) vẫn hoạt động bình thường; ESP32 tự thử kết nối lại WiFi mỗi 10s, MQTT mỗi 5s.

---

## ⚙️ Cài đặt và chạy

### 1. Mô phỏng Wokwi (không cần phần cứng thật)

1. Vào [wokwi.com](https://wokwi.com) → New Project → ESP32
2. Dán nội dung `GasFireAlarm_ESP32.ino` vào tab `sketch.ino`
3. Dán nội dung `diagram_esp32.json` vào tab `diagram.json`
4. Bấm **▶ Play** để chạy mô phỏng

### 2. Node-RED Dashboard

```bash
# Cài Node-RED (nếu chưa có)
npm install -g --unsafe-perm node-red
node-red-admin install node-red-dashboard

# Chạy Node-RED
node-red
```

1. Mở `http://localhost:1880`
2. Menu → **Import** → dán nội dung `nodered-flow.json`
3. Bấm **Deploy**
4. Mở dashboard tại `http://localhost:1880/ui`

### 3. Script giả lập (thay thế ESP32 thật)

```bash
# Cài dependencies
npm install

# Chạy giả lập
node simulate.js
```

Gas tự động dao động 400→7000 ppm, nhiệt độ 24→65°C. Sau 40 giây kể từ khi kết nối sẽ tự test trạng thái **CHÁY** trong 10 giây.

> ⚠️ `simulate.js` và `control.html` gửi lên **cùng topic** với ESP32 (có trường `"source"` để phân biệt). Không chạy chúng cùng lúc với ESP32 thật, nếu không dashboard sẽ nhảy qua lại và Telegram gửi cảnh báo sai. Dashboard và Telegram có ghi chú `(gia lap)` khi dữ liệu không đến từ ESP32.

### 4. Bảng điều khiển web

Mở file `control.html` trong trình duyệt (Chrome/Edge).

- Kéo slider điều chỉnh Gas và Nhiệt độ
- Bật/tắt phát hiện lửa
- Chế độ **Tự động**: gửi dữ liệu mỗi 2 giây
- Chế độ **Thủ công**: bấm "Gửi ngay" mới gửi
- Trang mở lên ở trạng thái **DỪNG** (không tự gửi, tránh ghi đè dữ liệu thật của ESP32). Bấm **▶ CHẠY** để bắt đầu, **⏹ DỪNG** để tạm dừng

### 5. Sa bàn bếp 2D

Mở file `kitchen.html` trong trình duyệt — tự động kết nối MQTT và cập nhật theo trạng thái thật. Nếu 10 giây không có dữ liệu (hoặc ESP32 báo OFFLINE), thanh trạng thái chuyển sang xám **MẤT KẾT NỐI THIẾT BỊ**.

---

## 📡 MQTT

| Thông số | Giá trị |
|----------|---------|
| Broker | `broker.hivemq.com` |
| Port | `1883` (TCP) / `8000` (WebSocket) |

| Topic | Chiều | Nội dung |
|-------|-------|----------|
| `gas-fire-alarm/sensors` | ESP32 → Dashboard | Dữ liệu cảm biến (JSON), mỗi 2s và ngay khi đổi trạng thái |
| `gas-fire-alarm/status` | ESP32 → Dashboard | `ONLINE` / `OFFLINE` (retained, OFFLINE là LWT do broker tự gửi khi ESP32 mất kết nối) |
| `gas-fire-alarm/cmd` | Dashboard → ESP32 | `RESET` — mở khoá van (không dùng retain) |
| `gas-fire-alarm/event` | ESP32 → Dashboard | Kết quả reset: `RESET_OK`, `RESET_TU_CHOI`, `RESET_KHONG_CAN` |

**Payload JSON mẫu (`sensors`):**
```json
{
  "source": "esp32",
  "gas": 3200,
  "gasRaw": 3910,
  "temp": 35.2,
  "dhtError": false,
  "flame": "KHONG",
  "state": "CANH BAO",
  "valve": "MO",
  "fan": "CHAM",
  "latched": false
}
```

> `gas` là nồng độ đã quy đổi ra ppm; `gasRaw` là giá trị ADC 12-bit gốc (chỉ ESP32 gửi).

> ⚠️ `broker.hivemq.com` là broker công khai, không có mật khẩu: ai biết topic đều có thể đọc/gửi dữ liệu. Lệnh RESET chỉ có tác dụng khi cảm biến đã an toàn, nhưng khi triển khai thật nên dùng broker riêng có TLS + tài khoản.

---

## 📱 Telegram Bot

Token bot và chat ID **không** được lưu trong `nodered-flow.json`. Đặt chúng qua biến môi trường trước khi chạy Node-RED:

```powershell
# Windows PowerShell
$env:TG_TOKEN   = "<token lấy từ @BotFather>"
$env:TG_CHAT_ID = "<chat id nhận cảnh báo>"
node-red
```

```bash
# Linux / macOS
TG_TOKEN="<token>" TG_CHAT_ID="<chat id>" node-red
```

Nếu thiếu biến môi trường, node "Kiem tra canh bao" sẽ hiện cảnh báo trong tab Debug và không gửi tin.

> ⚠️ Không commit token lên Git. Nếu token đã bị lộ, dùng lệnh `/revoke` trong @BotFather để tạo token mới.

Bot `@gascanhbao_bot` gửi tin nhắn khi:
- mức cảnh báo **tăng** (BÌNH THƯỜNG → CẢNH BÁO → NGUY HIỂM → CHÁY),
- hệ thống **trở lại bình thường**,
- **mất kết nối** thiết bị (10s không có dữ liệu) và khi **kết nối lại**.

Hạ mức (VD CHÁY → NGUY HIỂM) không gửi để tránh spam.

```
⚠️ CANH BAO HE THONG GAS & CHAY
Trang thai: CANH BAO
Gas: 450 ppm
Nhiet do: 35C
Lua: KHONG
```

---

## 🛠️ Thư viện Arduino cần cài

**Wokwi (wokwi.com):** dán nội dung file `libraries.txt` vào tab `libraries.txt` của project.

| Thư viện | Tác giả | Bản ESP32 | Bản Uno |
|----------|---------|:---:|:---:|
| LiquidCrystal I2C | Frank de Brabander | ✅ | ✅ |
| DHT sensor library | Adafruit | ✅ | ✅ |
| Adafruit Unified Sensor | Adafruit (DHT cần) | ✅ | ✅ |
| ESP32Servo | Kevin Harrington | ✅ | |
| Servo | Arduino | | ✅ |
| PubSubClient | Nick O'Leary (MQTT) | ✅ | |
| ArduinoJson | Benoit Blanchon | ✅ | |

Với bản Uno trên Wokwi, `libraries.txt` dùng: `LiquidCrystal I2C`, `DHT sensor library`, `Adafruit Unified Sensor`, `Servo`.

**Arduino IDE:** Tools → Manage Libraries → tìm tên trong bảng → Install. `WiFi` và `Wire` có sẵn trong gói board ESP32, không cần cài.

---

## 📸 Giao diện

| Control Panel | Kitchen 2D | Node-RED Dashboard |
|:---:|:---:|:---:|
| Bảng điều khiển web | Sa bàn bếp mô phỏng | Dashboard realtime |
