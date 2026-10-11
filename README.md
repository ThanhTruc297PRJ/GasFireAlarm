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
├── GasFireAlarm_ESP32.ino   # Code Arduino cho ESP32
├── diagram_esp32.json       # Sơ đồ mạch Wokwi
├── wokwi.toml               # Cấu hình Wokwi
├── libraries.txt            # Danh sách thư viện Arduino
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
| LED Xanh (Bình thường) | GPIO 25 |
| LED Vàng (Cảnh báo) | GPIO 26 |
| LED Đỏ (Nguy hiểm/Cháy) | GPIO 27 |
| Buzzer | GPIO 14 |
| Servo (Van gas) | GPIO 13 |
| Động cơ DC (Quạt) | GPIO 12 |
| LCD I2C 16x2 | SDA=21, SCL=22 |

---

## 🚦 Ngưỡng cảnh báo

| Trạng thái | Điều kiện | LED | Buzzer | Van gas |
|------------|-----------|-----|--------|---------|
| ✅ BÌNH THƯỜNG | Gas < 300 và Nhiệt < 40°C | Xanh | Tắt | Mở |
| ⚠️ CẢNH BÁO | Gas 300–499 hoặc Nhiệt 40–49°C | Vàng | Beep chậm | Mở |
| 🚨 NGUY HIỂM | Gas ≥ 500 hoặc Nhiệt ≥ 50°C | Đỏ | Liên tục | Đóng |
| 🔥 CHÁY | Phát hiện lửa | Đỏ nhấp nháy | Báo động | Đóng |

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

Gas tự động dao động 150→700 ppm. Sau 40 giây sẽ tự test trạng thái **CHÁY** trong 10 giây.

### 4. Bảng điều khiển web

Mở file `control.html` trong trình duyệt (Chrome/Edge).

- Kéo slider điều chỉnh Gas và Nhiệt độ
- Bật/tắt phát hiện lửa
- Chế độ **Tự động**: gửi dữ liệu mỗi 2 giây
- Chế độ **Thủ công**: bấm "Gửi ngay" mới gửi
- Nút **Dừng/Chạy** để tạm dừng gửi dữ liệu

### 5. Sa bàn bếp 2D

Mở file `kitchen.html` trong trình duyệt — tự động kết nối MQTT và cập nhật theo trạng thái thật.

---

## 📡 MQTT

| Thông số | Giá trị |
|----------|---------|
| Broker | `broker.hivemq.com` |
| Port | `1883` (TCP) / `8000` (WebSocket) |
| Topic | `gas-fire-alarm/sensors` |

**Payload JSON mẫu:**
```json
{
  "gas": 450,
  "temp": 35,
  "flame": "KHONG",
  "state": "CANH BAO"
}
```

---

## 📱 Telegram Bot

Bot `@gascanhbao_bot` tự động gửi tin nhắn khi trạng thái thay đổi:

```
⚠️ CANH BAO HE THONG GAS & CHAY
Trang thai: CANH BAO
Gas: 450 ppm
Nhiet do: 35C
Lua: KHONG
```

---

## 🛠️ Thư viện Arduino cần cài

```
LiquidCrystal I2C
DHT sensor library
ESP32Servo
PubSubClient
ArduinoJson
```

---

## 📸 Giao diện

| Control Panel | Kitchen 2D | Node-RED Dashboard |
|:---:|:---:|:---:|
| Bảng điều khiển web | Sa bàn bếp mô phỏng | Dashboard realtime |
