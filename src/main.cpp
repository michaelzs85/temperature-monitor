
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h> 
#include <IPAddress.h>


void setup()
{
    Serial.begin(230400);
    
    IPAddress address(192, 168, 0, 1);
    IPAddress &gateway = address;
    IPAddress subnet(255, 255, 255, 0);
 
    WiFiManager wifiManager;    
    wifiManager.setAPStaticIPConfig(address, gateway, subnet);
    wifiManager.autoConnect("Temperature Monitor");

    Serial.println("setup: wifi connected");
}

void loop()
{
}

