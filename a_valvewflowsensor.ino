/*a_valvewflowsensor - control a valve with a flowsensor, report state and change in flow to mqtt server. /makodse@gmail.com*/
#include "ota_secret.h"
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>
#include <NTPClient.h>
#include <PubSubClient.h>
#include <stdlib.h>


/*your wifi is setup in ota_secret.h, this is only loading it into the var*/
/*if downloaded you need to change it and change "ota_secret_example.h" to "ota_secret.h" */
const char* ssid = STASSID;
const char* password = STAPSK;
/* GMT +2*/
const long utcOffsetInSeconds = 7200;
/*wifi*/
WiFiUDP ntpUDP;

/*network time client */
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds, 60000);
/*webserver, used to set esp in programming mode */
ESP8266WebServer server;
/*
   192.168.3.63 = this board in  this router...
   http://192.168.3.63/restart
 * */
 /*your mqtt server*/
const char* mqtt_server = "192.168.3.220";


/*for mqtt*/
WiFiClient espClient;
PubSubClient client(espClient);
/*define sensors*/ 
#define VALVE 4 //valve through pin 4 (D2) 3.3v relä!
#define FLOWSENSOR 5 //flowsensor connected to pin5, D1
/*
 * kallarevatten 
 * kallarevatten/valve
 * kallarevatten/flow
 * kallarevatten/status
 */

/*ota variable*/
bool ota_flag = true;
/*variables for flow calculations*/
int trigger_flow=0;
/*timer variables*/
unsigned long timepassed;
unsigned long time_1sec=0;
unsigned long time_10sec=0;
unsigned long time_60sec=0;
unsigned long time_10min=0;
unsigned long time_60min=0;
float m3ssincereboot=0;
float m3perday_last=0; //logically in liters...
float m3perday=0;
/*track lastmode variables*/
int last_flowsensor=0;
int tenseconds_flow=0; //calculate the flow over 10 seconds
bool last_valve=0;
bool valve_state=0; //keeps track of valve state
char res[12]; // used to process result
String mytime; //string to use as buff for time
/*count_flowm() interupt function to calculate pulses -> flow*/
ICACHE_RAM_ATTR void count_flow(){
  trigger_flow++;
}
bool a_ota(uint16_t *time_elapsed, uint16_t *time_wanted){
      while (*time_elapsed < *time_wanted)
      {
        ArduinoOTA.handle();
        *time_elapsed = millis();
        delay(10);
      }
  return false;
}
void callback(char* topic, byte* payload, unsigned int length) {
 
  Serial.print("Message arrived in topic: ");
  Serial.println(topic);
 
  Serial.print("Message:");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
 
  Serial.println();
  Serial.println("-----------------------");
 
}
void setup() {
  /*serial*/
  Serial.begin(115200);
  Serial.println("Booting");
  /*wifi and OTA, NIH (from OTA example)*/
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  /*pinmode settings */
  pinMode(VALVE, OUTPUT);
  pinMode(FLOWSENSOR, INPUT);
  /*interrupt to measure flow*/
  attachInterrupt(digitalPinToInterrupt(FLOWSENSOR), count_flow, RISING);
  /*restart on url access for test thnx to ACROBOTIC*/
  /*TODO: implement with MQTT message so that the bord restarts on mqtt topic */
  server.on("/restart", []() {
    server.send(200, "text/plain", "Restarting...");
    delay(1000);
    ESP.restart();
  });
  
  /*mqtt*/
    client.setServer(mqtt_server, 1883);
  /*start the webserver*/
  server.begin();
  /*call to ntp client*/
  timeClient.begin();
  client.publish("esp/test", "Hello from ESP8266");
  client.subscribe("esp/test");
}
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("kallarevatten")) {
      Serial.println("connected");
      // Subscribe
      client.subscribe("kallarevatten/output");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void loop() {
  /*ota variables and function call if bord was rebooted*/
  uint16_t time_elapsed = 0;
  uint16_t time_wanted = 15000;
  if (ota_flag)
  {
    ota_flag = a_ota(&time_elapsed, &time_wanted);
      /*update ntp client*/
    timeClient.update();
  }
  /*mqtt*/
  
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  /*tasks that dont run every pass*/
  timepassed = millis();
  if(timepassed - time_1sec >= 1000){
    time_1sec=timepassed;
    /*run once every second*/
    
  }else if(timepassed - time_10sec >= 10000){
    time_10sec=timepassed;
    /*run once every 10 seconds*/
    /*calculate the flow based on 10 seconds measure, slow to rise and slow to fall but stable?*/
    //calc=(NbTopsFan *60 / 7.5); //(Pulse frequency x 60) / 7.5Q, = flow rate in L/hour 
    tenseconds_flow=(trigger_flow *60 / 75); //7.5*10 (10 seconds)
    /*count the m3*/
    m3perday=m3perday+trigger_flow*2.25/1000;
    m3ssincereboot=m3ssincereboot+trigger_flow*2.25/1000;
    trigger_flow=0;  
    /*check if last_flowsensor is less than 90% of the new value OR last_flowsensor is greater then 110% of new value = value changed more than 10%  */
    if((last_flowsensor < (tenseconds_flow *0.8)) || (last_flowsensor > (tenseconds_flow *1.2)))
    {
      /*report value */
      //Serial.println("Value changed 20%");
      last_flowsensor=tenseconds_flow;
      dtostrf(tenseconds_flow, 0, 0, res);
      client.publish("kallarevatten/flow", res);
      //char res[10];
      //dtostrf(tenseconds_flow, 0, 0, res);
      //client.publish("kallarevatten/valve", res);
    }
    /*end check if last_flowsensor*/
    /*check last_valve*/
    if(last_valve != valve_state){
          dtostrf(valve_state, 0, 0, res);
          client.publish("kallarevatten/valve", res);
          last_valve=valve_state;
    }
    /*report statechange*/
    /*end check lastvalve*/
    /*end evaluate statechange*/
      //Serial.print(calc, DEC);
      //Serial.println("L/hour");
    /*end of calculate flow*/
    
    /*print the time every 10 seconds*/
    //Serial.println(timeClient.getFormattedTime());
  }else if(timepassed - time_60sec >= 60000){
    time_60sec=timepassed;
    
    //Serial.println(timeClient.getFormattedTime());
    /*run once every minute*/    

  }else if(timepassed - time_10min >= 600000){
    time_10min=timepassed;
    /*run once every 10 minutes*/
    /*send time to network (for debug)*/
    mytime=timeClient.getFormattedTime();
    mytime.toCharArray(res, 10);
    client.publish("kallarevatten/tid", res);
    
    /*check m3*/
    /*m3perday=m3perday+trigger_flow*2.25/1000;
    m3ssincereboot=m3sincereboot+trigger_flow*2.25/1000;
    */
    
    if(m3perday > (m3perday_last+0.05)){
      m3perday_last=m3perday;
     dtostrf(m3perday, 0, 3, res);
     client.publish("kallarevatten/L_perday", res);
     dtostrf(m3ssincereboot, 0, 3, res);
     client.publish("kallarevatten/L_reboot", res);

    }
    /*watervalve between 6:00- 7:00*/
    //if ((timeClient.getHours() < 11) && (timeClient.getHours() > 9)) {
    if ((timeClient.getHours() < 13) && (timeClient.getHours() > 11)) {
      //low is open...
      digitalWrite(VALVE, HIGH);
      valve_state=1;
    }else{
      //high is not open
      digitalWrite(VALVE, LOW);
      valve_state=0;
    }
    /*end watervalve*/  
    }else if(timepassed - time_60min >= 3600000){
    time_60min=timepassed;
    /*run once every 60 minute*/
    if (timeClient.getHours() == 0){
      m3perday=0;
    }
     /*send value as heartbeat*/
     dtostrf(tenseconds_flow, 0, 0, res);
     client.publish("kallarevatten/flow", res);
     /* valve last known state */
     dtostrf(valve_state, 0, 0, res);
     client.publish("kallarevatten/valve", res);
     /*end send value as hartbeat*/
    /*TODO: if no statechange, when do i report?, heartbeat?*/
    /*update ntp client*/
    timeClient.update();
    /*e nd update ntp client*/
  }else{
    /*no timed actions, run once every cycle that nothing else is done*/
    server.handleClient();
  }
  /* no need to have something here, if you do it will run every cycle.*/
}
