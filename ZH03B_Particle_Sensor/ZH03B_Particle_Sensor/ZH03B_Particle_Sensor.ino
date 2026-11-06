//---------------------------------------------------------------------------------------------
//
// Firmware for the TinyPICO connected to a ZH03B particle sensor  
//
// Copyright 2021-2023, Bergdahl IT AB
// Written by Jonny Bergdahl, 2021-09-12
//
// Licensed under the MIT license, see the LICENSE.txt file.
//
// Hookup guide:
//  ZH03B pin 1 VDD -> TinyPICO pin 5V
//  ZH03B pin 2 GND -> TinyPICO pin GND
//  ZH03B pin 4 RXD -> TinyPICO pin 25
//  ZH03B pin 5 TXD -> TinyOICO pin 26
//
// Dependencies:
//  SD_ZH03B        https://github.com/ShaggyDog18/SD_ZH03B
//  PubSubClient    https://github.com/knolleary/pubsubclient
//
//---------------------------------------------------------------------------------------------
#include "WiFi.h"
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <SD_ZH03B.h>
#include "rom/rtc.h"
#include "secrets.h"

#define FIRMWARE_PREFIX  "particlesensor" 
#define AVAILABILITY_ONLINE "online"
#define AVAILABILITY_OFFLINE "offline"
#define ZH03B_RX_PIN  26
#define ZH03B_TX_PIN  25

struct zh03bdata_t 
{  
  uint16_t concPM1_0;
  uint16_t concPM2_5;
  uint16_t concPM10_0;
  uint8_t error;
};

uint32_t getChipId();
void printResetReason();
zh03bdata_t readSensorData();

SD_ZH03B zh03b(Serial2);
PubSubClient mqttClient;
char mqttNodeName[24];
char mqttTopicAvailability[128];
char mqttTopicState[128];
char mqttTopicCommand[128];
char MqttTopicAutoConfPm01Sensor[128];
char MqttTopicAutoConfPm25Sensor[128];
char MqttTopicAutoConfPm10Sensor[128];
char MqttTopicAutoconfWifiSensor[128];

uint64_t currentMillis = 0;
uint32_t lastMqttConnectionAttempt = 0;
const uint16_t mqttConnectionInterval = 60000; // 1 minute = 60 seconds = 60000 milliseconds
uint32_t statusPublishPreviousMillis = 0;
const uint16_t statusPublishInterval = 30000; // 30 seconds = 30000 milliseconds

////////////////////////////////////////////////////////////////////////////////////////////////
//
// WiFi support methods
//
void setupWifi()
{
  Serial.println("----------------------------------------");
  Serial.println("Connecting to WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
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
    snprintf(mqttNodeName, sizeof(mqttNodeName), "particlesensor-%X", getChipId());
    Serial.print("Node id:");
    Serial.println(mqttNodeName);
    snprintf(mqttTopicAvailability, 127, "%s/%s/status", FIRMWARE_PREFIX, mqttNodeName);
    snprintf(mqttTopicState, 127, "%s/%s/state", FIRMWARE_PREFIX, mqttNodeName);
    snprintf(mqttTopicCommand, 127, "%s/%s/command", FIRMWARE_PREFIX, mqttNodeName);
    snprintf(MqttTopicAutoConfPm01Sensor, 127, "homeassistant/sensor/%s/%s_pm01/config", FIRMWARE_PREFIX, mqttNodeName);
    snprintf(MqttTopicAutoConfPm25Sensor, 127, "homeassistant/sensor/%s/%s_pm25/config", FIRMWARE_PREFIX, mqttNodeName);
    snprintf(MqttTopicAutoConfPm10Sensor, 127, "homeassistant/sensor/%s/%s_pm10/config", FIRMWARE_PREFIX, mqttNodeName);
    snprintf(MqttTopicAutoconfWifiSensor, 127, "homeassistant/sensor/%s/%s_wifi/config", FIRMWARE_PREFIX, mqttNodeName);
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
        printf("Connect to MQTT, try %i\n", attempt);
        if (mqttClient.connect(mqttNodeName, MQTT_USER_NAME, MQTT_PASSWORD, mqttTopicAvailability, 1, true, AVAILABILITY_OFFLINE)) 
        {
            printf("MQTT connected\n");
            mqttClient.publish(mqttTopicAvailability, AVAILABILITY_ONLINE, true);
            printf("Publish autoconfig\n");
            publishAutoConfig();

            // Make sure to subscribe after polling the status so that we never execute commands with the default data
            mqttClient.subscribe(mqttTopicCommand);
            break;
        }
        delay(5000);
    }

}

void publishState(zh03bdata_t data)
{
  DynamicJsonDocument wifiJson(192);
  DynamicJsonDocument stateJson(604);
  char payload[256];

  Serial.printf("Publish MQTT packet for %s\n", &mqttTopicState);
  Serial.printf("PM1.0=%d, PM2.5=%d, PM10=%d\n", data.concPM1_0, data.concPM2_5. data.concPM10_0);

  wifiJson["ssid"] = WiFi.SSID();
  wifiJson["ip"] = WiFi.localIP().toString();
  wifiJson["rssi"] = WiFi.RSSI();

  stateJson["pm01"] = data.concPM1_0;
  stateJson["pm25"] = data.concPM2_5;
  stateJson["pm10"] = data.concPM10_0;

  stateJson["wifi"] = wifiJson.as<JsonObject>();

  serializeJson(stateJson, payload);
  mqttClient.publish(&mqttTopicState, &payload, true);
}

void publishAutoDiscovery()
{
  // <discovery_prefix>/<component>/<object_id>/config
  char discoveryTopic[127];
  char payload[2048];
  char name[32];
  char uniqueId[32];

  DynamicJsonDocument device(256);
  DynamicJsonDocument config(1024);
  StaticJsonDocument<64> identifiersDoc;

  JsonArray identifiers = identifiersDoc.to<JsonArray>();

  identifiers.add(nodeId);
  device["identifiers"] = identifiers;
  device["manufacturer"] = HA_MANUFACTURER;
  device["model"] = HA_MODEL;
  device["name"] = nodeId;
  device["sw_version"] = FIRMWARE_VERSION;

  Serial.print("crap - ");
  Serial.println(availabilityTopic);
  // WiFi config
  snprintf(discoveryTopic, 127, "%s/%s/%s_wifi/config", HA_DISCOVERY_PREFIX, HA_COMPONENT, nodeId);
  snprintf(name, 32, "%s WiFi", nodeId);
  snprintf(uniqueId, 32, "%s_wifi", nodeId);
  config["identifiers"] = device.as<JsonObject>();
  config["availability_topic"] = availabilityTopic;
  config["state_topic"] = stateTopic;
  config["name"] = name;
  config["device_class"] = "signal_strength";
  config["value_template"] = "{{value_json.wifi.rssi}}";
  config["unique_id"] = uniqueId;
  config["unit_of_measurement"] = "dBm";
  config["json_attributes_topic"] = stateTopic;
  config["json_attributes_template"] = "{\"ssid\": \"{{value_json.wifi.ssid}}\", \"ip\": \"{{value_json.wifi.ip}}\"}";
  config["icon"] = "mdi:wifi";
  serializeJson(config, payload);
  Serial.println(discoveryTopic);
  Serial.println(payload);

  // PM1 config
  config.clear();
  snprintf(discoveryTopic, 127, "%s/%s/%s_pm1/config", HA_DISCOVERY_PREFIX, HA_COMPONENT, nodeId);
  snprintf(name, 32, "%s PM1", nodeId);
  snprintf(uniqueId, 32, "%s_pm1", nodeId);
  config["identifiers"] = device.as<JsonObject>();
  config["availability_topic"] = availabilityTopic;
  config["state_topic"] = stateTopic;
  config["name"] = name;
  config["device_class"] = "pm1";
  config["value_template"] = "{{value_json.pm1}}";
  config["unique_id"] = uniqueId;
  config["unit_of_measurement"] = "µg/m³";
  config["icon"] = "mdi:air-filter";
  serializeJson(config, payload);
  Serial.println(discoveryTopic);
  Serial.println(payload);

/*


  config["device_class"] = "pm1";
  config["name"] = "pm01";
  config["state_topic"] = stateTopic;
  config["unit_of_measurement"] = "µg/m³";
  config["value_template"] = "{{ value_json.pm1}}";
  serializeJson(stateJson, json);
  mqttClient.publish(discoveryTopic, stateJson, true);

  // PM25
  snprintf(discoveryTopic, 127, "%s/%s/%s25/config", HA_DISCOVERY_PREFIX, HA_COMPONENT, haUniqueId);
  json.clear();
  payload["device_class"] = "pm25";
  payload["name"] = "pm25";
  payload["state_topic"] = stateTopic;
  payload["unit_of_measurement"] = "µg/m³";
  payload["value_template"] = "{{ value_json.pm25}}";
  serializeJson(stateJson, json);
  mqttClient.publish(discoveryTopic, stateJson, true);

  // PM10
  snprintf(discoveryTopic, 127, "%s/%s/%s10/config", HA_DISCOVERY_PREFIX, HA_COMPONENT, haUniqueId);
  json.clear();
  payload["device_class"] = "pm10";
  payload["name"] = "pm10";
  payload["state_topic"] = stateTopic;
  payload["unit_of_measurement"] = "µg/m³";
  payload["value_template"] = "{{ value_json.pm10}}";
  serializeJson(stateJson, json);
  mqttClient.publish(discoveryTopic, stateJson, true);

// {"device_class": "temperature", "name": "Temperature", "state_topic": "homeassistant/sensor/sensorBedroom/state", "unit_of_measurement": "°C", "value_template": "{{ value_json.temperature}}" }

  
  Configuration topic no1: homeassistant/sensor/sensorBedroomT/config
Configuration payload no1: {"device_class": "temperature", "name": "Temperature", "state_topic": "homeassistant/sensor/sensorBedroom/state", "unit_of_measurement": "°C", "value_template": "{{ value_json.temperature}}" }
Configuration topic no2: homeassistant/sensor/sensorBedroomH/config
Configuration payload no2: {"device_class": "humidity", "name": "Humidity", "state_topic": "homeassistant/sensor/sensorBedroom/state", "unit_of_measurement": "%", "value_template": "{{ value_json.humidity}}" }
Common state payload: { "temperature": 23.20, "humidity": 43.70 }
  */

/*
{
  "name": "Front Door",
  "device_class": "door",
  "state_topic": "premise/frontdoor/sensor/dooropened",
  "unique_id": "elk_m1_front_door_contact_sensor",
  "payload_on": "1",
  "payload_off": "0",
  "device": {
      "identifiers": ["elk_m1_security_panel"],
      "name": "ELK M1",
      "model": "M1 Gold",
      "manufacturer": "ELK",
      "sw_version": "4.XX"
  }
}

{
    "device": {
        "identifiers": [
            "VINDRIKTNING-1B8E90"
        ],
        "manufacturer": "Ikea",
        "model": "VINDRIKTNING",
        "name": "VINDRIKTNING-1B8E90",
        "sw_version": "2021.08.0"
    },
    "availability_topic": "esp8266-vindriktning-particle-sensor/VINDRIKTNING-1B8E90/status",
    "state_topic": "esp8266-vindriktning-particle-sensor/VINDRIKTNING-1B8E90/state",
    "name": "VINDRIKTNING-1B8E90 WiFi",
    "value_template": "{{value_json.wifi.rssi}}",
    "unique_id": "VINDRIKTNING-1B8E90_wifi",
    "unit_of_measurement": "dBm",
    "json_attributes_topic": "esp8266-vindriktning-particle-sensor/VINDRIKTNING-1B8E90/state",
    "json_attributes_template": "{\"ssid\": \"{{value_json.wifi.ssid}}\", \"ip\": \"{{value_json.wifi.ip}}\"}",
    "icon": "mdi:wifi"
}

{
    "device": {
        "identifiers": [
            "VINDRIKTNING-1B8E90"
        ],
        "manufacturer": "Ikea",
        "model": "VINDRIKTNING",
        "name": "VINDRIKTNING-1B8E90",
        "sw_version": "2021.08.0"
    },
    "availability_topic": "esp8266-vindriktning-particle-sensor/VINDRIKTNING-1B8E90/status",
    "state_topic": "esp8266-vindriktning-particle-sensor/VINDRIKTNING-1B8E90/state",
    "name": "VINDRIKTNING-1B8E90 PM 2.5",
    "unit_of_measurement": "μg/m³",
    "value_template": "{{value_json.pm25}}",
    "unique_id": "VINDRIKTNING-1B8E90_pm25",
    "icon": "mdi:air-filter"
}
*/

}

void mqttCallback(char* topic, uint8_t* payload, unsigned int length) 
{ 
  // Nothing to do
}

////////////////////////////////////////////////////////////////////////////////////////////////
//
// Support methods
//

zh03bdata_t readSensorData() 
{
  zh03bdata_t result;
  uint8_t response;
   
  if (response = zh03b.readData()) 
  {
    result.concPM1_0 = zh03b.getPM1_0();
    result.concPM2_5 = zh03b.getPM2_5();
    result.concPM10_0 = zh03b.getPM10_0();
  }
  else 
  {
    result.error = response;
  } 
  return result;
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
    Serial.printf("SDK Version: %s\n", ESP.getSdkVersion());
    Serial.printf("CPU Frequency: %u MHz\n", ESP.getCpuFreqMHz());
    Serial.print("Reset reason: ");
    printResetReason();
    setupWifi();
    setupMqtt();

    Serial.println("Initializing ZH03B");
    Serial2.begin(9600, SERIAL_8N1, ZH03B_TX_PIN, ZH03B_RX_PIN);
    delay(200);
    zh03b.setMode(SD_ZH03B::QA_MODE);
}

////////////////////////////////////////////////////////////////////////////////////////////////
//
// Loop
//
void loop () 
{
  mqttClient.loop();

  if (!mqttClient.connected() && currentMillis - lastMqttConnectionAttempt >= mqttConnectionInterval) {
      lastMqttConnectionAttempt = currentMillis;
      printf("Reconnect mqtt\n");
      reconnectMmqtt();
  }

  const uint32_t currentMillis = millis();
  if (currentMillis - statusPublishPreviousMillis >= statusPublishInterval) {
    statusPublishPreviousMillis = currentMillis;

    zh03bdata_t data = readSensorData();
    if (data.error == 0)
    {
      publishState(data);
    }
    else
    {
      Serial.printf("Error reading data: %d", data.error);
    }
  }
}