/**********************************
*** AD5M Ventilation Controller ***
*** For ESP32 SuperMini C3      ***
***********************************/

#include <WiFi.h> // Arduino WiFi Library
#include <BME280I2C.h> // BME280 Library
#include <ScioSense_ENS160.h> // ENS160 Adafruit library
#include <Wire.h>
#include <EEPROM.h>
#include "wifi_settings.h" // User WiFi settings

#define NETWORK_PORT 8080
const char* VERSION = "0.2"; // Firmware release version

/**** Pin Layout  ****/
// 8 - I2C SDA
// 9 - I2C SCL
// 0 - PWM Recirculation Fan
// 1 - PWM Exhaust Fan
// 2 - TVOC High LED
const int PIN_SDA = 8;
const int PIN_SCL = 9;
const int PIN_RECIRC = 0;
const int PIN_EXHAUST = 1;
const int PIN_TVOC = 2;

struct VC_Settings {
    int eeprom_saved;
    int mode; // 0 - auto, 1 - manual
    int fan_recirc_enable;
    int fan_exhaust_enable;
    int fan_recirc_pwm;
    int fan_exhaust_pwm;
};
int save_to_eeprom = 0;
VC_Settings vc_settings;
NetworkServer server(NETWORK_PORT);
unsigned long last_wifi_check = 0;
const unsigned long WIFI_CHECK_INTERVAL_MS = 30000;

// Fan Limits
const int fan_recirc_min = 55;
const int fan_exhaust_min = 50;
int fan_recirc_last = 0;
int fan_exhaust_last = 0;


// BME280 Temperature / Humidity / Pressure sensor
bool bme280_detected = false;
bool bmp280_detected = false;
BME280I2C::Settings settings (
    BME280::OSR_X1, //temp
    BME280::OSR_X1, //humid
    BME280::OSR_X4, // pressure
    BME280::Mode_Normal, // Mode
    BME280::StandbyTime_500us, // Standby time
    BME280::Filter_16, // Filter
    BME280::SpiEnable_False,
    BME280I2C::I2CAddr_0x76
);
BME280I2C bme(settings);
float temperature = 0.0; // C
float pressure = 0.0; // hPa
float humidity = 0.0; // RH %

// ENS160 VOC Sensor
bool ens160_detected = false;
ScioSense_ENS160 ens160(ENS160_I2CADDR_1); // Change if needed, 0 = 0x52, 1 = 0x53
int air_AQI = 0; // 1-5
int air_TVOC = 0; // ppb
int air_eCO2 = 0; // ppm

// HTML
String http_header = R"(HTTP/1.1 200 OK
Content-Type: text/html

<!DOCTYPE HTML>
<html>
<head>
	<title>AD5M Ventilation Controller</title>
	<style>
	body { padding: 0; margin: 0; font-family: segoe ui; font-size: 18px;}
	label { margin: 0px 10px; }
  span.safe { color: #1daf1d; }
  span.warn { color: #fdaa28; }
  span.danger { color: #ff1414; }
	#container {
		background-color: #f1f1f1;
		width: 1024px;
		margin: 0px auto;
		padding: 10px; }

	</style>
</head>
<body>
<div id="container">
<h1>AD5M Ventilation Controller</h1>
)";

String http_footer = R"(<br><a href="https://github.com/Chryseus/AD5M_Fan_Controlller">Github</a>
</div>
</body>
</html>

)";

void connect_wifi() {
  while (!WiFi.isConnected()) {
    Serial.print("Connecting to ");
    Serial.println(SSID);
    WiFi.begin(SSID, WPA_KEY);
    delay(10000);
  }

  Serial.println("Connected.");
  Serial.print("Local IP address: ");
  Serial.println(WiFi.localIP());
}

void check_eeprom() {
  EEPROM.get(0, vc_settings);

  if (vc_settings.eeprom_saved != 1)
  {
    Serial.println("Refreshing EEPROM.");
    vc_settings.mode = 0;
    vc_settings.fan_exhaust_enable = 0;
    vc_settings.fan_recirc_enable = 0;
    vc_settings.fan_exhaust_pwm = fan_exhaust_min;
    vc_settings.fan_recirc_pwm = fan_recirc_min;
    vc_settings.eeprom_saved = 1;
    EEPROM.put(0, vc_settings);
    EEPROM.commit();
  }
}

void check_wifi() {
  if (!WiFi.isConnected()) {
    Serial.println("Wifi connection failure, retrying.");
    connect_wifi();
    return;
  }
}

void process_form(String &data, WiFiClient &client) {
  int start = 0;
  while (start < data.length()) {
    int eq = data.indexOf('=', start);
    if (eq < 0)
      break;

    int amp = data.indexOf('&', eq + 1);
    if (amp < 0)
      amp = data.length();

    String key = data.substring(start, eq);
    String value = data.substring(eq + 1, amp);

    if (key == "mode")
      vc_settings.mode = value.toInt();
    else if (key == "recirc")
      vc_settings.fan_recirc_enable = value.toInt();
    else if (key == "recirc_pwm")
      vc_settings.fan_recirc_pwm = value.toInt();
    else if (key == "exhaust")
      vc_settings.fan_exhaust_enable = value.toInt();
    else if (key == "exhaust_pwm")
      vc_settings.fan_exhaust_pwm = value.toInt();
    else if (key == "save")
      save_to_eeprom = value.toInt();

    start = amp + 1;
  }

  Serial.println("Settings updated:");
  Serial.println("Mode: " + String(vc_settings.mode));
  Serial.println("Recirc Fan: " + String(vc_settings.fan_recirc_enable));
  Serial.println("Recirc PWM: " + String(vc_settings.fan_recirc_pwm));
  Serial.println("Exhaust Fan: " + String(vc_settings.fan_exhaust_enable));
  Serial.println("Exhaust PWM: " + String(vc_settings.fan_exhaust_pwm));

  if (save_to_eeprom == 1) {
    Serial.println("Saving settings to EEPROM.");
    vc_settings.eeprom_saved = 1;
    EEPROM.put(0, vc_settings);
    EEPROM.commit();
    save_to_eeprom = 0;
  }

  // Redirect
  client.print("<head>");
  client.print("<meta http-equiv=\"refresh\" content=\"0;url=/\">");
  client.print("</head>");
}

void send_webpage(WiFiClient &client) {
    if(client) {
        while (client.connected()) {
            if (client.available()) {
                String c = client.readStringUntil('\n');
                Serial.println(c);
                client.println(http_header);
                if (c.indexOf('?') > 0) {
                    process_form(c, client);
                }

                if (bme280_detected && !bmp280_detected) {
                    client.println("<h2>BME280 Sensor Data</h2>");
                    client.println("<p>Temperature: " + String(temperature) + " C<br>");
                    client.println("Pressure: " + String(pressure) + " hPa<br>");
                    client.println("Humidity: " + String(humidity) + " %</p>");
                } else if (bme280_detected && bmp280_detected) {
                    client.println("<h2>BMP280 Sensor Data</h2>");
                    client.println("<p>Temperature: " + String(temperature) + " C<br>");
                    client.println("Pressure: " + String(pressure) + " hPa</p>");
                }

                if (ens160_detected) {
                    client.println("<h2>ENS160 Sensor Data</h2>");
                    if (air_TVOC <= 200) {
                        client.println("<p><span class=safe>TVOC: " + String(air_TVOC) + " ppb</span><br>");
                    } else if (air_TVOC > 200 && air_TVOC < 600) {
                        client.println("<p><span class=warn>TVOC: " + String(air_TVOC) + " ppb</span><br>");
                    } else {
                        client.println("<p><span class=danger>TVOC: " + String(air_TVOC) + " ppb</span><br>");
                    }

                    if (air_eCO2 <= 800) {
                        client.println("<span class=safe>eCO2: " + String(air_eCO2) + " ppm</span><br>");
                    } else if (air_eCO2 > 800 && air_eCO2 < 1500) {
                        client.println("<span class=warn>eCO2: " + String(air_eCO2) + " ppm</span><br>");
                    } else {
                        client.println("<span class=danger>eCO2: " + String(air_eCO2) + " ppm</span><br>");
                    }

                    if (air_AQI <= 2 ) {
                        client.println("<span class=safe>AQI: " + String(air_AQI) + "</span></p>");
                    } else if (air_AQI > 2 && air_AQI <= 4) {
                        client.println("<span class=warn>AQI: " + String(air_AQI) + "</span></p>");
                    } else {
                        client.println("<span class=danger>AQI: " + String(air_AQI) + "</span></p>");
                    }
                }

                client.println("<h2>Settings</h2><form method=\"get\" action=\"\"><fieldset><legend>Mode</legend>");
                if(vc_settings.mode == 0) {
                    client.println("<label for=\"auto\">Automatic</label><input type=\"radio\" id=\"auto\" name=\"mode\" value=\"1\" checked><label for=\"manual\">Manual Override</label><input type=\"radio\" id=\"manual\" name=\"mode\" value=\"0\">");
                } else {
                    client.println("<label for=\"auto\">Automatic</label><input type=\"radio\" id=\"auto\" name=\"mode\" value=\"1\"><label for=\"manual\">Manual Override</label><input type=\"radio\" id=\"manual\" name=\"mode\" value=\"0\" checked>");
                }
                client.println("</fieldset><br><fieldset><legend>Recirculation Fan</legend>");

                if (vc_settings.fan_recirc_enable == 1) {
                    client.println("<label for=\"recirc_enable\">Enable</label><input type=\"radio\" id=\"recirc_enable\" name=\"recirc\" value=\"1\" checked>");
                    client.println("<label for=\"recirc_disable\">Disable</label><input type=\"radio\" id=\"recirc_disable\" name=\"recirc\" value=\"0\"><br>");
                } else {
                    client.println("<label for=\"recirc_enable\">Enable</label><input type=\"radio\" id=\"recirc_enable\" name=\"recirc\" value=\"1\">");
                    client.println("<label for=\"recirc_disable\">Disable</label><input type=\"radio\" id=\"recirc_disable\" name=\"recirc\" value=\"0\" checked><br>");
                }
                client.println("<label for=\"recirc_pwm\">Speed</label><input type=\"number\" id=\"recirc_pwm\" name=\"recirc_pwm\" value=\"" + String(vc_settings.fan_recirc_pwm) + "\" min=\"" + fan_recirc_min + "\" max=\"100\"> %");
                client.println("</fieldset><br><fieldset><legend>Exhaust Fan</legend>");

                if(vc_settings.fan_exhaust_enable == 1) {
                    client.println("<label for=\"exhaust_enable\">Enable</label><input type=\"radio\" id=\"exhaust_enable\" name=\"exhaust\" value=\"1\" checked>");
                    client.println("<label for=\"exhaust_disable\">Disable</label><input type=\"radio\" id=\"exhaust_disable\" name=\"exhaust\" value=\"0\"><br>");
                } else {
                    client.println("<label for=\"exhaust_enable\">Enable</label><input type=\"radio\" id=\"exhaust_enable\" name=\"exhaust\" value=\"1\">");
                    client.println("<label for=\"exhaust_disable\">Disable</label><input type=\"radio\" id=\"exhaust_disable\" name=\"exhaust\" value=\"0\" checked><br>");
                }
                client.println("<label for=\"exhaust_pwm\">Speed</label><input type=\"number\" id=\"exhaust_pwm\" name=\"exhaust_pwm\" value=\"" + String(vc_settings.fan_exhaust_pwm) + "\" min=\"" + fan_exhaust_min + "\" max=\"100\"> %");
                client.println("</fieldset><br><fieldset><legend>Misc</legend>");

                client.println("<label for=\"save\">Save settings to EEPROM</label><input type=\"checkbox\" id=\"save\" name=\"save\" value=\"1\">");
                client.println("</fieldset><br><input type=\"Submit\" value=\"Apply Settings\"></form>");

                client.println(http_footer);
                break;

            }
        }
        client.stop();
    }
}

void setup() {
  Serial.begin(9600);
  Serial.println("AD5M fan controller and monitoring.");
  Serial.print("Version: ");
  Serial.println(VERSION);
  pinMode(PIN_RECIRC, OUTPUT);
  pinMode(PIN_RECIRC, OUTPUT);
  pinMode(PIN_TVOC, OUTPUT);
  analogWriteFrequency(0, 30e3);
  analogWriteFrequency(1, 30e3);
  Wire.begin();
  EEPROM.begin(sizeof(vc_settings));
check_eeprom();

  if (!bme.begin()) {
    Serial.println("BME280 not found, some features may not be available.");
  } else {
    bme280_detected = true;
    if (bme.chipModel() == BME280::ChipModel_BMP280) {
      Serial.println(
          "BMP280 detected, humidity reading will not be available.");
      bmp280_detected = true;
    }
  }

  ens160.begin();
  if (!ens160.available()) {
    Serial.println(
        "ENS160 is not available, some features may not be available.");
  } else {
    ens160.setMode(ENS160_OPMODE_STD);
    ens160_detected = true;
  }

  connect_wifi();
  server.setTimeout(120);
  server.begin();
}

void loop() {
  unsigned long now_ms = millis();
  if (now_ms - last_wifi_check >= WIFI_CHECK_INTERVAL_MS) {
    check_wifi();
    last_wifi_check = now_ms;
  }

  if (bme280_detected && !bmp280_detected) {
    bme.read(pressure, temperature, humidity);
  } else if (bme280_detected && bmp280_detected) {
    temperature = bme.temp();
    pressure = bme.pres();
  }

  if (ens160_detected) {
    ens160.measure(false); // don't block
    air_AQI = ens160.getAQI();
    air_eCO2 = ens160.geteCO2();
    air_TVOC = ens160.getTVOC();
  }

  WiFiClient client = server.available();
  send_webpage(client);

  // Fan Control
  if (vc_settings.fan_recirc_enable > 0) {
    float output = (vc_settings.fan_recirc_pwm / 100.0f) * 255.0f;
    int ioutput = (int)output;
    if (fan_recirc_last == 0) {
      fan_recirc_last = vc_settings.fan_recirc_pwm;
      analogWrite(PIN_RECIRC, 255);
      delay(500);
    }
    analogWrite(PIN_RECIRC, ioutput);
  } else {
    analogWrite(PIN_RECIRC, 0);
    fan_recirc_last = 0;
  }

  if (vc_settings.fan_exhaust_enable > 0) {
    float output2 = (vc_settings.fan_exhaust_pwm / 100.0f) * 255.0f;
    int ioutput2 = (int)output2;
    if (fan_exhaust_last == 0) {
      fan_exhaust_last = vc_settings.fan_exhaust_pwm;
      analogWrite(PIN_EXHAUST, 255);
      delay(500);
    }
    analogWrite(PIN_EXHAUST, ioutput2);
  } else {
    analogWrite(PIN_EXHAUST, 0);
    fan_exhaust_last = 0;
  }

  // TVOC LED
  if (air_TVOC > 600) {
    digitalWrite(PIN_TVOC, HIGH);
  } else {
    digitalWrite(PIN_TVOC, LOW);
  }
}
