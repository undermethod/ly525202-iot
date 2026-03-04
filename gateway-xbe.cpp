#include <WiFiS3.h>
#include <ESP_SSLClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#define EXPECTED_INTERVAL 2000
const char* WIFI_SSID = "wifissid";
const char* WIFI_PSWD = "wifipswd";
const char* WIFI_LOCAL_STATIC_IP = "10.11.12.222";
const char* MQTT_SERVER = "10.11.12.13";
const uint16_t MQTT_PORT = 8883;
const char* MQTT_USER = "mqttuser";
const char* MQTT_PSWD = "mqttpswd";
const char* ADMIN_PSWD = "adminpswd";
bool SNR_MODE = false;
float HUM_PCT_LIMIT = 40.0;
float AC_TEMP_LIMIT = 26.0;
float HT_TEMP_LIMIT = 23.0;

WiFiClient basicClient;
ESP_SSLClient sslClient;
PubSubClient mqttClient(sslClient);

struct Node {
  const char* name;
  int AC_LED_PIN;
  int HT_LED_PIN;
  int FAN_PIN_NEG;
  int FAN_PIN_POS;
  float temperature;
  float humidity;
  float pressure;
  unsigned long lastSeen;
  unsigned long packetCount;
  float meanRSSI;
  float m2;
  float lastInterval;

  Node(const char* nodeName,
          int acPin,
          int htPin,
          int fanPinNeg,
          int fanPinPos)
    : name(nodeName),
      AC_LED_PIN(acPin),
      HT_LED_PIN(htPin),
      FAN_PIN_NEG(fanPinNeg),
      FAN_PIN_POS(fanPinPos),
      temperature(0),
      humidity(0),
      pressure(0),
      lastSeen(0),
      packetCount(0),
      meanRSSI(0),
      m2(0),
      lastInterval(0)
  {}
};
Node nodes[] = {
  /*
  per classroom:
  Node(name, acPin, htPin, fanPinNeg, fanPinPos)
  */
  Node("classroom_orange", 13, 12, 2, 3),
  Node("classroom_white", 8, 7, 4, 5)
};
const int NUM_NODES = sizeof(nodes) / sizeof(nodes[0]);

char currentNodeName[32] = "";

Node* getNode(const char* name) {
  for (int i = 0; i < NUM_NODES; i++) {
    if (strcmp(nodes[i].name, name) == 0) return &nodes[i];
  }
  return nullptr;
}

void requestRSSI() {
  uint8_t frame[] = {
    0x7E,       // start
    0x00, 0x04, // length
    0x08,       // AT command frame
    0x52,       // frame ID ('R')
    'D', 'B',   // DB command
    0x00        // placeholder checksum
  };

  uint8_t sum = 0;
  for (int i = 3; i < 7; i++) sum += frame[i];
  frame[7] = 0xFF - sum;

  Serial1.write(frame, 8);
}

void computeELQI(Node* node, float variance) {
  // RSSI score (map -90 → 0, -40 → 100)
  float rssiScore = constrain(
      (node->meanRSSI + 90) * 2,
      0,
      100);

  float stabilityScore = constrain(
      100 - variance * 2,
      0,
      100);

  float intervalError = abs(node->lastInterval - EXPECTED_INTERVAL);
  float timingScore = constrain(
      100 - (intervalError / 20.0),
      0,
      100);

  float elqi = (0.5 * rssiScore) +
               (0.3 * stabilityScore) +
               (0.2 * timingScore);

  Serial.print("ELQI Score: "); Serial.println(elqi);
}

void handleCallback(char* topic, byte* payload, unsigned int length) {
  StaticJsonDocument<128> obj;
  DeserializationError err = deserializeJson(obj, payload, length);
  if (err) return;

  const char* valPswd = obj["password"];
  if (valPswd == nullptr || strcmp(valPswd, ADMIN_PSWD) != 0) {
    Serial.println("❌ admin attempt failed");
    return;
  }

  float valAc = obj["ac"];
  if (valAc != 0.0F) {
    Serial.print("🖥️ Updating AC to "); Serial.println(valAc);
    AC_TEMP_LIMIT = valAc;
  }
  float valHt = obj["heat"];
  if (valHt != 0.0F) {
    Serial.print("🖥️ Updating Heat to "); Serial.println(valHt);
    HT_TEMP_LIMIT = valHt;
  }
  float valHum = obj["humidity"];
  if (valHum != 0.0F) {
    Serial.print("🖥️ Updating Humidity to "); Serial.println(valHum);
    HUM_PCT_LIMIT = valHum;
  }
}

void sub() {
  mqttClient.subscribe("cesi/lyon/gateway");
  delay(500);
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(9600);
  delay(2000);

  Serial.print("Wi-Fi...");
  WiFi.config(WIFI_LOCAL_STATIC_IP);
  WiFi.begin(WIFI_SSID, WIFI_PSWD);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > 20000) {
      Serial.println("❌ Could not connect to Wi-Fi");
      while(true) delay(1000);
    }
    delay(500);
    Serial.print(".");
  }

  Serial.print(" DHCP...");
  unsigned long dhcpStart = millis();
  while (WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
    if (millis() - dhcpStart > 10000) {
      Serial.println("❌ DHCP failed");
      while(true) delay(1000);
    }
    delay(500);
    Serial.print(".");
  }
  Serial.print(" 🛜 connected with IP: "); Serial.println(WiFi.localIP());

  Serial.print("TCP to broker...");
  if (!basicClient.connect(MQTT_SERVER, MQTT_PORT)) {
    Serial.println("\n❌ TCP failed");
  } else {
    Serial.println(" ✔️ TCP ok");
    basicClient.stop();
    delay(500);
  }

  sslClient.setClient(&basicClient);
  sslClient.setInsecure(); // skip cert verification, traffic still encrypted
  sslClient.setBufferSizes(2048, 1024);
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setBufferSize(512);
  mqttClient.setCallback(handleCallback);

  Serial.print("MQTT...");
  unsigned long mqttStart = millis();
  while (!mqttClient.connected()) {
    if (millis() - mqttStart > 30000) {
      Serial.println("\n❌ MQTT connection timed out");
      while(true) delay(1000);
    }
    if (mqttClient.connect("gateway", MQTT_USER, MQTT_PSWD)) {
      Serial.println(" ✔️ connected");
    } else {
      Serial.print("❌ MQTT failed, rc="); Serial.println(mqttClient.state());
      delay(2000);
    }
  }
  sub();

  for (int i = 0; i < NUM_NODES; i++) {
    pinMode(nodes[i].AC_LED_PIN, OUTPUT);
    pinMode(nodes[i].HT_LED_PIN, OUTPUT);
    pinMode(nodes[i].FAN_PIN_POS, OUTPUT);
    pinMode(nodes[i].FAN_PIN_NEG, OUTPUT);
  }

  delay(1000);
  Serial.println("✅ gateway ready");
}

void loop() {
  if (!mqttClient.connected()) {
    Serial.print("MQTT reconnecting...");
    if (mqttClient.connect("gateway", MQTT_USER, MQTT_PSWD)) {
      Serial.println(" ✔️ reconnected");
      sub();
    } else {
      Serial.print("❌ rc="); Serial.println(mqttClient.state());
      delay(2000);
      return;
    }
  }
  mqttClient.loop();

  static enum { WAIT_START, WAIT_LENGTH_1, WAIT_LENGTH_2, WAIT_FRAME } state = WAIT_START;
  static uint16_t length = 0;
  static uint16_t index = 0;
  static uint8_t buffer[256];

  while (Serial1.available()) {
    uint8_t b = Serial1.read();

    switch (state) {
      case WAIT_START:
        if (b == 0x7E) {
          state = WAIT_LENGTH_1;
        }
        break;
      case WAIT_LENGTH_1:
        length = b << 8;
        state = WAIT_LENGTH_2;
        break;
      case WAIT_LENGTH_2:
        length |= b;
        if (length > sizeof(buffer)) {
          // avoid for bus fault
          state = WAIT_START;
          break;
        }
        index = 0;
        state = WAIT_FRAME;
        break;
      case WAIT_FRAME:
        buffer[index++] = b;
        if (index >= length) {
          uint8_t frameType = buffer[0];

          if (frameType == 0x88) {
            // last byte is RSSI: [0x88, Frame ID, 'D', 'B', status, RSSI]
            if (buffer[2] == 'D' && buffer[3] == 'B') {
              uint8_t rssiVal = buffer[5];

              int rssi = - (int)rssiVal;
              unsigned long now = millis();
              Node* node = getNode(currentNodeName);
              if (node) {
                node->packetCount++;
                if (node->lastSeen != 0) {
                  node->lastInterval = now - node->lastSeen;
                }
                node->lastSeen = now;

                float delta = rssi - node->meanRSSI;
                node->meanRSSI += delta / node->packetCount;
                node->m2 += delta * (rssi - node->meanRSSI);

                float variance = (node->packetCount > 1)
                                  ? node->m2 / (node->packetCount - 1)
                                  : 0;

                // Serial.print("Node: "); Serial.println(node->name);
                Serial.print("Avg RSSI: "); Serial.println(node->meanRSSI);
                Serial.print("RSSI Var: "); Serial.println(variance);
                Serial.print("Interval(ms): "); Serial.println(node->lastInterval);

                computeELQI(node, variance);
              }
            }
          }

          if (frameType == 0x91) { // 91 instead of 90 (AO: 1=Explicit instead of 0=Native)
            // extract payload
            int payloadStart = 18;
            if (SNR_MODE) {
              Serial.println(""); Serial.print("Payload length: "); Serial.println(length);
            }
            if (length <= payloadStart) {
              // avoid bus fault
              state = WAIT_START;
              break;
            }
            char* payloadPtr = (char*)&buffer[payloadStart];

            char buf[64];
            int payloadLen = length - payloadStart;
            if (payloadLen > 63) payloadLen = 63;
            memcpy(buf, payloadPtr, payloadLen);
            buf[payloadLen] = '\0';

            char* delim = strchr(buf, ';');
            if (delim == nullptr) break;
            *delim = '\0';

           currentNodeName[0] = '\0';
            Node* node = getNode(buf);
            if (node == nullptr) {
              state = WAIT_START;
              break;
            }
            strncpy(currentNodeName, node->name, sizeof(currentNodeName) - 1);
            currentNodeName[sizeof(currentNodeName) - 1] = '\0';
            float lastTemp  = node->temperature;
            float lastHum   = node->humidity;
            float lastPress = node->pressure;

            float temperature = 0, humidity = 0, pressure = 0;
            char* token = strtok(delim + 1, ";");
            while (token != nullptr) {
              if (strncmp(token, "TEMP=", 5) == 0)  temperature = strtof(token + 5, nullptr);
              if (strncmp(token, "HUM=", 4) == 0)   humidity    = strtof(token + 4, nullptr);
              if (strncmp(token, "PRESS=", 6) == 0) pressure    = strtof(token + 6, nullptr);
              token = strtok(nullptr, ";");
            }

            char topicBuf[48];
            char valueBuf[16];
            String base = "cesi/lyon/" + String(node->name) + "/";

            (base + "temperature").toCharArray(topicBuf, sizeof(topicBuf));
            dtostrf(temperature, 1, 4, valueBuf);
            mqttClient.publish(topicBuf, valueBuf);

            (base + "humidity").toCharArray(topicBuf, sizeof(topicBuf));
            dtostrf(humidity, 1, 4, valueBuf);
            mqttClient.publish(topicBuf, valueBuf);

            (base + "pressure").toCharArray(topicBuf, sizeof(topicBuf));
            dtostrf(pressure, 1, 4, valueBuf);
            mqttClient.publish(topicBuf, valueBuf);

            bool updated = false;
            if (temperature != 0 || humidity != 0) {
              if (temperature != lastTemp || humidity != lastHum) {
                updated = true;
              }
              node->temperature = temperature;
              node->humidity    = humidity;
              node->pressure    = pressure;

              if(updated) {
                Serial.print(node->name);
                Serial.print(": ");
                Serial.print(node->temperature);
                Serial.print(" °C, ");
                Serial.print(node->humidity);
                Serial.print(" %, ");
                Serial.print(node->pressure);
                Serial.println(" hPa");
              }
              if (SNR_MODE) requestRSSI();
            }
          }

          state = WAIT_START;
        }
        break;
      default:
        Serial.print("Unimplemented state "); Serial.println(state);
    }
  }
  for(int i = 0; i < NUM_NODES; i++) {
    Node &node = nodes[i];
    digitalWrite(node.AC_LED_PIN, (node.temperature != 0 && node.temperature >= AC_TEMP_LIMIT) ? HIGH : LOW);
    digitalWrite(node.HT_LED_PIN, (node.temperature != 0 && node.temperature <= HT_TEMP_LIMIT) ? HIGH : LOW);
    digitalWrite(node.FAN_PIN_NEG, (node.humidity != 0 && node.humidity >= HUM_PCT_LIMIT) ? LOW : HIGH);
    digitalWrite(node.FAN_PIN_POS, HIGH);
  }
}
