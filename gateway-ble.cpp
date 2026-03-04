#include <ArduinoBLE.h>
#include <WiFiS3.h>

// standard UUIDs
#define ENV_SERVICE_UUID "181A"
#define TEMP_CHAR_UUID   "2A6E"
#define HUM_CHAR_UUID    "2A6F"
#define PRESS_CHAR_UUID  "2A6D"

float HUM_PCT_LIMIT = 40.0;
float AC_TEMP_LIMIT = 26.0;
float HT_TEMP_LIMIT = 23.0;

// per classroom 1/2
const char* NODE_O_NAME = "classroom_orange";
const int NODE_O_AC_PIN = 4;
const int NODE_O_HT_PIN = 2;
const int NODE_O_FAN_PIN_NEG = 7;
const int NODE_O_FAN_PIN_POS = 6;
const char* NODE_W_NAME = "classroom_white";
const int NODE_W_AC_PIN = 12;
const int NODE_W_HT_PIN = 13;
const int NODE_W_FAN_PIN_NEG = 8;
const int NODE_W_FAN_PIN_POS = 9;

const char WIFI_SSID[] = "wifissid";
const char WIFI_PSWD[] = "wifipswd";
const char WIFI_ADMIN_KEY[] = "adminpswd";
WiFiServer server(80);

struct BLENode {
    const char* name;
    BLEDevice* device;
    bool subscribed;
    BLECharacteristic tempChar;
    BLECharacteristic humChar;
    BLECharacteristic pressChar;
    float temperature;
    float humidity;
    float pressure;
    int AC_LED_PIN;
    int HT_LED_PIN;
    int FAN_PIN_NEG;
    int FAN_PIN_POS;

    BLENode(const char* nodeName,
            int acPin,
            int htPin,
            int fanPinNeg,
            int fanPinPos)
      : name(nodeName),
        device(nullptr),
        subscribed(false),
        tempChar(),
        humChar(),
        pressChar(),
        temperature(0),
        humidity(0),
        pressure(0),
        AC_LED_PIN(acPin),
        HT_LED_PIN(htPin),
        FAN_PIN_NEG(fanPinNeg),
        FAN_PIN_POS(fanPinPos)
    {}
};
BLENode nodes[] = {
  // per classroom 2/2
  BLENode(NODE_O_NAME, NODE_O_AC_PIN, NODE_O_HT_PIN, NODE_O_FAN_PIN_NEG, NODE_O_FAN_PIN_POS),
  BLENode(NODE_W_NAME, NODE_W_AC_PIN, NODE_W_HT_PIN, NODE_W_FAN_PIN_NEG, NODE_W_FAN_PIN_POS)
};
const int NUM_NODES = sizeof(nodes) / sizeof(nodes[0]);

BLEDevice* connectToNode(const char* nodeName, unsigned long timeoutMs) {
  BLE.scanForName(nodeName);

  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    BLEDevice peripheral = BLE.available();
    if (peripheral && peripheral.localName() == nodeName) {
      BLE.stopScan();
      if (peripheral.connect()) {
        Serial.print("Found "); Serial.print(nodeName); Serial.print("...");
        peripheral.discoverAttributes();
        Serial.println("connected ✅");
        return new BLEDevice(peripheral);
      }
    }
  }
  BLE.stopScan();
  return nullptr;
}

void setup() {
  Serial.begin(115200);
  // while (!Serial);

  // wifi
  int status = WL_IDLE_STATUS;
  while (status != WL_CONNECTED) {
    status = WiFi.begin(WIFI_SSID, WIFI_PSWD);
    delay(1000);
  }
  Serial.print("🛜 Connected to Wi-Fi with IP address: "); Serial.println(WiFi.localIP());
  server.begin();

  // bluetooth
  if (!BLE.begin()) {
    Serial.println("❌ failed to initialize BLE");
    while (1);
  }
  for (int i = 0; i < NUM_NODES; i++) {
    if (nodes[i].AC_LED_PIN != -1) pinMode(nodes[i].AC_LED_PIN, OUTPUT);
    if (nodes[i].HT_LED_PIN != -1) pinMode(nodes[i].HT_LED_PIN, OUTPUT);
    if (nodes[i].FAN_PIN_NEG != -1) pinMode(nodes[i].FAN_PIN_NEG, OUTPUT);
    if (nodes[i].FAN_PIN_POS != -1) pinMode(nodes[i].FAN_PIN_POS, OUTPUT);
  }
  Serial.println("📞 BLE gateway ready");
}

void loop() {
  for (int i = 0; i < NUM_NODES; i++) {
    // subscribe
    BLENode &node = nodes[i];

    if (!node.device || !node.device->connected()) {
      node.device = connectToNode(node.name, 2000); // 2s retry
      node.subscribed = false;

      if (node.device) {
        node.tempChar  = node.device->characteristic(TEMP_CHAR_UUID);
        node.humChar   = node.device->characteristic(HUM_CHAR_UUID);
        node.pressChar = node.device->characteristic(PRESS_CHAR_UUID);

        if (node.tempChar)  node.tempChar.subscribe();
        if (node.humChar)   node.humChar.subscribe();
        if (node.pressChar) node.pressChar.subscribe();
        node.subscribed = true;
      }
    }

    // read
    bool updated = false;
    float lastTemp = node.temperature;
    node.tempChar.readValue((byte*)&node.temperature, sizeof(float));
    if (lastTemp != node.temperature) updated = true;

    float lastHum = node.humidity;
    node.humChar.readValue((byte*)&node.humidity, sizeof(float));
    if (lastHum != node.humidity) updated = true;

    float lastPress = node.pressure;
    node.pressChar.readValue((byte*)&node.pressure, sizeof(float));
    if (lastPress != node.pressure) updated = true;

    if(updated) {
      Serial.print("["); Serial.print(node.name); Serial.print("] ");
      Serial.print("Temp: "); Serial.print(node.temperature);
      Serial.print(" °C, Humidity: "); Serial.print(node.humidity);
      Serial.print(" %, Pressure: "); Serial.print(node.pressure); Serial.println(" hPa");

      if (node.AC_LED_PIN != -1)
        digitalWrite(node.AC_LED_PIN, (node.temperature >= AC_TEMP_LIMIT) ? HIGH : LOW);
      
      if (node.HT_LED_PIN != -1)
        digitalWrite(node.HT_LED_PIN, (node.temperature <= HT_TEMP_LIMIT) ? HIGH : LOW);
      
      if (node.FAN_PIN_NEG != -1)
        digitalWrite(node.FAN_PIN_NEG, (node.humidity >= HUM_PCT_LIMIT) ? LOW : HIGH);

      if (node.FAN_PIN_POS != -1)
        digitalWrite(node.FAN_PIN_POS, HIGH);
    }
  }

  WiFiClient client = server.available();
  if (client) {
    String request = client.readStringUntil('\r');
    client.flush();

    // auth
    if (request.indexOf("/set?") >= 0) {
      bool authorized = false;
      int keyIndex = request.indexOf("key=");
      if (keyIndex > 0) {
        int keyEnd = request.indexOf("&", keyIndex);
        String keyValue;
        if (keyEnd > 0)
          keyValue = request.substring(keyIndex + 4, keyEnd);
        else
          keyValue = request.substring(keyIndex + 4);
        if (keyValue == WIFI_ADMIN_KEY) {
          authorized = true;
        }
      }
      if (authorized) {
        int humIndex = request.indexOf("hum=");
        int acIndex = request.indexOf("ac=");
        int htIndex = request.indexOf("ht=");
        if (humIndex > 0) {
          int humEnd = request.indexOf("&", humIndex);
          String humValue;
          if (humEnd > 0)
            humValue = request.substring(humIndex + 4, humEnd);
          else
            humValue = request.substring(humIndex + 4);
          HUM_PCT_LIMIT = humValue.toFloat();
        }
        if (acIndex > 0) {
          int acEnd = request.indexOf("&", acIndex);
          String acValue;
          if (acEnd > 0)
            acValue = request.substring(acIndex + 3, acEnd);
          else
            acValue = request.substring(acIndex + 3);
          AC_TEMP_LIMIT = acValue.toFloat();
        }
        if (htIndex > 0) {
          int htEnd = request.indexOf("&", htIndex);
          String htValue;
          if (htEnd > 0)
            htValue = request.substring(htIndex + 3, htEnd);
          else
            htValue = request.substring(htIndex + 3);
          HT_TEMP_LIMIT = htValue.toFloat();
        }
      } else {
        Serial.println("❌ unauthorized admin attempt");
      }
    }

    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println("Connection: close"); client.println();
    client.println("<html><body>");
    client.println("<h1>Classrooms</h1>");
    client.println("<hr />");

    // User
    for (int i = 0; i < NUM_NODES; i++) {
      client.print("<h3>"); client.print(nodes[i].name); client.print("</h3>");
      client.print("Temperature: "); client.print(nodes[i].temperature); client.println(" &deg;C<br />");
      client.print("Humidity: "); client.print(nodes[i].humidity); client.println(" %<br />");
      client.print("Pressure: "); client.print(nodes[i].pressure); client.println(" hPa<br />");
      client.println("<hr />");
    }

    // Admin
    client.println("<hr />");
    client.println("<h2>Admin Controls</h2>");
    client.println("<form action='/set'>");
    client.print("Enable fan at: <input name='hum' value='"); client.print(HUM_PCT_LIMIT); client.println("'>%<br />");
    client.print("Enable heating at: <input name='ht' value='"); client.print(HT_TEMP_LIMIT); client.println("'>&deg;C<br />");
    client.print("Enable A/C cooling at: <input name='ac' value='"); client.print(AC_TEMP_LIMIT); client.println("'>&deg;C<br />");
    client.println("<label>Admin Password:</label><input type='password' value='' name='key'><br />");
    client.println("<input type='submit' value='Update'>");
    client.println("</form>");

    client.println("</body></html>");
    client.stop();
  }
  delay(750);
}
