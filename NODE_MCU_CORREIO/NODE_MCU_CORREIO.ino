//MQTT
#include <PubSubClient.h>//https://www.youtube.com/watch?v=GMMH6qT8_f4  
#include <Bounce2.h>// https://github.com/thomasfredericks/Bounce2
#include <FS.h>  
//JSON
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson  
#include <Timing.h>
//ESP
#include <ESP8266WiFi.h>
//Wi-Fi Manger library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>//https://github.com/tzapu/WiFiManager
#include <NtpClientLib.h>
#include <TimeLib.h>

#define DETECT_MAIL 4 //D2
#define CHECK_MAIL 14 //D5
#define STREET_DOOR 2 //D4
//Descomentar a linha a baixo para usar o sensor de temperatura DHT no pino 16 
#define DHT_PIN 12
//Descomentar a linha a baixo para usar o LDR no pino A0  
#define LDR_PIN A0 //A0


Timing notifTimer;


//CONSTANTS
const String MAIN_PLACE  = "bhhome";
const String HOSTNAME  = "mailBoxSensor";
String buildMqttTopicState(String mainLocal,String local,String typeOfDevice,String nameOfDevice, String typeOfData){
  return mainLocal+"/"+local+"/"+typeOfDevice+"/"+String(HOSTNAME)+"/"+nameOfDevice+"/"+typeOfData+"/state";
}

const String MQTT_LOG = "system/log";
const String MQTT_SYSTEM_CONTROL_TOPIC = "system/set/"+HOSTNAME;
const String MQTT_MAILBOX_MAIL_STATE_TOPIC = buildMqttTopicState(MAIN_PLACE,"street","sensor","mailbox","hasMail");
const String MQTT_MAILBOX_LAST_MAIL_IN_STATE_TOPIC = buildMqttTopicState(MAIN_PLACE,"street","sensor","mailbox","lastIn");
const String MQTT_MAILBOX_LAST_MAIL_CHECK_STATE_TOPIC = buildMqttTopicState(MAIN_PLACE,"street","sensor","mailbox","lastCheck");
const String MQTT_MAILBOX_DOOR_STATE_TOPIC = buildMqttTopicState(MAIN_PLACE,"street","sensor","mailboxDoor","position");
const String MQTT_OUTDOOR_FRONTDOOR_STATE_TOPIC = buildMqttTopicState(MAIN_PLACE,"street","sensor","outFrontDoor","position");
//Configuração por defeito
char mqtt_server[40];
char mqtt_port[6] = "1883";
char mqtt_username[34] = "";
char mqtt_password[34] = "";

WiFiManager wifiManager;
WiFiClient wclient;
PubSubClient client(mqtt_server,atoi(mqtt_port),wclient); 

//FLAGS de Control
bool youGotmail = false;
long lastNotifTime = 0;

String mailDoorState = "";
bool lastStreetDoorState = false;
String lastMailDoorState = "";
bool youGotmailNotif = false;
bool checkMailNotif = false;
bool shouldSaveConfig = false;



void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}


void mountFileSystem(){
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");
          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_username, json["mqtt_username"]);
          strcpy(mqtt_password, json["mqtt_password"]);
        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
}

void formatFileSystem(){
  SPIFFS.format(); 
}


//Init webserver
int timesPress = 0;
long lastPressedMillis = 0;
bool checkManualReset(){
  if(timesPress == 0){
    lastPressedMillis = millis();
  }
  if(lastPressedMillis + 3000 > millis()){
    timesPress++;
    Serial.println("TIME ++");
  }else{
    timesPress = 0;
    Serial.println("TIME ZERO");
   }
    if(timesPress > 5){
      Serial.println("RESET");
      wifiManager.resetSettings();
      delay(1000);
     WiFi.forceSleepBegin(); wdt_reset(); ESP.restart(); while(1)wdt_reset();
    }
  

}


void setupWifiManager(){
   //reset saved settings
 //wifiManager.resetSettings();
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);
  WiFiManagerParameter custom_mqtt_username("username", "mqtt username", mqtt_username, 32);
  WiFiManagerParameter custom_mqtt_password("password", "mqtt password", mqtt_password, 32);
  
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_username);
  wifiManager.addParameter(&custom_mqtt_password);

  if (!wifiManager.autoConnect(HOSTNAME.c_str())) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //Reset para tentar novamente
    ESP.restart();
    delay(5000);
  }

  //Leitura dos valures guardados
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_username, custom_mqtt_username.getValue());
  strcpy(mqtt_password, custom_mqtt_password.getValue());
 //Guardar Configuração se necessario
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_password"] = mqtt_password;
    json["mqtt_username"] = mqtt_username;
    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }
  Serial.println("IP: ");
  Serial.println(WiFi.localIP());
}


//Chamada de recepção de mensagem 
void callback(char* topic, byte* payload, unsigned int length) {
  String payloadStr = "";
  for (int i=0; i<length; i++) {
    payloadStr += (char)payload[i];
  }
  Serial.println(payloadStr);
  String topicStr = String(topic);
 if(topicStr.equals(MQTT_SYSTEM_CONTROL_TOPIC)){
   if (payloadStr.equals("REBOOT")){
    ESP.restart();
  }
 }     
} 

//Verifica se a ligação está ativa, caso não este liga-se e subscreve aos tópicos de interesse
bool checkMqttConnection(){
  if (!client.connected()) {
     Serial.print("TRY CONNECT TO MQTT ");
       Serial.println(mqtt_server);
    if (client.connect(HOSTNAME.c_str(),mqtt_username, mqtt_password)) {
      //SUBSCRIÇÃO DE TOPICOS
      Serial.print("CONNECTED ON MQTT ");
      Serial.println(mqtt_server);
      client.subscribe(MQTT_SYSTEM_CONTROL_TOPIC.c_str());
      //Envia uma mensagem por MQTT para o tópico de log a informar que está ligado
      client.publish(MQTT_LOG.c_str(),(String(HOSTNAME)+" CONNECTED").c_str());
      
      //MAILBOX
      client.publish(("homeassistant/sensor/"+String(HOSTNAME)+"_mailbox/config").c_str(),("{\"name\": \""+String(HOSTNAME)+"_MAILBOX\", \"state_topic\": \""+MQTT_MAILBOX_MAIL_STATE_TOPIC+"\"}").c_str());
      //MAILBOX DOOR STATE
      client.publish(("homeassistant/sensor/"+String(HOSTNAME)+"_maibox_door/config").c_str(),("{\"name\": \""+String(HOSTNAME)+"_MAILBOX_DOOR\", \"state_topic\": \""+MQTT_MAILBOX_DOOR_STATE_TOPIC+"\"}").c_str());
      //LAST MAIL IN
      client.publish(("homeassistant/sensor/"+String(HOSTNAME)+"_last_mail_in/config").c_str(),("{\"name\": \""+String(HOSTNAME)+"_MAIL_IN\", \"state_topic\": \""+MQTT_MAILBOX_LAST_MAIL_CHECK_STATE_TOPIC+"\"}").c_str());
      //LAST MAIL CHECK
      client.publish(("homeassistant/sensor/"+String(HOSTNAME)+"_last_mail_check/config").c_str(),("{\"name\": \""+String(HOSTNAME)+"_MAIL_CHECK\", \"state_topic\": \""+ MQTT_MAILBOX_LAST_MAIL_IN_STATE_TOPIC+"\"}").c_str());
      //OUTDOOR FRONT STATE
      client.publish(("homeassistant/sensor/"+String(HOSTNAME)+"_out_frontdoor/config").c_str(),("{\"name\": \""+String(HOSTNAME)+"_OUT_FRONTDOOR\", \"state_topic\": \""+MQTT_OUTDOOR_FRONTDOOR_STATE_TOPIC+"\"}").c_str());

      client.publish(MQTT_OUTDOOR_FRONTDOOR_STATE_TOPIC.c_str(),digitalRead(STREET_DOOR) ? "OPEN" : "CLOSED", true);
    }
  }
  return client.connected();
}
Bounce debouncerSwMag = Bounce(); 
void setup() {
 Serial.begin(115200);
  //Montar sistema de ficheiros
  mountFileSystem();
  //Configurar Wi-Fi Manager
  setupWifiManager();

  client.setCallback(callback); 
 #ifdef DHT_PIN
  setupDHT(DHT_PIN,1);//Notifica a temperatura e humidade a cada  minuto
  #endif
  pinMode(DETECT_MAIL,INPUT);
  pinMode(CHECK_MAIL,INPUT_PULLUP);
  pinMode(STREET_DOOR,INPUT_PULLUP);
  debouncerSwMag.attach(STREET_DOOR);
  debouncerSwMag.interval(5); // interval in ms
  attachInterrupt(digitalPinToInterrupt(DETECT_MAIL), youHaveMail, RISING);
  attachInterrupt(digitalPinToInterrupt(CHECK_MAIL), checkMail, RISING);
  NTP.begin("1.pt.pool.ntp.org", 0, true);
  NTP.setInterval(63);
  prepareWebserverUpdate();
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


void loop() {
  debouncerSwMag.update();
if (WiFi.status() == WL_CONNECTED) {
    otaLoop();
    if (checkMqttConnection()){
      //IF MQTT CLIENT CONNECT DO SOMMETHING      
      client.loop();
     #ifdef DHT_PIN
      loopDHT();//lê a temperatura e humidade e publica via MQTT
     #endif 
       #ifdef LDR_PIN
      loopLDR();//lê a temperatura e humidade e publica via MQTT
     #endif 
      String dateStr = NTP.getTimeDateString().equals("00:00:01 01/01/1970") ? "Never" : NTP.getTimeDateString();
      
      if(youGotmail && !youGotmailNotif){
        client.publish(MQTT_MAILBOX_MAIL_STATE_TOPIC.c_str(),"Yes", true);
        client.publish(MQTT_MAILBOX_LAST_MAIL_IN_STATE_TOPIC.c_str(),dateStr.c_str(), true);
        youGotmailNotif = true;
       }
       if(!checkMailNotif){
        client.publish(MQTT_MAILBOX_MAIL_STATE_TOPIC.c_str(),"No", true);
        client.publish(MQTT_MAILBOX_LAST_MAIL_CHECK_STATE_TOPIC.c_str(),dateStr.c_str(),true);
        checkMailNotif = true;
        }
        
        
        mailDoorState =  !digitalRead(CHECK_MAIL) ? "CLOSED" : "OPEN";
        
        if(mailDoorState != lastMailDoorState){
          lastMailDoorState = mailDoorState;
          client.publish(MQTT_MAILBOX_DOOR_STATE_TOPIC.c_str(),mailDoorState.c_str() ,true);
         }
        
        bool realStateMag = debouncerSwMag.read();
        
        if(realStateMag != lastStreetDoorState ){
            lastStreetDoorState = realStateMag;
            Serial.println(realStateMag);
            client.publish(MQTT_OUTDOOR_FRONTDOOR_STATE_TOPIC.c_str(),digitalRead(STREET_DOOR) ? "OPEN" : "CLOSED", true);
         }  
    }
  }
        
}



