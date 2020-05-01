#include <Arduino.h>

//#include <DNSServer.h>
//#include <ESP8266WebServer.h>
#include <IPAddress.h>
#include <WiFiManager.h>

#include <DallasTemperature.h>
#include <OneWire.h>

#include <CTBot.h>
#include <Chatter.hpp>
#include <unordered_set>

#include <FS.h>
#include <ArduinoJson.hpp>


struct ProjectConfig
{
    String bot_token;
};

struct
{

    struct EarlyInit
    {
        EarlyInit() { Serial.begin(230400); }
    } _;

    OneWire ow{ D3 };
    DallasTemperature temp_sensors{ &ow };
    uint8_t temperature_sensor_address{ 0xff };

    CTBot tbot;
    Chatter<String, void, const TBMessage &> cb;

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

        // load project configuration
        if(!SPIFFS.begin()) Serial.println("Error: File System not ready!");
        ProjectConfig cfg = load_config(F("/config.json"));
        SPIFFS.end();
        
        // configure telegram bot
        tbot.setTelegramToken(cfg.bot_token);

        add_chatbot_cmds();

    }

    void loop()
    {
        handleTelegramMessages();
        float temp = getTemperatureCelsius();
        if(temp > 30.0f)
        {
            Serial.println("Warning the temperature is dangerously high");
        }
        Serial.println(temp);
        for(auto registered_user : registered_ids)
        {
            tbot.sendMessage(registered_user, String(temp));
        }
        delay(10000);
        // process: enable wifi, notify, disable wifi
        // (deep) sleep
    }

private:
    float getTemperatureCelsius()
    {
        temp_sensors.requestTemperaturesByAddress(&temperature_sensor_address);
        return temp_sensors.getTempC(&temperature_sensor_address);
    }

    ProjectConfig load_config(const String& filename)
    {
        ProjectConfig cfg;

        File cfgfile = SPIFFS.open(filename, "r");
        StaticJsonBuffer<512> json_bfr;

        JsonObject &json_doc = json_bfr.parseObject(cfgfile);
        if(!json_doc.success())
            Serial.println(F("Failed to read configuratiomn file!"));

        cfg.bot_token = json_doc["bot_token"] | "MISSING_TOKEN";

        cfgfile.close();
        return cfg;
    }

    std::unordered_set<int32_t> registered_ids;

    void handleTelegramMessages()
    {
        TBMessage msg;
        while(tbot.getNewMessage(msg))
        {
            cb.handle_incomoing_message(msg.text, msg);
        }
    }

    void add_chatbot_cmds()
    {
        cb.add("/start", [&](const TBMessage &msg) {
            String text{ "Welcome to TemperatureMonitor!\ncurrently supported commands are: "
                         "/register and /unregister" };
            tbot.sendMessage(msg.sender.id, text);
        });
        cb.add("/register", "Register to get temperature updates.", [&](const TBMessage &msg) {
            if(registered_ids.insert(msg.sender.id).second)
            {
                tbot.sendMessage(msg.sender.id,
                                 "You have now been registered to get temperature updates!");
                Serial.print("A new user has been registered: ");
                Serial.print(msg.sender.id);
            }
        });
        cb.add("/unregister", "Don't get any more temperature updates.", [&](const TBMessage &msg) {
            auto it = registered_ids.find(msg.sender.id);
            if(it != end(registered_ids))
            {
                registered_ids.erase(it);
                tbot.sendMessage(msg.sender.id, "You will no longer receive temperature updates!");
            }
        });
    }

} R;

void setup() { R.setup(); }

void loop() { R.loop(); }
