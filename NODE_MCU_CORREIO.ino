#include <Timing.h>
//MQTT
#include <PubSubClient.h>
//ESP
#include <ESP8266WiFi.h>
//Wi-Fi Manger library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>//https://github.com/tzapu/WiFiManager
//OTA
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include <NtpClientLib.h>
#include <TimeLib.h>
#include "DHT.h"
Timing notifTimer;
//CONSTANTS
const String HOSTNAME  = "MailBoxSensor";
const char * OTA_PASSWORD  = "otapower";
const String mqttLog = "ota/log/"+HOSTNAME;
const String mqttSystemControlTopic = "system/set/"+HOSTNAME;

#define PHOTO_RESISTOR_PIN A0 //A0
#define DETECT_MAIL 4 //D2
#define CHECK_MAIL 14 //D5
#define STREET_DOOR 5 //D1


DHT dht;
// Update these with values suitable for your network.
IPAddress server(192, 168,187,203);
WiFiClient wclient;
PubSubClient client(server,1883,wclient);
bool youGotmail = false;
long lastNotifTime = 0;
String streetDoorState = "";
String mailDoorState = "";
String lastStreetDoorState = "";
String lastMailDoorState = "";
bool youGotmailNotif = false;
bool checkMailNotif = false;

//CONTROL FLAGS
bool OTA = false;
bool OTABegin = false;

void callback(char* topic, byte* payload, unsigned int length) {
   String payloadStr = "";
     for (int i=0; i<length; i++) {
    payloadStr += (char)payload[i];
  }
 if(String(topic) == mqttSystemControlTopic){

  if(payloadStr.equals("OTA_ON")){
    OTA = true;
    OTABegin = true;
  }else if (payloadStr.equals("OTA_OFF")){
    OTA = true;
    OTABegin = true;
  }else if (payloadStr.equals("REBOOT")){
    ESP.restart();
  }
 } 
} 

bool checkMqttConnection(){
  if (!client.connected()) {
    if (client.connect(HOSTNAME.c_str(), "username","passsword")) {
      client.subscribe(mqttSystemControlTopic.c_str());
      youGotmailNotif = false;
      messureTemperatureHumidityAndPublish();
      messureLuxAndPublish();
    }
  }
  return client.connected();
}
void setup() {
 Serial.begin(115200);
  WiFiManager wifiManager;
   notifTimer.begin(0);
  //reset saved settings
  //wifiManager.resetSettings();
  /*sets timeout until configuration portal gets turned off
   useful to make it all retry or go to sleep in seconds*/
  wifiManager.setTimeout(180);
  wifiManager.autoConnect(HOSTNAME.c_str(),"xptoxpto");
  client.setCallback(callback); 
  dht.setup(12);
  pinMode(DETECT_MAIL,INPUT);
  pinMode(CHECK_MAIL,INPUT_PULLUP);
  pinMode(STREET_DOOR,INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(DETECT_MAIL), youHaveMail, RISING);
  attachInterrupt(digitalPinToInterrupt(CHECK_MAIL), checkMail, RISING);
  NTP.begin("1.pt.pool.ntp.org", 0, true);
  NTP.setInterval(63);
}

void youHaveMail(){  
  if(!digitalRead(CHECK_MAIL) && !youGotmail){
      Serial.println("YOU GOT MAIL");
      youGotmail = true;
      youGotmailNotif = false;
    }
}

void checkMail(){  
  if(digitalRead(CHECK_MAIL)){
      Serial.println("Mail CHECKED");
      checkMailNotif = false;
      youGotmail = false;
    }
}

void messureTemperatureHumidityAndPublish(){
    double humidity = 0;
      double   temperature = 0;
         delay(dht.getMinimumSamplingPeriod());
          for(int i = 0 ;i <1000; i++){
               humidity += dht.getHumidity();
               temperature += dht.getTemperature();
                client.loop();
          } 
              
              
        String hum = String(humidity / 1000,1);
        String temp = String(temperature / 1000 ,1);
        Serial.println(temp);
        client.publish("home-assistant/street/sensor/humidity",hum.c_str(), true);
        client.publish("home-assistant/street/sensor/temperature",temp.c_str(), true);
}
void messureLuxAndPublish(){
       double  lux = 0;
      for(int i = 0 ;i <10; i++){
         lux+=  analogRead(PHOTO_RESISTOR_PIN);
         delay(50);  
      }
     lux = light(lux/10);
      
        client.publish("home-assistant/street/sensor/ldr",String(lux).c_str(), true);
       
 }
void loop() {
if (WiFi.status() == WL_CONNECTED) {
    if (checkMqttConnection()){
      //IF MQTT CLIENT CONNECT DO SOMMETHING      
      client.loop();
if (notifTimer.onTimeout(1800000)){
          messureTemperatureHumidityAndPublish();
          messureLuxAndPublish();
      }
       
      
      String dateStr = NTP.getTimeDateString().equals("00:00:01 01/01/1970") ? "Never" : NTP.getTimeDateString();
      
      if(youGotmail && !youGotmailNotif){
        client.publish("home-assistant/street/mailbox","Yes", true);
        client.publish("home-assistant/street/mailbox/receive/on",dateStr.c_str(), true);
        youGotmailNotif = true;
       }
       if(!checkMailNotif){
        client.publish("home-assistant/street/mailbox","No", true);
        client.publish("home-assistant/street/mailbox/checked/on",dateStr.c_str(),true);
        checkMailNotif = true;
        }
        
        streetDoorState = !digitalRead(STREET_DOOR) ? "Locked" : "Unlocked";
        mailDoorState =  !digitalRead(CHECK_MAIL) ? "Locked" : "Unlocked";
        
        if(mailDoorState != lastMailDoorState){
          lastMailDoorState = mailDoorState;
          client.publish("home-assistant/street/maildoor",mailDoorState.c_str() ,true);
         }
        if(streetDoorState != lastStreetDoorState ){
          client.publish("home-assistant/street/frontdoor",streetDoorState.c_str(), true);
         }  
      if(OTA){
        if(OTABegin){
          setupOTA();
          OTABegin= false;
        }
        ArduinoOTA.handle();
      }
    }
  }
        
}

void setupOTA(){
  if (WiFi.status() == WL_CONNECTED && checkMqttConnection()) {
    client.publish(mqttLog.c_str(),"OTA SETUP");
    ArduinoOTA.setHostname(HOSTNAME.c_str());
    ArduinoOTA.setPassword((const char *)OTA_PASSWORD);
    
    ArduinoOTA.onStart([]() {
    client.publish(mqttLog.c_str(),"START");
  });
  ArduinoOTA.onEnd([]() {
    client.publish(mqttLog.c_str(),"END");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    String p = "Progress: "+ String( (progress / (total / 100)));
    client.publish(mqttLog.c_str(),p.c_str());
  });
  ArduinoOTA.onError([](ota_error_t error) {
    if (error == OTA_AUTH_ERROR) client.publish(mqttLog.c_str(),"Auth Failed");
    else if (error == OTA_BEGIN_ERROR)client.publish(mqttLog.c_str(),"Auth Failed"); 
    else if (error == OTA_CONNECT_ERROR)client.publish(mqttLog.c_str(),"Connect Failed");
    else if (error == OTA_RECEIVE_ERROR)client.publish(mqttLog.c_str(),"Receive Failed");
    else if (error == OTA_END_ERROR)client.publish(mqttLog.c_str(),"End Failed"); 
  });
 ArduinoOTA.begin();
 }  
}

//Lux
double light (int RawADC0){
double Vout=RawADC0*0.004887585533;
int lux=(2500/Vout-500)/10;
return lux;
}

