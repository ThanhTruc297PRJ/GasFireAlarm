// Script giả lập dữ liệu cảm biến gửi lên MQTT
const mqtt = require('mqtt');

const client = mqtt.connect('mqtt://broker.hivemq.com:1883', {
  clientId: 'simulator-gas-alarm-' + Math.random().toString(16).slice(2)
});

let gasValue = 150;
let temperature = 24;
let flameDetected = false;
let direction = 1;
let tempDirection = 1;

// Tự động test CHAY sau 40 giây (20 chu kỳ x 2s)
setTimeout(() => {
  console.log('\n>>> TEST CHAY: bật lửa trong 10 giây...');
  flameDetected = true;
  setTimeout(() => {
    flameDetected = false;
    console.log('>>> Tắt lửa, tiếp tục bình thường.\n');
  }, 10000);
}, 40000);

client.on('connect', () => {
  console.log('Đã kết nối MQTT broker!');
  console.log('Bắt đầu gửi dữ liệu mỗi 2 giây...\n');

  setInterval(() => {
    // Tăng/giảm gas để mô phỏng
    gasValue += direction * 30;
    if (gasValue >= 700) direction = -1;
    if (gasValue <= 100) direction = 1;

    // Tăng/giảm nhiệt độ (24°C → 65°C → 24°C, bước 1°C mỗi 2s)
    temperature += tempDirection * 1;
    if (temperature >= 65) tempDirection = -1;
    if (temperature <= 24) tempDirection = 1;

    // Xác định trạng thái
    let state;
    if (flameDetected) state = 'CHAY';
    else if (gasValue >= 500) state = 'NGUY HIEM';
    else if (gasValue >= 300) state = 'CANH BAO';
    else state = 'BINH THUONG';

    const payload = JSON.stringify({
      gas: gasValue,
      temp: temperature,
      flame: flameDetected ? 'CO LUA' : 'KHONG',
      state: state
    });

    client.publish('gas-fire-alarm/sensors', payload);
    console.log('Đã gửi:', payload);
  }, 2000);
});

client.on('error', (err) => {
  console.error('Lỗi:', err.message);
});

// Nhấn Ctrl+C để dừng
process.on('SIGINT', () => {
  console.log('\nDừng simulator.');
  client.end();
  process.exit();
});
