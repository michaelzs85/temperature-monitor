#include <Arduino.h>

//#include <DNSServer.h>
//#include <ESP8266WebServer.h>
#include <IPAddress.h>
#include <WiFiManager.h>

#include <DallasTemperature.h>
#include <OneWire.h>

struct
{

    struct EarlyInit
    {
        EarlyInit() { Serial.begin(230400); }
    } _;

    OneWire ow{ D3 };
    DallasTemperature temp_sensors{ &ow };
    uint8_t temperature_sensor_address{ 0xff };

    void setup()
    {
        // disable LED blinking upon WiFi activity
        wifi_status_led_uninstall();
        pinMode(LED_BUILTIN, OUTPUT);
        digitalWrite(LED_BUILTIN, HIGH);

        // WiFi STA setup
        IPAddress address(192, 168, 0, 1);
        IPAddress &gateway = address;
        IPAddress subnet(255, 255, 255, 0);

        WiFiManager wifiManager;
        wifiManager.setAPStaticIPConfig(address, gateway, subnet);
        wifiManager.autoConnect("Temperature Monitor");

        Serial.println("setup: wifi connected");

        // temperature sensor setup
        temp_sensors.begin();
        temp_sensors.getAddress(&temperature_sensor_address, 0);

        // 10dBm corresponds to  10 mW
        // 20dBm corresponds to 100 mW
        WiFi.setOutputPower(10);
    }

    void loop() {
        // getTemperatureCelsius()
        // process: enable wifi, notify, disable wifi
        // (deep) sleep
    }

private:
    float getTemperatureCelsius()
    {
        temp_sensors.requestTemperaturesByAddress(&temperature_sensor_address);
        return temp_sensors.getTempC(&temperature_sensor_address);
    }

} R;

void setup() { R.setup(); }

void loop() { R.loop(); }
