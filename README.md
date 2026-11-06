# Arduino_GroveSensors_Firmware

Arduino firmware for ESP32 boards (TinyPICO) that read air quality sensors and publish the values to an MQTT server.

| Sketch | Sensor | Interface | Status |
|--------|--------|-----------|--------|
| [Grove_CO2_Sensor](Grove_CO2_Sensor) | [Seeed Grove CO2 Sensor](https://wiki.seeedstudio.com/Grove-CO2_Sensor/) | UART | Work in progress |
| [Grove_Multichannel_Gas_Sensor](Grove_Multichannel_Gas_Sensor) | [Seeed Grove Multichannel Gas Sensor v1](https://wiki.seeedstudio.com/Grove-Multichannel_Gas_Sensor/) | I2C | Working, with debug output |
| [ZH03B_Particle_Sensor](ZH03B_Particle_Sensor) | Winsen ZH03B particle sensor | UART | Home Assistant MQTT discovery, compiles but not yet tested on hardware |

## Requirements

- An ESP32 board, such as the [TinyPICO](https://www.tinypico.com/), with the ESP32 core installed in the Arduino IDE.
- An MQTT server, for example the Mosquitto add-on in Home Assistant.
- The libraries listed for each sketch below, installed through the Arduino Library Manager.

## Configuration

Each sketch folder has a `secrets.h` file with placeholder values. Fill it in with your own settings before you build:

```c
#define WIFI_SSID "your-wifi-ssid"
#define WIFI_PASSWORD "your-wifi-password"

#define MQTT_SERVER "your-mqtt-server"     // Host name or IP address, port 1883 is used
#define MQTT_USER "your-mqtt-user"         // MQTT_USER_NAME in ZH03B_Particle_Sensor
#define MQTT_PASSWORD "your-mqtt-password"
```

Don't commit your real values. To keep Git from picking up local changes to the file, run:

```bash
git update-index --skip-worktree Grove_CO2_Sensor/secrets.h Grove_Multichannel_Gas_Sensor/secrets.h ZH03B_Particle_Sensor/ZH03B_Particle_Sensor/secrets.h
```

## Grove_CO2_Sensor

Reads CO2 concentration and temperature from the Grove CO2 sensor every 10 seconds.

**Wiring**

| Sensor | ESP32 |
|--------|-------|
| TX | GPIO 25 (RX) |
| RX | GPIO 26 (TX) |
| VCC | 5V |
| GND | GND |

**Libraries:** [PubSubClient](https://github.com/knolleary/pubsubclient)

**MQTT topics** (client name `co2sensor1`, values are retained)

| Topic | Value | Unit |
|-------|-------|------|
| `cosensor1/co` | CO2 | ppm |
| `cosensor1/no` | Temperature | °C |

## Grove_Multichannel_Gas_Sensor

Reads eight gas concentrations from the Multichannel Gas Sensor every 60 seconds. The sensor uses I2C address `0x04`, on GPIO 5 (SDA) and GPIO 4 (SCL), which is the Grove connector on [PCB_TinyPico_SensorBoard](https://github.com/jonnybergdahl/PCB_TinyPico_SensorBoard).

Seeed's [Mutichannel_Gas_Sensor](https://github.com/Seeed-Studio/Mutichannel_Gas_Sensor) library only supports AVR boards, so a modified copy of it is included in the sketch folder.

**Libraries:** [PubSubClient](https://github.com/knolleary/pubsubclient)

**MQTT topics** (client name `MCGS`, values in ppm with two decimals, retained)

| Topic | Gas |
|-------|-----|
| `gassensor1/nh3` | NH3, ammonia |
| `gassensor1/co` | CO, carbon monoxide |
| `gassensor1/no` | NO2, nitrogen dioxide |
| `gassensor1/c3h8` | C3H8, propane |
| `gassensor1/c4h10` | C4H10, butane |
| `gassensor1/ch4` | CH4, methane |
| `gassensor1/h2` | H2, hydrogen |
| `gassensor1/c2h5oh` | C2H5OH, ethanol |

At startup, the sketch scans the I2C bus and prints the devices it finds to the serial monitor (115200 baud).

## ZH03B_Particle_Sensor

Firmware for a TinyPICO connected to a ZH03B particle sensor. Every 30 seconds it reads PM1.0, PM2.5 and PM10 and publishes them as JSON, together with WiFi information. At connect it announces the sensors to Home Assistant through MQTT discovery, so they show up as a device without any YAML.

**Wiring**

The sensor uses UART on GPIO 21/22, which is the QWIIC/STEMMA QT connector on [PCB_TinyPico_SensorBoard](https://github.com/jonnybergdahl/PCB_TinyPico_SensorBoard). See [pinout.png](ZH03B_Particle_Sensor/pinout.png) for the sensor's connector.

| ZH03B | TinyPICO |
|-------|----------|
| Pin 1, VDD | 5V |
| Pin 2, GND | GND |
| Pin 4, RXD | GPIO 21 (TX) |
| Pin 5, TXD | GPIO 22 (RX) |

**Libraries:** [SD_ZH03B](https://github.com/ShaggyDog18/SD_ZH03B), [PubSubClient](https://github.com/knolleary/pubsubclient), [ArduinoJson](https://arduinojson.org/) v7

**Board:** select *UM TinyPICO* (`esp32:esp32:um_tinypico`) in the Arduino IDE.

**Home Assistant entities** (on a device named `<node>`, where `<node>` is `tp_ZH03B-<chip id>`)

| Entity | Device class | Unit |
|--------|--------------|------|
| `<node> PM1` | `pm1` | µg/m³ |
| `<node> PM2.5` | `pm25` | µg/m³ |
| `<node> PM10` | `pm10` | µg/m³ |
| `<node> WiFi` | `signal_strength` (diagnostic, with SSID and IP as attributes) | dBm |

**MQTT topics**

| Topic | Content |
|-------|---------|
| `homeassistant/sensor/<node>/status` | Availability, `online` or `offline` (last will), retained |
| `homeassistant/sensor/<node>/state` | JSON with `pm01`, `pm25`, `pm10` and `wifi` (`ssid`, `ip`, `rssi`), retained |
| `homeassistant/sensor/<node>/command` | Subscribed, not used yet |
| `homeassistant/sensor/<node>_<sensor>/config` | Discovery config for `wifi`, `pm1`, `pm25` and `pm10`, retained |

## License

MIT, see [LICENSE](LICENSE).
