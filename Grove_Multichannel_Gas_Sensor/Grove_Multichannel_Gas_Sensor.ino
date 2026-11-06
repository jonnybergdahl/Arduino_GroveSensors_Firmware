//---------------------------------------------------------------------------------------------
//
// Sketch for connecting the SEEED Studios Grove Multichannel Gas Sensor v1.0 
// to a MQTT server.
//
// Requires a development board compatible with the WiFiClient.
//
// https://wiki.seeedstudio.com/Grove-Multichannel_Gas_Sensor/
//
// Jonny Bergdahl, 2020-06-12
//
//---------------------------------------------------------------------------------------------
//
// Includes
//
#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>                   // https://github.com/knolleary/pubsubclient
#include "MutichannelGasSensor.h"           // https://github.com/Seeed-Studio/Mutichannel_Gas_Sensor
#include "secrets.h"

//---------------------------------------------------------------------------------------------
//
// Defines
//
#define MCGS_I2C_ADDRESS 0x04

#define MQTT_PUBLISH_INTERVAL 60000          // Time interval for MQTT publish in milliseconds
#define MQTT_CLIENT_NAME "MCGS"

#define NH3_TOPIC "gassensor1/nh3"          // MQTT topic for NH3 (Ammonia) value
#define CO_TOPIC "gassensor1/co"            // MQTT topic for CO (Carbon Monoxide) value
#define NO_TOPIC "gassensor1/no"            // MQTT topic for NO (Nitric oxide) value
#define C3H8_TOPIC "gassensor1/c3h8"        // MQTT topic for C3H8 (Propane) value
#define C4H10_TOPIC "gassensor1/c4h10"      // MQTT topic for C4H10 (Butane) value
#define CH4_TOPIC "gassensor1/ch4"          // MQTT topic for CH4 (Methane) value
#define H2_TOPIC "gassensor1/h2"            // MQTT topic for H2 (Hydrogen) value
#define C2H50H_TOPIC "gassensor1/c2h5oh"    // MQTT topic for C2H5OH (Ethanol) value

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
uint64_t lastPublish = 0;

void setup() {
  Serial.begin(115200);  // start serial for output
delay(2000);
  Wire.begin(4, 5);
  Wire.setTimeout(100);
byte error, address;
  int nDevices;
  Serial.println("Scanning...");
  nDevices = 0;
  for(address = 1; address < 10; address++ ) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    Serial.print(address);
    Serial.print(", ");
    Serial.println(error);
    if (error == 0) {
      Serial.print("I2C device found at address 0x");
      if (address<16) {
        Serial.print("0");
      }
      Serial.println(address,HEX);
      nDevices++;
    }
    else if (error==4) {
      Serial.print("Unknow error at address 0x");
      if (address<16) {
        Serial.print("0");
      }
      Serial.println(address,HEX);
    }    
  }
  if (nDevices == 0) {
    Serial.println("No I2C devices found\n");
  }
  else {
    Serial.println("done\n");
  }

  Serial.println("Gas sensor client");
  setup_wifi();
  mqttClient.setServer(MQTT_SERVER, 1883);
  
  Serial.println("Setting up gas sensor");
  gas.begin(MCGS_I2C_ADDRESS);//the default I2C address of the slave is 0x04

  Serial.print("Gas sensor firmware version: ");
  Serial.println(gas.getVersion());
  gas.powerOn();
}

void loop() 
{
  float c;
  Serial.println("loop");
  if (!mqttClient.connected()) 
  {
    reconnect();
  }
  mqttClient.loop();      // Required for the MQTT client do it's job

  if (millis() - lastPublish >= MQTT_PUBLISH_INTERVAL) 
  {
    lastPublish += MQTT_PUBLISH_INTERVAL;

    Serial.println("Publishing values to MQTT:");
    c = gas.measure_NH3();
    publish(NH3_TOPIC, c);
    c = gas.measure_CO();
    publish(CO_TOPIC, c);
    c = gas.measure_NO2();
    publish(NO_TOPIC, c);
    c = gas.measure_C3H8();
    publish(C3H8_TOPIC, c);
    c = gas.measure_C4H10();
    publish(C4H10_TOPIC, c);
    c = gas.measure_CH4();
    publish(CH4_TOPIC, c);
    c = gas.measure_H2();
    publish(H2_TOPIC, c);
    c = gas.measure_C2H5OH();
    publish(C2H50H_TOPIC, c);
  }
}

void publish(char* topic, float value)
{
  char buffer[32];
  sprintf(buffer, "%0.2f", value);      // Format value with 2 decimals
  
  Serial.print(topic);
  Serial.print(": ");
  Serial.print(buffer);
  Serial.println(" ppm"); 
  mqttClient.publish(topic, buffer, true);
}


void setup_wifi() 
{
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("Got IP address: ");
  Serial.println(wifiClient.localIP());
}

void reconnect() {
  // Loop until we're reconnected
  while (!mqttClient.connected()) 
  {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    // If you do not want to use a username and password, change next line to
    // if (client.connect("MQTT_CLIENT_NAME")) {
    if (mqttClient.connect(MQTT_CLIENT_NAME, MQTT_USER, MQTT_PASSWORD)) 
    {
      Serial.println("connected");
    } 
    else 
    {
      Serial.print("failed, state=");
      Serial.print(mqttClient.state());
      Serial.println(" trying again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}
