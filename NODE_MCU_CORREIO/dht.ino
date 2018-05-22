#ifdef DHT_PIN
#include "DHT.h"


DHT dht;
Timing notifytimer;
bool autoHaNotify = true;
long notifyTimeout;
const String MQTT_HUMIDITY_TOPIC_STATE = buildMqttTopicState(MAIN_PLACE,"street","sensor","mailbox","humidity");
const String MQTT_TEMPERATURE_TOPIC_STATE = buildMqttTopicState(MAIN_PLACE,"street","sensor","mailbox","temperature");;

//Chamar este método no setup principal
void setupDHT(int pin, long timeInMinutes){
  Serial.println("DHT SETUP ON - PLUG SENSOR ON "+String(pin)+" PIN");
  dht.setup(pin);
  notifytimer.begin(0);
  notifyTimeout = timeInMinutes*60*1000;
}

//Chamar este método no lopp principal
void loopDHT(){
  if (!checkMqttConnection()){
    return;
  }
  if(autoHaNotify){
    client.publish(("homeassistant/sensor/"+String(HOSTNAME)+"_temperature/config").c_str(),("{\"name\": \""+String(HOSTNAME)+"_TEMPERATURE\", \"state_topic\": \""+MQTT_TEMPERATURE_TOPIC_STATE+"\"}").c_str());
    client.publish(("homeassistant/sensor/"+String(HOSTNAME)+"_humidity/config").c_str(),("{\"name\": \""+String(HOSTNAME)+"_HUMIDITY\", \"state_topic\": \""+MQTT_HUMIDITY_TOPIC_STATE+"\"}").c_str());
    autoHaNotify = false;
  }
  
  if (notifytimer.onTimeout(notifyTimeout)) {
      //cria os sensores automáticamente no Homeassistant
    delay(dht.getMinimumSamplingPeriod());
    float humidity = dht.getHumidity();
    float temperature = dht.getTemperature();
  
    Serial.print("Humidity: ");
    Serial.println(humidity);
    Serial.print("Temperature: ");
    Serial.println(temperature);
  
    client.publish(MQTT_HUMIDITY_TOPIC_STATE.c_str(),String(humidity).c_str(),true);
    client.publish(MQTT_TEMPERATURE_TOPIC_STATE.c_str(),String(temperature).c_str(),true);
  }
  
}
#endif
