#include <Adafruit_BME280.h>

#define NODE_ID "classroom_orange"
#define INTERVAL_MS 2000

Adafruit_BME280 bme(10); // CSB pin

void setup() {
  Serial.begin(115200);
  Serial1.begin(9600);

  if (!bme.begin()) {
    Serial.println("❌ could not find BME280 sensor");
    while (1);
  }

  delay(1000);
  Serial.println("✅ node ready");
}

void loop() {
  float temp = bme.readTemperature();
  float hum = bme.readHumidity();
  float press = bme.readPressure() / 100.0F;

  Serial.println("");
  Serial.print(NODE_ID); Serial.println(":");
  Serial.print("  Temp: "); Serial.println(temp);
  Serial.print("  Humidity: "); Serial.println(hum);
  Serial.print("  Pressure: "); Serial.println(press);

  String payload = String(NODE_ID) + ";TEMP=" + String(temp) + ";HUM=" + String(hum) + ";PRESS=" + String(press);

  uint8_t frame[256];
  int index = 0;

  frame[index++] = 0x7E;   // start delimiter
  frame[index++] = 0x00;   // length MSB (placeholder)
  frame[index++] = 0x00;   // length LSB (placeholder)

  frame[index++] = 0x10;   // frame type: Transmit Request
  frame[index++] = 0x01;   // frame ID

  // destination: broadcast
  uint8_t dest64[8] = {0,0,0,0,0,0,0xFF,0xFF};
  for (int i = 0; i < 8; i++) {
    frame[index++] = dest64[i];
  }

  // destination address unknown
  frame[index++] = 0xFF;
  frame[index++] = 0xFE;

  frame[index++] = 0x00;   // broadcast radius
  frame[index++] = 0x00;   // options

  // payload
  for (int i = 0; i < payload.length(); i++) {
    frame[index++] = payload[i];
  }
  int length = index-3;
  frame[1] = (length >> 8) & 0xFF;
  frame[2] = length & 0xFF;

  uint8_t sum = 0;
  for(int i = 3; i < index; i++) {
    sum += frame[i];
  }
  frame[index++] = 0xFF - sum;

  Serial1.write(frame, index);

  delay(INTERVAL_MS);
}
