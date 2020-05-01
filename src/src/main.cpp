#include <Arduino.h>

//#include <DNSServer.h>
//#include <ESP8266WebServer.h>
#include <IPAddress.h>
#include <WiFiManager.h>

#include <DallasTemperature.h>
#include <OneWire.h>

#include <CTBot.h>
#include <Chatter.hpp>
#include <map>

#include <ArduinoJson.hpp>
#include <FS.h>

const char* jname_user_id PROGMEM = "user_id";
const char* jname_too_cold PROGMEM= "too_cold";
const char* jname_too_hot PROGMEM  = "too_hot";
const char* msg_file_system_error PROGMEM = "Error: File System not ready!";

static const uint32_t time_between_warnings_ms = 1000 * 60 * 1; // = 1min

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

struct MonitorData
{
    int32_t user_id;
    int32_t msg_id;
};

bool same_id(const User &lhs, const User &rhs) { return lhs.user_id == rhs.user_id; }

void toJson(const User &usr, JsonArray &arr)
{
    JsonObject &obj = arr.createNestedObject();
    obj[jname_user_id] = String(usr.user_id);
    obj[jname_too_cold] = String(usr.too_cold);
    obj[jname_too_hot] = String(usr.too_hot);
};

String botCommandsJson(const Chatter<String, void, const TBMessage &>& cb)
{
    DynamicJsonBuffer buffer;
    JsonArray& arr = buffer.createArray();
    for(auto cmd : cb.commands)
    {
        JsonObject& obj = arr.createNestedObject();
        obj["command"] = cmd.cmd_text;
        obj["description"] = cmd.help_text;
    }
    String str_json;
    arr.printTo(str_json);
    return str_json;
}

float configureTemp(CTBot &tbot, decltype(TBContact::id) recipient, float init, const char *text)
{
    String const help_text(F("\n\nUse the buttons below to adjust the temperature."));
    // Set up the temperature keyboard
    CTBotInlineKeyboard kbd;
    kbd.addButton("-1", "dec", CTBotKeyboardButtonQuery);
    kbd.addButton("-.5", "dec.5", CTBotKeyboardButtonQuery);
    kbd.addButton("OK", "accept", CTBotKeyboardButtonQuery);
    kbd.addButton("+.5", "inc.5", CTBotKeyboardButtonQuery);
    kbd.addButton("+1", "inc", CTBotKeyboardButtonQuery);
    String kbdjson = kbd.getJSON();

    String strtext(text);
    strtext.replace("<temp>", String(init));

    int32_t msgid = tbot.sendMessage(recipient, strtext + help_text, kbdjson);

    bool config_finished = false;
    TBMessage msg;
    while(!config_finished)
    {
        while(tbot.getNewMessage(msg) && !config_finished)
        {
            if(msg.messageType != CTBotMessageQuery)
            {
                tbot.sendMessage(recipient,
                                 F("Use the inline buttons to configure a value, then press OK"));
                continue;
            }
            if(msg.callbackQueryData == F("dec"))
                init -= 1.0f;
            else if(msg.callbackQueryData == F("dec.5"))
                init -= .5f;
            else if(msg.callbackQueryData == F("accept"))
                config_finished = true;
            else if(msg.callbackQueryData == F("inc.5"))
                init += 0.5f;
            else if(msg.callbackQueryData == F("inc"))
                init += 1.0f;
            if(!config_finished)
            {
                strtext = text;
                strtext.replace(F("<temp>"), String(init));
                tbot.editMessage(msg.sender.id, msgid, strtext + help_text, kbdjson);
            }
            else
            {
                tbot.editMessage(msg.sender.id, msgid, strtext, ""); // remove keyboard and helptext.
            }

            tbot.endQuery(msg.callbackQueryID, "");
            yield();
        }
        yield();
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

        Serial.println(F("setup: wifi connected"));

        // temperature sensor setup
        temp_sensors.begin();
        temp_sensors.getAddress(&temperature_sensor_address, 0);

        // 10dBm corresponds to  10 mW
        // 20dBm corresponds to 100 mW
        WiFi.setOutputPower(10);

        // load project configuration
        if(!SPIFFS.begin())
            Serial.println(msg_file_system_error);
        ProjectConfig cfg = load_config(F("/config.json"));
        SPIFFS.end();

        // configure telegram bot
        tbot.setTelegramToken(cfg.bot_token);

        add_chatbot_cmds();
        tbot.setMyCommands(botCommandsJson(cb));

        load_registered_users();
    }

    void loop()
    {
        handleTelegramMessages();
        curtemp = getTemperatureCelsius();
        Serial.println(curtemp);
        handleRegisteredUsers(curtemp);
        handleMonitoringUsers(curtemp);
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
            Serial.println(F("Failed to read configuration file!"));

        cfg.bot_token = json_doc["bot_token"] | "MISSING_TOKEN";

        cfgfile.close();
        return cfg;
    }

    std::vector<User> registered_users;
    std::map<decltype(User::user_id), int64_t> last_warning;
    float curtemp;
    std::vector<MonitorData> monitorings;


    void handleTelegramMessages()
    {
        TBMessage msg;
        while(tbot.getNewMessage(msg))
        {
            if(msg.messageType == CTBotMessageType::CTBotMessageText)
                cb.handle_incomoing_message(msg.text, msg);
            else if (msg.messageType == CTBotMessageType::CTBotMessageQuery)
            {
                cb.handle_incomoing_message(msg.callbackQueryData, msg);
            }
            
        }
    }

    void handleRegisteredUsers(float temp)
    {
        for(const User &usr : registered_users)
        {
            int64_t now = millis();
            auto it = last_warning.find(usr.user_id);
            if(it != last_warning.end())
                if((now - it->second) < time_between_warnings_ms)
                    continue;
            if(temp <= usr.too_cold)
            {
                tbot.sendMessage(usr.user_id,
                                 String("⚠️ Temperature Warning! ❄️ ") + String(temp) + "°C");
                last_warning[usr.user_id] = now;
            }
            if(temp >= usr.too_hot)
            {
                tbot.sendMessage(usr.user_id, String("⚠️ Temperature Warning! 🔥") + String(temp) + "°C");
                last_warning[usr.user_id] = now;
            }
            yield();
        }
    }

    void handleMonitoringUsers(float temp)
    {
        CTBotInlineKeyboard kbd;
        kbd.addButton("Stop Monitoring", "monitor_stop", CTBotKeyboardButtonQuery);
        String text = "Live Temperature: " + String(temp) + "°C";
        for(MonitorData& md : monitorings)
        {
            if (md.msg_id == 0)
            {
                md.msg_id = tbot.sendMessage(md.user_id, text, kbd); 
            }
            else
            {
                tbot.editMessage(md.user_id, md.msg_id, text, kbd);
            }
            yield();
        }
    }

    void add_chatbot_cmds()
    {
        cb.add("/start", "Show the welcome message.", [&](const TBMessage &msg) {
            String text{"Welcome to TemperatureMonitor!\ncurrently supported commands are:\n"};
            for(auto cmd :cb.commands)
            {
                if(cmd.help_text.isEmpty()) continue;
                text += cmd.cmd_text + ' ' + cmd.help_text + '\n';
            }
            tbot.sendMessage(msg.sender.id, text);
        });
        //---------------------------------------------------------------------
        cb.add("/register", "Register to get temperature updates.", [&](const TBMessage &msg) {
            String preamble("Configure at which temperatures you want to receive warnings:\n");
            tbot.sendMessage(msg.sender.id, preamble);
            float lower = configureTemp(tbot, msg.sender.id,
                                        1.0f, "Send a warning when the temperature falls below <temp>°C!");
            float upper = configureTemp(tbot, msg.sender.id,
                                        30.0f, "Send a warning when the temperature rises above <temp>°C!");
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
            store_registered_users();
        });
        //---------------------------------------------------------------------
        cb.add("/unregister", "Don't get any more temperature updates.", [&](const TBMessage &msg) {
            auto it = std::find_if(registered_users.begin(), registered_users.end(),
                                   [&msg](const User &usr) { return usr.user_id == msg.sender.id; });
            if(it == registered_users.end())
            {
                tbot.sendMessage(msg.sender.id, "You weren't registered anyway!");
            }
            if(it != registered_users.end() - 1)
            {
                std::swap(*it, registered_users.back());
            }
            registered_users.pop_back();
            store_registered_users();
            tbot.sendMessage(msg.sender.id, "No more warnings for you!");
        });
        //---------------------------------------------------------------------
        cb.add("/info", "Get information about your registration status", [&](const TBMessage &msg) {
            auto it = std::find_if(registered_users.begin(), registered_users.end(),
                                   [&msg](const User &usr) { return usr.user_id == msg.sender.id; });
            if(it == registered_users.end())
            {
                tbot.sendMessage(msg.sender.id, String("Hi ") + msg.sender.firstName + ", you are currently not a registered user!");
                return;
            }
            tbot.sendMessage(msg.sender.id,
                             "You will get warnings when the temperature is either below " +
                             String(it->too_cold) + "°C or above " + String(it->too_hot) +
                             "°C. Use /register to change the values!");
        });
        //---------------------------------------------------------------------
        cb.add("/currtemp", "Replies the current temperature", [&](const TBMessage &msg){
            tbot.sendMessage(msg.sender.id, String(curtemp) + "°C");
        });
        //---------------------------------------------------------------------
        cb.add("/monitor", "Start monitoring the temperature.", [&](const TBMessage &msg){
            monitorings.push_back(MonitorData{msg.sender.id, 0});
        });
        //---------------------------------------------------------------------
        cb.add("monitor_stop", [&](const TBMessage& msg){
            auto it = std::find_if(monitorings.begin(), monitorings.end(), [&msg](const MonitorData& md){
                return md.user_id == msg.sender.id;
            });
            if(it == monitorings.end())
                return;
            MonitorData md = *it;
            std::swap(*it, monitorings.back());
            monitorings.pop_back();
            tbot.deleteMessage(md.user_id, md.msg_id);
        });
    }

    void store_registered_users()
    {
        Serial.println("store_registered_users begin"); 
        DynamicJsonBuffer jbuffer;
        JsonObject &root = jbuffer.createObject();
        JsonArray &arr = root.createNestedArray("registration_data");
        Serial.println('A');
        for(const User &usr : registered_users)
        {
            Serial.println('B');
            toJson(usr, arr);
            Serial.println('C');
        }
        Serial.println('D');
        root.printTo(Serial);
        Serial.println();

        if(!SPIFFS.begin())
            Serial.println(msg_file_system_error);

        File file = SPIFFS.open("/reg_users", "w");
        root.printTo(file);

        file.close();
        SPIFFS.end();
        Serial.println("store_registered_users end"); 
    }

    void load_registered_users()
    {
        if(!SPIFFS.begin())
            Serial.println(msg_file_system_error);
        DynamicJsonBuffer jbuffer;

        ProjectConfig cfg;

        File file = SPIFFS.open("/reg_users", "r");

        JsonObject &json_doc = jbuffer.parseObject(file);
        if(!json_doc.success())
        {
            Serial.println(F("Failed to read registered users file!"));
            return;
        }

        JsonArray &arr = json_doc["registration_data"];
        for(auto obj : arr)
        {
            String str_user_id = obj[jname_user_id] | "";
            String str_too_cold = obj[jname_too_cold] | "";
            String str_too_hot = obj[jname_too_hot] | "";
            if(str_user_id.isEmpty() || str_too_cold.isEmpty() or str_too_hot.isEmpty())
                continue;
            registered_users.push_back(User{ static_cast<decltype(User::user_id)>(str_user_id.toInt()),
                                             str_too_cold.toFloat(), str_too_hot.toFloat() });
        }

        file.close();
        SPIFFS.end();
    }

} R;

void setup() { R.setup(); }

void loop() { R.loop(); }
