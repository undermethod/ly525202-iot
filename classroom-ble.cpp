#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <ArduinoBLE.h>
#include <SPI.h>

#define BME_CS 10
const char* nodeName = "classroom_orange";

Adafruit_BME280 bme(BME_CS);

// standard UUIDs
BLEService envService("181A");
BLEFloatCharacteristic temperatureChar("2A6E", BLERead | BLENotify);
BLEFloatCharacteristic humidityChar("2A6F", BLERead | BLENotify);
BLEFloatCharacteristic pressureChar("2A6D", BLERead | BLENotify);

void setup() {
  Serial.begin(115200);

  if (!bme.begin()) {
    Serial.println("❌ could not find BME280 sensor");
    while (1);
  }

  if (!BLE.begin()) {
    Serial.println("❌ starting BLE failed");
    while (1);
  }

  BLE.setLocalName(nodeName);
  BLE.setAdvertisedService(envService);

  envService.addCharacteristic(temperatureChar);
  envService.addCharacteristic(humidityChar);
  envService.addCharacteristic(pressureChar);

  BLE.addService(envService);
  BLE.advertise();

  Serial.println("✅ BLE node ready");
}

void loop() {
  float temperature = bme.readTemperature();
  float humidity = bme.readHumidity();
  float pressure = bme.readPressure() / 100.0F;

  Serial.print("Node: "); Serial.println(nodeName);
  Serial.print("  Temp: "); Serial.println(temperature);
  Serial.print("  Humidity: "); Serial.println(humidity);
  Serial.print("  Pressure: "); Serial.println(pressure);
  Serial.println("---------------------");

  BLEDevice gateway = BLE.central();
  if (gateway) {
    Serial.print("Connected to gateway: "); Serial.println(gateway.address());
    while (gateway.connected()) {
      temperature = bme.readTemperature();
      humidity = bme.readHumidity();
      pressure = bme.readPressure() / 100.0F;
      
      // push
      temperatureChar.writeValue(temperature);
      humidityChar.writeValue(humidity);
      pressureChar.writeValue(pressure);

      delay(1000);
    }
    Serial.print("Disconnected from central: "); Serial.println(gateway.address());
  }
  BLE.poll();
  delay(1000);
}
// Done