/*
  Mach bao chay gia dinh - ESP32 + MQ2 + DHT22 + MQTT
  Chay tren Wokwi (WiFi: Wokwi-GUEST)
*/
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

// ---------------- CHAN KET NOI ----------------
#define MQ2_PIN    34   // AOUT cua MQ2 (ADC1)
#define DHT_PIN    15   // SDA cua DHT22
#define DHT_TYPE   DHT22
#define FAN_PIN    26   // Quat (LED do)
#define PUMP_PIN   27   // May bom (LED xanh la)
#define ALARM_PIN  25   // Den canh bao (LED vang)

// ---------------- NGUONG CANH BAO ----------------
const float GAS_THRESHOLD  = 1000.0;  // ppm  -> bat quat
const float TEMP_THRESHOLD = 50.0;    // do C -> bat may bom

// ---------------- WIFI + MQTT ----------------
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";
const char* MQTT_HOST = "broker.emqx.io";
const int   MQTT_PORT = 1883;

// DOI "nhom01" THANH MA RIENG CUA BAN (de khong trung voi nguoi khac)
#define BASE "baochay/nhom01/"
const char* T_GAS        = BASE "gas";
const char* T_TEMP       = BASE "temp";
const char* T_FAN_STATE  = BASE "fan/state";
const char* T_PUMP_STATE = BASE "pump/state";
const char* T_MODE_STATE = BASE "mode/state";
const char* T_FAN_CMD    = BASE "fan/cmd";
const char* T_PUMP_CMD   = BASE "pump/cmd";
const char* T_MODE_CMD   = BASE "mode/cmd";

WiFiClient   espClient;
PubSubClient mqtt(espClient);
DHT          dht(DHT_PIN, DHT_TYPE);

bool autoMode  = true;   // true = tu dong, false = manual
bool fanOn     = false;
bool pumpOn    = false;
float gasPpm   = 0;
float tempC    = 0;

unsigned long lastRead = 0;

// ---------------- HAM PHU ----------------
void applyOutputs() {
  digitalWrite(FAN_PIN,  fanOn  ? HIGH : LOW);
  digitalWrite(PUMP_PIN, pumpOn ? HIGH : LOW);
}

void publishStates() {
  mqtt.publish(T_FAN_STATE,  fanOn  ? "ON" : "OFF", true);
  mqtt.publish(T_PUMP_STATE, pumpOn ? "ON" : "OFF", true);
  mqtt.publish(T_MODE_STATE, autoMode ? "AUTO" : "MANUAL", true);
}

void callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();
  msg.toUpperCase();
  String t = String(topic);
  Serial.printf("[MQTT] %s = %s\n", topic, msg.c_str());

  if (t == T_MODE_CMD) {
    autoMode = (msg == "AUTO");
  } else if (t == T_FAN_CMD && !autoMode) {      // chi nhan lenh khi o che do manual
    fanOn = (msg == "ON");
  } else if (t == T_PUMP_CMD && !autoMode) {
    pumpOn = (msg == "ON");
  }
  applyOutputs();
  publishStates();
}

void connectWiFi() {
  Serial.print("Dang ket noi WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASS, 6);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\nWiFi OK, IP: " + WiFi.localIP().toString());
}

void connectMQTT() {
  while (!mqtt.connected()) {
    String clientId = "esp32-baochay-" + String(random(0xffff), HEX);
    Serial.print("Dang ket noi MQTT... ");
    if (mqtt.connect(clientId.c_str())) {
      Serial.println("OK");
      mqtt.subscribe(T_FAN_CMD);
      mqtt.subscribe(T_PUMP_CMD);
      mqtt.subscribe(T_MODE_CMD);
      publishStates();
    } else {
      Serial.printf("loi rc=%d, thu lai sau 2s\n", mqtt.state());
      delay(2000);
    }
  }
}

// ---------------- SETUP / LOOP ----------------
void setup() {
  Serial.begin(115200);
  pinMode(FAN_PIN, OUTPUT);
  pinMode(PUMP_PIN, OUTPUT);
  pinMode(ALARM_PIN, OUTPUT);
  analogReadResolution(12);
  dht.begin();
  applyOutputs();

  connectWiFi();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(callback);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  if (!mqtt.connected()) connectMQTT();
  mqtt.loop();

  if (millis() - lastRead >= 2000) {
    lastRead = millis();

    // 1. Doc cam bien
    int raw = analogRead(MQ2_PIN);               // 0..4095
    gasPpm = raw * 10000.0 / 4095.0;             // quy doi gan dung ra ppm
    float t = dht.readTemperature();
    if (!isnan(t)) tempC = t;

    // 2. Thuat toan canh bao (chi chay o che do AUTO)
    bool gasAlarm  = gasPpm > GAS_THRESHOLD;
    bool tempAlarm = tempC  > TEMP_THRESHOLD;
    if (autoMode) {
      fanOn  = gasAlarm;    // khi gas vuot nguong -> bat quat
      pumpOn = tempAlarm;   // khi nhiet do vuot nguong -> bat may bom
    }
    digitalWrite(ALARM_PIN, (gasAlarm || tempAlarm) ? HIGH : LOW);
    applyOutputs();

    // 3. Hien thi Serial
    Serial.printf("Gas: %.0f ppm | Nhiet do: %.1f C | Quat: %s | Bom: %s | Che do: %s\n",
                  gasPpm, tempC, fanOn ? "ON" : "OFF", pumpOn ? "ON" : "OFF",
                  autoMode ? "AUTO" : "MANUAL");

    // 4. Gui len MQTT
    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f", gasPpm);
    mqtt.publish(T_GAS, buf);
    snprintf(buf, sizeof(buf), "%.1f", tempC);
    mqtt.publish(T_TEMP, buf);
    publishStates();
  }
}
