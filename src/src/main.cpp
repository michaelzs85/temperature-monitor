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

#include <ArduinoJson.hpp>
#include <FS.h>


struct ProjectConfig
{
    String bot_token;
};

struct User
{
    int32_t user_id;
    float too_cold;
    float too_hot;
};

bool same_id(const User &lhs, const User &rhs) { return lhs.user_id == rhs.user_id; }

float configureTemp(CTBot &tbot, decltype(TBContact::id) recipient, float init, const char *text)
{
    // Set up the temperature keyboard
    CTBotInlineKeyboard kbd;
    kbd.addButton("-1", "dec", CTBotKeyboardButtonQuery);
    kbd.addButton("-.1", "dec.1", CTBotKeyboardButtonQuery);
    kbd.addButton("temp°C", "accept", CTBotKeyboardButtonQuery); // "temp" is meant to be replaced by an actual temperature value
    kbd.addButton("+.1", "inc.1", CTBotKeyboardButtonQuery);
    kbd.addButton("+1", "inc", CTBotKeyboardButtonQuery);
    String kbdjson = kbd.getJSON();


    String actkbd = kbdjson;
    actkbd.replace("temp", String(init));
    int32_t msgid = tbot.sendMessage(recipient, text, actkbd);

    bool config_finished = false;
    TBMessage msg;
    while(!config_finished)
    {
        if(tbot.getNewMessage(msg))
        {
            if(msg.messageType != CTBotMessageQuery)
            {
                tbot.sendMessage(recipient, "Use the inline buttons to configure a value, press "
                                            "the value to accept.");
                continue;
            }
            if(msg.callbackQueryData == "dec")
                init -= 1.0f;
            else if(msg.callbackQueryData == "dec.1")
                init -= .1f;
            else if(msg.callbackQueryData == "accept")
                config_finished = true;
            else if(msg.callbackQueryData == "inc.1")
                init += 0.1f;
            else if(msg.callbackQueryData == "inc")
                init += 1.0f;
            if(!config_finished)
            {
                actkbd = kbdjson;
                actkbd.replace("temp", String(init));
                tbot.editKeyboard(msg.sender.id, msgid, actkbd);
            }
            tbot.endQuery(msg.callbackQueryID, "");
        }
        delay(50);
    }
    return init;
}

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
        if(!SPIFFS.begin())
            Serial.println("Error: File System not ready!");
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
        Serial.println(temp);
        handleRegisteredUsers(temp);
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

    ProjectConfig load_config(const String &filename)
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

    std::vector<User> registered_users;

    void handleTelegramMessages()
    {
        TBMessage msg;
        while(tbot.getNewMessage(msg))
        {
            cb.handle_incomoing_message(msg.text, msg);
        }
    }

    void handleRegisteredUsers(float temp)
    {
        for(const User &usr : registered_users)
        {
            if(temp <= usr.too_cold)
                tbot.sendMessage(usr.user_id,
                                 String("⚠️ Temperature Warning! ❄️ ") + String(temp) + "°C");
            if(temp >= usr.too_hot)
                tbot.sendMessage(usr.user_id, String("⚠️ Temperature Warning! 🔥") + String(temp) + "°C");
        }
    }

    void add_chatbot_cmds()
    {
        cb.add("/start", [&](const TBMessage &msg) {
            String text{ "Welcome to TemperatureMonitor!\ncurrently supported commands are: "
                         "/register and /unregister" };
            tbot.sendMessage(msg.sender.id, text);
        });
        //---------------------------------------------------------------------
        cb.add("/register", "Register to get temperature updates.", [&](const TBMessage &msg) {
            String preamble("Configure at which temperatures you want to receive warnings:\n");
            tbot.sendMessage(msg.sender.id, preamble);
            float lower = configureTemp(tbot, msg.sender.id, 1.0f, "Configure the lower temperature boundary:");
            float upper = configureTemp(tbot, msg.sender.id, 30.0f, "Configure the upper temperature boundary:");

            Serial.println("Configured boundary values are: " + String(lower) + " and " + String(upper));

            User new_registration{ msg.sender.id, lower, upper };

            bool updated_exisiting = false;
            for(User &usr : registered_users)
            {
                if(same_id(usr, new_registration))
                {
                    usr = new_registration;
                    updated_exisiting = true;
                }
            }
            if(!updated_exisiting)
            {
                registered_users.push_back(std::move(new_registration));
            }

            tbot.sendMessage(new_registration.user_id,
                             "You will now receive an alert when the temperature falls below " +
                             String(new_registration.too_cold) + "°C or rises above " +
                             String(new_registration.too_hot) + "°C");
        });
        //---------------------------------------------------------------------
        cb.add("/unregister", "Don't get any more temperature updates.", [&](const TBMessage &msg) {
            auto it = std::find_if(registered_users.begin(), registered_users.end(),
                                   [&msg](const User &usr) { return usr.user_id == msg.sender.id; });
            if(it == registered_users.end())
                return; // Not a registered user, nothing to do
            if(it != registered_users.end() - 1)
            {
                std::swap(*it, registered_users.back());
            }
            registered_users.pop_back();
            tbot.sendMessage(msg.sender.id, "No more warnings for you!");
        });
    }

} R;

void setup() { R.setup(); }

void loop() { R.loop(); }
