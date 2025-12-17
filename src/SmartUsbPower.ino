#include <WiFi.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>

#define VERSION "v2.1"
#define APP "SmartUsbPower"
const char* urlBase = "http://updateserver";

const char* ssid = "";
const char* password = "";

const char* mqtt_server = "192.168.1.xx";
const int mqtt_port = 1883;
const char* mqtt_user = "";
const char* mqtt_password = "";

const char* device_name = "SmartUsbPower";
const char* availability_topic = "homeassistant/switch/SmartUsbPower/availability";

WiFiClient espClient;
PubSubClient client(espClient);

const int usbPins[6] = { 19, 23, 25, 16, 17, 18 };

unsigned long lastReconnectAttempt = 0;
unsigned long lastWiFiCheck = 0;
unsigned long lastHeartbeat = 0;
unsigned long lastUptimeReport = 0;
const unsigned long heartbeatInterval = 30000;
const unsigned long uptimeInterval = 30000;

void checkForUpdates(void) {
  String checkUrl = String(urlBase);
  checkUrl += "?ver=" + String(VERSION) + "&dev=" + String(APP);
  Serial.println("INFO: Checking for updates at URL: " + checkUrl);
  WiFiClient client;
  t_httpUpdate_return ret = httpUpdate.update(client, checkUrl);
  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.println("ERROR: Update Failed - " + String(httpUpdate.getLastErrorString().c_str()));
      break;
    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("INFO: No Updates");
      break;
    case HTTP_UPDATE_OK:
      Serial.println("INFO: Update OK");
      break;
  }
}

void setup_wifi() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  WiFi.setAutoReconnect(true);
}

void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected. Reconnecting...");
    WiFi.disconnect();
    WiFi.begin(ssid, password);
  }
}

void publishState(int index, bool state) {
  if (!client.connected()) return;
  String topic = "homeassistant/switch/" + String(device_name) + "/usb" + String(index) + "/state";
  client.publish(topic.c_str(), state ? "ON" : "OFF", true);
}

void setUsbState(int index, bool state) {
  if (index < 0 || index >= 6) return;
  digitalWrite(usbPins[index], state ? HIGH : LOW);
  publishState(index, state);
}
unsigned long lastMillis = 0;
unsigned long totalSeconds = 0;

String formatUptime(unsigned long seconds) {
  unsigned long days = seconds / 86400;
  seconds %= 86400;
  unsigned long hours = seconds / 3600;
  seconds %= 3600;
  unsigned long minutes = seconds / 60;
  seconds %= 60;

  String result = "";
  if (days > 0) result += String(days) + "d ";
  if (hours > 0 || days > 0) result += String(hours) + "h ";
  if (minutes > 0 || hours > 0 || days > 0) result += String(minutes) + "m ";
  result += String(seconds) + "s";

  return result;
}

void updateUptime() {
  unsigned long now = millis();
  if (now - lastMillis >= 1000) {
    unsigned long elapsed = now - lastMillis;
    totalSeconds += elapsed / 1000;
    lastMillis = now - (elapsed % 1000);  // 保留小于1秒的余数
  }
}

void publishUptime() {
  updateUptime();
  String uptimeStr = formatUptime(totalSeconds);

  String topic = "homeassistant/sensor/" + String(device_name) + "/uptime/state";
  client.publish(topic.c_str(), uptimeStr.c_str());
}

void publishAutoDiscovery() {
  for (int i = 0; i < 6; i++) {
    String object_id = "usb" + String(i);
    String base_topic = "homeassistant/switch/" + String(device_name) + "/" + object_id;
    String config_topic = base_topic + "/config";
    String payload = "{";
    payload += "\"name\": \"" + String(device_name) + "_USB" + String(i) + "\",";
    payload += "\"uniq_id\": \"" + String(device_name) + "_usb" + String(i) + "\",";
    payload += "\"object_id\": \"" + object_id + "\",";
    payload += "\"~\": \"" + base_topic + "\",";
    payload += "\"cmd_t\": \"~/set\",";
    payload += "\"stat_t\": \"~/state\",";
    payload += "\"avty_t\": \"" + String(availability_topic) + "\",";
    payload += "\"pl_on\": \"ON\",";
    payload += "\"pl_off\": \"OFF\",";
    payload += "\"pl_avail\": \"online\",";
    payload += "\"pl_not_avail\": \"offline\",";
    payload += "\"qos\": 0,";
    payload += "\"device\": {\"identifiers\": [\"" + String(device_name) + "\"], \"name\": \"" + String(device_name) + "\", \"sw_version\": \"" VERSION "\"}";
    payload += "}";
    client.publish(config_topic.c_str(), payload.c_str(), true);
  }

  String restart_topic = "homeassistant/button/" + String(device_name) + "/restart/config";
  String restart_payload = "{";
  restart_payload += "\"name\": \"" + String(device_name) + "_Restart\",";
  restart_payload += "\"uniq_id\": \"" + String(device_name) + "_restart\",";
  restart_payload += "\"cmd_t\": \"homeassistant/button/" + String(device_name) + "/restart/set\",";
  restart_payload += "\"avty_t\": \"" + String(availability_topic) + "\",";
  restart_payload += "\"pl_avail\": \"online\",";
  restart_payload += "\"pl_not_avail\": \"offline\",";
  restart_payload += "\"device\": {\"identifiers\": [\"" + String(device_name) + "\"], \"name\": \"" + String(device_name) + "\", \"sw_version\": \"" VERSION "\"}";
  restart_payload += "}";
  client.publish(restart_topic.c_str(), restart_payload.c_str(), true);
  client.subscribe(("homeassistant/button/" + String(device_name) + "/restart/set").c_str());

  // uptime 传感器
  String uptime_config_topic = "homeassistant/sensor/" + String(device_name) + "/uptime/config";
  String uptime_payload = "{";
  uptime_payload += "\"name\": \"Uptime\",";
  uptime_payload += "\"uniq_id\": \"" + String(device_name) + "_uptime\",";
  uptime_payload += "\"object_id\": \"smartusbpower_uptime\",";
  uptime_payload += "\"stat_t\": \"homeassistant/sensor/" + String(device_name) + "/uptime/state\",";
  uptime_payload += "\"avty_t\": \"" + String(availability_topic) + "\",";
  uptime_payload += "\"pl_avail\": \"online\",";
  uptime_payload += "\"pl_not_avail\": \"offline\",";
  uptime_payload += "\"icon\": \"mdi:timer\",";
  uptime_payload += "\"device\": {\"identifiers\": [\"" + String(device_name) + "\"], \"name\": \"" + String(device_name) + "\", \"sw_version\": \"" VERSION "\"}";
  uptime_payload += "}";
  client.publish(uptime_config_topic.c_str(), uptime_payload.c_str(), true);
}

void callback(char* topic, byte* payload, unsigned int length) {
  String msgTopic = topic;
  String msgPayload = "";
  for (unsigned int i = 0; i < length; i++) msgPayload += (char)payload[i];

  for (int i = 0; i < 6; i++) {
    String expectedTopic = "homeassistant/switch/" + String(device_name) + "/usb" + String(i) + "/set";
    if (msgTopic == expectedTopic) {
      bool newState = (msgPayload == "ON");
      setUsbState(i, newState);
    }
  }

  if (msgTopic == "homeassistant/button/" + String(device_name) + "/restart/set" && msgPayload == "PRESS") {
    client.publish(availability_topic, "offline", true);
    delay(100);
    ESP.restart();
  }
}

void reconnect() {
  if (millis() - lastReconnectAttempt < 5000) return;
  lastReconnectAttempt = millis();

  if (WiFi.status() != WL_CONNECTED) {
    checkWiFiConnection();
    return;
  }
  client.setKeepAlive(60);  // 设置 keepalive 为 60 秒
  if (client.connect(device_name, mqtt_user, mqtt_password, availability_topic, 1, true, "offline")) {
    Serial.println("MQTT Connected");
    client.publish(availability_topic, "online", true);
    for (int i = 0; i < 6; i++) {
      String topic = "homeassistant/switch/" + String(device_name) + "/usb" + String(i) + "/set";
      client.subscribe(topic.c_str());
    }
    publishAutoDiscovery();
  // 重新发布当前物理状态（用 digitalRead）
  for (int i = 0; i < 6; i++) {
    bool currentState = digitalRead(usbPins[i]) == HIGH;
    publishState(i, currentState);
  }    
  } else {
    Serial.printf("MQTT connection failed, rc=%d\n", client.state());
  }
  yield();
}

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < 6; i++) {
    digitalWrite(usbPins[i], LOW);
    pinMode(usbPins[i], OUTPUT);
  }
  setup_wifi();
  checkForUpdates();
  Serial.println(mqtt_server);
  Serial.println(mqtt_port);
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  client.setBufferSize(2048);
  reconnect();
  //for (int i = 0; i < 6; i++) setUsbState(i, false);
}

void loop() {
  if (millis() - lastWiFiCheck > 30000) {
    lastWiFiCheck = millis();
    checkWiFiConnection();
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected()) reconnect();
    else client.loop();
  }

  if (millis() - lastHeartbeat > heartbeatInterval) {
    lastHeartbeat = millis();
    if (client.connected()) client.publish(availability_topic, "online", true);
  }

  if (millis() - lastUptimeReport > uptimeInterval) {
    lastUptimeReport = millis();
    if (client.connected()) publishUptime();
  }

  yield();
}
