// Script giả lập dữ liệu cảm biến gửi lên MQTT
// Lưu ý: gửi lên CÙNG topic với ESP32 → không chạy cùng lúc với ESP32 thật.
const mqtt = require('mqtt');

const TOPIC = 'gas-fire-alarm/sensors';

const client = mqtt.connect('mqtt://broker.hivemq.com:1883', {
  clientId: 'simulator-gas-alarm-' + Math.random().toString(16).slice(2)
});

let gasValue = 400;   // ppm
let temperature = 24;
let flameDetected = false;
let direction = 1;
let tempDirection = 1;
let publishTimer = null;

function computeState() {
  if (flameDetected) return 'CHAY';
  if (gasValue >= 5000 || temperature >= 50) return 'NGUY HIEM';
  if (gasValue >= 2000 || temperature >= 40) return 'CANH BAO';
  return 'BINH THUONG';
}

function step() {
  // Tăng/giảm gas để mô phỏng (400 → 7000 → 400 ppm, bước 300 mỗi 2s)
  gasValue += direction * 300;
  if (gasValue >= 7000) direction = -1;
  if (gasValue <= 400)  direction = 1;

  // Tăng/giảm nhiệt độ (24°C → 65°C → 24°C, bước 1°C mỗi 2s)
  temperature += tempDirection * 1;
  if (temperature >= 65) tempDirection = -1;
  if (temperature <= 24) tempDirection = 1;

  const state = computeState();
  const danger = (state === 'NGUY HIEM' || state === 'CHAY');
  const payload = JSON.stringify({
    source: 'simulate',
    gas: gasValue,
    temp: temperature,
    dhtError: false,
    flame: flameDetected ? 'CO LUA' : 'KHONG',
    state,
    valve: danger ? 'DONG' : 'MO',
    fan: danger ? 'NHANH' : state === 'CANH BAO' ? 'CHAM' : 'TAT',
    latched: false
  });

  client.publish(TOPIC, payload);
  console.log('Đã gửi:', payload);
}

client.on('connect', () => {
  console.log('Đã kết nối MQTT broker!');
  // 'connect' chạy lại sau mỗi lần reconnect → chỉ tạo timer một lần,
  // nếu không sẽ có nhiều vòng gửi chồng lên nhau.
  if (publishTimer) return;

  console.log('Bắt đầu gửi dữ liệu mỗi 2 giây...\n');
  publishTimer = setInterval(step, 2000);

  // Tự động test CHAY sau 40 giây kể từ khi kết nối
  setTimeout(() => {
    console.log('\n>>> TEST CHAY: bật lửa trong 10 giây...');
    flameDetected = true;
    setTimeout(() => {
      flameDetected = false;
      console.log('>>> Tắt lửa, tiếp tục bình thường.\n');
    }, 10000);
  }, 40000);
});

client.on('error', (err) => {
  console.error('Lỗi:', err.message);
});

// Nhấn Ctrl+C để dừng
process.on('SIGINT', () => {
  console.log('\nDừng simulator.');
  if (publishTimer) clearInterval(publishTimer);
  client.end();
  process.exit();
});
