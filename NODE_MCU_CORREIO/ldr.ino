//Lux

#ifdef LDR_PIN

double readAdc (int RawADC0){
  double Vout=RawADC0*0.004887585533;
  int lux=(2500/Vout-500)/10;
  return lux;
}

Timing notifyLDRtimer;
long notifyLDRTimeout;
const String MQTT_LDR_TOPIC_STATE =  buildMqttTopicState(MAIN_PLACE,"street","sensor","mailbox","ldr");


//Chamar este método no lopp principal
void loopLDR(){
  if (!checkMqttConnection()){
    return;
  }
  
    
  
  if (notifyLDRtimer.onTimeout(5000)) {
    client.publish(("homeassistant/sensor/"+String(HOSTNAME)+"_ldr/config").c_str(),("{\"name\": \""+String(HOSTNAME)+"_LDR\", \"state_topic\": \""+MQTT_LDR_TOPIC_STATE+"\"}").c_str());
    Serial.print("Lux: ");
    double lux = readAdc(analogRead(LDR_PIN));
    Serial.println(lux );
  
    client.publish(MQTT_LDR_TOPIC_STATE.c_str(),String(lux).c_str(),true);
  }
  
}
#endif
