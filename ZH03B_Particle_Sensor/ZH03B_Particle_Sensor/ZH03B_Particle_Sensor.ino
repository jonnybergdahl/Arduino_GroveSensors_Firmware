//---------------------------------------------------------------------------------------------
//
// Firmware for the TinyPICO connected to a ZH03B particle sensor
//
// Copyright 2021, Bergdahl IT AB
// Written by Jonny Bergdahl, 2021-09-12
//
// Licensed under the MIT license, see the LICENSE.txt file.
//
// Hookup guide:
//  ZH03B pin 1 VDD -> TinyPICO pin 5V
//  ZH03B pin 2 GND -> TinyPICO pin GND
//  ZH03B pin 4 RXD -> TinyPICO pin 21 (TX)
//  ZH03B pin 5 TXD -> TinyPICO pin 22 (RX)
//
// Dependencies:
//  SD_ZH03B        https://github.com/ShaggyDog18/SD_ZH03B
//  PubSubClient    https://github.com/knolleary/pubsubclient
//  ArduinoJson     https://arduinojson.org/ (v7)
//
//---------------------------------------------------------------------------------------------
#include "WiFi.h"
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <SD_ZH03B.h>
#include "rom/rtc.h"
#include "secrets.h"

//---------------------------------------------------------------------------------------------
// Firmware defines
//
#define FIRMWARE_VERSION "0.9.0"

//---------------------------------------------------------------------------------------------
// Home Assistant MQTT defines
//
#define HA_NODE_NAME "TinyPICO"               // Node name
#define HA_NODE_ID "tp"                       // Node name short form
#define HA_SENSOR_ID "ZH03B"                  // Sensor model or type
#define HA_DISCOVERY_PREFIX "homeassistant"   // As defined in Home Assistant MQTT settings
#define HA_COMPONENT  "sensor"                // Home Assistant sensor type

//---------------------------------------------------------------------------------------------
// Sensor specific defines
//
#define ZH03B_RX_PIN  21                      // ZH03B RXD, connected to TinyPICO TX
#define ZH03B_TX_PIN  22                      // ZH03B TXD, connected to TinyPICO RX

//---------------------------------------------------------------------------------------------
// Internal defines
//
#define HA_UNIQUE_ID_PREFIX HA_NODE_ID "_" HA_SENSOR_ID
#define HA_MANUFACTURER "Bergdahl IT AB"
#define HA_MODEL HA_NODE_NAME " " HA_SENSOR_ID " " HA_COMPONENT
#define AVAILABILITY_ONLINE "online"
#define AVAILABILITY_OFFLINE "offline"

//---------------------------------------------------------------------------------------------
// Structs
//
struct zh03bdata_t
{
  uint16_t concPM1_0;
  uint16_t concPM2_5;
  uint16_t concPM10_0;
  uint8_t error;
};

//---------------------------------------------------------------------------------------------
// Global variables
//
SD_ZH03B zh03b(Serial2);
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
char nodeId[32];                      // NodeId is used as name and unique identifier in MQTT messages
char availabilityTopic[128];          // Home Assistant MQTT availability topic
char stateTopic[128];                 // Home Assistant MQTT state topic
char commandTopic[128];
uint32_t lastMqttConnectionAttempt = 0;
const uint32_t mqttConnectionInterval = 60000;  // 1 minute = 60 seconds = 60000 milliseconds
uint32_t statusPublishPreviousMillis = 0;
const uint32_t statusPublishInterval = 30000;   // 30 seconds = 30000 milliseconds

////////////////////////////////////////////////////////////////////////////////////////////////
//
// Setup
//
void setup()
{
    Serial.begin(115200);
    Serial.println();
    Serial.println("----------------------------------------");
    Serial.println("TinyPICO ZH03B sensor");
    Serial.println("----------------------------------------");
    Serial.printf("Firmware version: %s\n", FIRMWARE_VERSION);
    Serial.printf("SDK Version: %s\n", ESP.getSdkVersion());
    Serial.printf("CPU Frequency: %u MHz\n", ESP.getCpuFreqMHz());
    Serial.print("Reset reason: ");
    printResetReason();
    createGlobalIdentifiers();
    Serial.println("----------------------------------------");
    Serial.println("Initializing ZH03B");
    setupSensor();
    setupWifi();
    setupMqtt();
}

////////////////////////////////////////////////////////////////////////////////////////////////
//
// Loop
//
void loop ()
{
  uint32_t currentMillis = millis();
  mqttClient.loop();

  if (!mqttClient.connected() && currentMillis - lastMqttConnectionAttempt >= mqttConnectionInterval) {
      lastMqttConnectionAttempt = currentMillis;
      Serial.println("Reconnect MQTT");
      reconnectMqtt();
  }

  if (currentMillis - statusPublishPreviousMillis >= statusPublishInterval) {
    statusPublishPreviousMillis = currentMillis;
    zh03bdata_t data = readSensorData();
    if (data.error == 0)
    {
      Serial.printf("PM1.0=%u, PM2.5=%u, PM10=%u\n", data.concPM1_0, data.concPM2_5, data.concPM10_0);
      publishState(data);
    }
    else
    {
      Serial.printf("Error reading data: %u\n", data.error);
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////
//
// Sensor support methods
//
void setupSensor()
{
    // Serial2.begin(baud, config, rxPin, txPin): TinyPICO RX is the sensor's TXD and vice versa
    Serial2.begin(9600, SERIAL_8N1, ZH03B_TX_PIN, ZH03B_RX_PIN);
    delay(200);
    zh03b.setMode(SD_ZH03B::QA_MODE);
}

zh03bdata_t readSensorData()
{
  zh03bdata_t result = {0, 0, 0, 0};

  if (zh03b.readData())
  {
    result.concPM1_0 = zh03b.getPM1_0();
    result.concPM2_5 = zh03b.getPM2_5();
    result.concPM10_0 = zh03b.getPM10_0();
  }
  else
  {
    result.error = 1;     // No valid data frame from the sensor
  }
  return result;
}

////////////////////////////////////////////////////////////////////////////////////////////////
//
// WiFi support methods
//
void setupWifi()
{
  Serial.println("----------------------------------------");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println();
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

////////////////////////////////////////////////////////////////////////////////////////////////
//
// MQTT support methods
//
void setupMqtt()
{
    Serial.println("----------------------------------------");
    Serial.println("Connecting to MQTT");
    mqttClient.setServer(MQTT_SERVER, 1883);
    mqttClient.setKeepAlive(10);
    mqttClient.setBufferSize(2048);
    mqttClient.setCallback(mqttCallback);
    reconnectMqtt();
}

void reconnectMqtt()
{
    for (uint8_t attempt = 0; attempt < 3; ++attempt) {
        Serial.printf("Connect to MQTT, try %u\n", attempt + 1);
        if (mqttClient.connect(nodeId, MQTT_USER_NAME, MQTT_PASSWORD, availabilityTopic, 1, true, AVAILABILITY_OFFLINE))
        {
            Serial.println("MQTT connected");
            mqttClient.publish(availabilityTopic, AVAILABILITY_ONLINE, true);
            Serial.println("Publish auto discovery");
            publishAutoDiscovery();

            // Make sure to subscribe after publishing discovery so that we never execute commands with the default data
            mqttClient.subscribe(commandTopic);
            return;
        }
        Serial.printf("MQTT connect failed, state=%d\n", mqttClient.state());
        delay(5000);
    }
}

void publishState(zh03bdata_t data)
{
  if (!mqttClient.connected())
  {
    return;
  }

  JsonDocument stateJson;
  char payload[256];

  stateJson["pm01"] = data.concPM1_0;
  stateJson["pm25"] = data.concPM2_5;
  stateJson["pm10"] = data.concPM10_0;

  JsonObject wifiJson = stateJson["wifi"].to<JsonObject>();
  wifiJson["ssid"] = WiFi.SSID();
  wifiJson["ip"] = WiFi.localIP().toString();
  wifiJson["rssi"] = WiFi.RSSI();

  serializeJson(stateJson, payload, sizeof(payload));
  Serial.printf("Publish %s: %s\n", stateTopic, payload);
  mqttClient.publish(stateTopic, payload, true);
}

// Publish one Home Assistant MQTT discovery config message
// <discovery_prefix>/<component>/<object_id>/config
void publishDiscoveryConfig(const char* suffix, const char* friendlyName, const char* deviceClass,
                            const char* unit, const char* valueTemplate, const char* icon, bool wifiAttributes)
{
  char discoveryTopic[128];
  char name[48];
  char uniqueId[48];
  char payload[1024];

  snprintf(discoveryTopic, sizeof(discoveryTopic), "%s/%s/%s_%s/config", HA_DISCOVERY_PREFIX, HA_COMPONENT, nodeId, suffix);
  snprintf(name, sizeof(name), "%s %s", nodeId, friendlyName);
  snprintf(uniqueId, sizeof(uniqueId), "%s_%s", nodeId, suffix);

  JsonDocument config;
  JsonObject device = config["device"].to<JsonObject>();
  device["identifiers"].to<JsonArray>().add(nodeId);
  device["manufacturer"] = HA_MANUFACTURER;
  device["model"] = HA_MODEL;
  device["name"] = nodeId;
  device["sw_version"] = FIRMWARE_VERSION;

  config["availability_topic"] = availabilityTopic;
  config["state_topic"] = stateTopic;
  config["name"] = name;
  config["unique_id"] = uniqueId;
  config["device_class"] = deviceClass;
  config["state_class"] = "measurement";
  config["unit_of_measurement"] = unit;
  config["value_template"] = valueTemplate;
  config["icon"] = icon;
  if (wifiAttributes)
  {
    config["entity_category"] = "diagnostic";
    config["json_attributes_topic"] = stateTopic;
    config["json_attributes_template"] = "{\"ssid\": \"{{value_json.wifi.ssid}}\", \"ip\": \"{{value_json.wifi.ip}}\"}";
  }

  serializeJson(config, payload, sizeof(payload));
  Serial.println(discoveryTopic);
  Serial.println(payload);
  mqttClient.publish(discoveryTopic, payload, true);
}

void publishAutoDiscovery()
{
  publishDiscoveryConfig("wifi", "WiFi", "signal_strength", "dBm", "{{value_json.wifi.rssi}}", "mdi:wifi", true);
  publishDiscoveryConfig("pm1", "PM1", "pm1", "µg/m³", "{{value_json.pm01}}", "mdi:air-filter", false);
  publishDiscoveryConfig("pm25", "PM2.5", "pm25", "µg/m³", "{{value_json.pm25}}", "mdi:air-filter", false);
  publishDiscoveryConfig("pm10", "PM10", "pm10", "µg/m³", "{{value_json.pm10}}", "mdi:air-filter", false);
}

void mqttCallback(char* topic, uint8_t* payload, unsigned int length)
{
  // Nothing to do
}

////////////////////////////////////////////////////////////////////////////////////////////////
//
// Support methods
//
void createGlobalIdentifiers()
{
  snprintf(nodeId, sizeof(nodeId), "%s-%X", HA_UNIQUE_ID_PREFIX, getChipId());
  snprintf(stateTopic, sizeof(stateTopic), "%s/%s/%s/state", HA_DISCOVERY_PREFIX, HA_COMPONENT, nodeId);
  snprintf(availabilityTopic, sizeof(availabilityTopic), "%s/%s/%s/status", HA_DISCOVERY_PREFIX, HA_COMPONENT, nodeId);
  snprintf(commandTopic, sizeof(commandTopic), "%s/%s/%s/command", HA_DISCOVERY_PREFIX, HA_COMPONENT, nodeId);
  Serial.print("nodeId: ");
  Serial.println(nodeId);
  Serial.print("stateTopic: ");
  Serial.println(stateTopic);
  Serial.print("availabilityTopic: ");
  Serial.println(availabilityTopic);
}

uint32_t getChipId()
{
  uint32_t result = 0;
  for(int i=0; i<17; i=i+8)
  {
    result |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }
  return result;
}

void printResetReason()
{
  RESET_REASON reason = rtc_get_reset_reason(0);
  switch (reason)
  {
    case 1 : Serial.println ("POWERON_RESET");break;          /**<1,  Vbat power on reset*/
    case 3 : Serial.println ("SW_RESET");break;               /**<3,  Software reset digital core*/
    case 4 : Serial.println ("OWDT_RESET");break;             /**<4,  Legacy watch dog reset digital core*/
    case 5 : Serial.println ("DEEPSLEEP_RESET");break;        /**<5,  Deep Sleep reset digital core*/
    case 6 : Serial.println ("SDIO_RESET");break;             /**<6,  Reset by SLC module, reset digital core*/
    case 7 : Serial.println ("TG0WDT_SYS_RESET");break;       /**<7,  Timer Group0 Watch dog reset digital core*/
    case 8 : Serial.println ("TG1WDT_SYS_RESET");break;       /**<8,  Timer Group1 Watch dog reset digital core*/
    case 9 : Serial.println ("RTCWDT_SYS_RESET");break;       /**<9,  RTC Watch dog Reset digital core*/
    case 10 : Serial.println ("INTRUSION_RESET");break;       /**<10, Instrusion tested to reset CPU*/
    case 11 : Serial.println ("TGWDT_CPU_RESET");break;       /**<11, Time Group reset CPU*/
    case 12 : Serial.println ("SW_CPU_RESET");break;          /**<12, Software reset CPU*/
    case 13 : Serial.println ("RTCWDT_CPU_RESET");break;      /**<13, RTC Watch dog Reset CPU*/
    case 14 : Serial.println ("EXT_CPU_RESET");break;         /**<14, for APP CPU, reseted by PRO CPU*/
    case 15 : Serial.println ("RTCWDT_BROWN_OUT_RESET");break;/**<15, Reset when the vdd voltage is not stable*/
    case 16 : Serial.println ("RTCWDT_RTC_RESET");break;      /**<16, RTC Watch dog reset digital core and rtc module*/
    default : Serial.println ("NO_MEAN");
  }
}
