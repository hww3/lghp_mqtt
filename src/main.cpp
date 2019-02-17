#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

//needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager

#include <SoftwareSerial.h>
#include <PubSubClient.h>

void publishTopicValue(char* strString, char* value);

SoftwareSerial swSer(0,4);


//char id[5] = {'o','f','f','\0'};
char * id = "office";
#define PREFIX "ha/hvac/"

byte server[] = { 192, 168, 5, 150 };

IPAddress MQTT_Server;
WiFiClient wifiClient;

volatile long lastCharTime = 0;
volatile long lastRx = 0;
int charCount = 0;
unsigned char charBuff[13];
unsigned char charBuffNew[13];

unsigned int checksum1 = 0;
unsigned int checksum2 = 0;
unsigned int lastChecksum = 0;

byte powerByte = 0;
byte fanByte = 0;
byte modeByte = 0;
byte plasmaByte = 0;
byte tempByte = 0;
byte zoneByte = 0; 

long previousMillis = 0;       // will store last time loop ran
long interval = 5000;           // interval at which to limit loop
int i;

long previousMQTTCommand = 0;
int waitForCommand = 1000;
boolean changeWaiting = 0;
boolean justChanged = 0;


//*******************************
//      FUNCTIONS

void callback(char* topic, byte* payload,unsigned int length) {
	int topiclen = strlen(topic);
	Serial.print("Have message from topic ");
	Serial.println(topic);
    // If there has been no MQTT message received for a bit...
    if((millis() - previousMQTTCommand > waitForCommand) && (changeWaiting == 0)) {
      previousMQTTCommand = millis();
      //Copy the existing bytes
      for (int i = 0; i < 13;i++){
        charBuffNew[i] = charBuff[i];
      }
    }
  
    if (topic[topiclen - 2] == '/'){
      if (topic[topiclen - 1] == 'Z'){
        changeWaiting = 1;
        //Serial.println("Zones");
        if (payload[3] == '1'){
          bitWrite(charBuffNew[5],3, 1);
        }else{
          bitWrite(charBuffNew[5],3, 0);
        }
        if (payload[2] == '1'){
          bitWrite(charBuffNew[5],4, 1);
        }else{
          bitWrite(charBuffNew[5],4, 0);
        }
        if (payload[1] == '1'){
          bitWrite(charBuffNew[5],5, 1);
        }else{
          bitWrite(charBuffNew[5],5, 0);
        }
        if (payload[0] == '1'){
          bitWrite(charBuffNew[5],6, 1);
        }else{
          bitWrite(charBuffNew[5],6, 0);
        }
        //Serial.println(charBuffNew[6],BIN);
      }
      if (topic[topiclen - 1] == 'M'){
        changeWaiting = 1;
        if (payload[0] == '0'){  //Cooling
          bitWrite(charBuffNew[1], 2, 0);
          bitWrite(charBuffNew[1], 3, 0);
          bitWrite(charBuffNew[1], 4, 0);
        }
        if (payload[0] == '1'){  //Dehumidify
          bitWrite(charBuffNew[1], 2, 1);
          bitWrite(charBuffNew[1], 3, 0);
          bitWrite(charBuffNew[1], 4, 0);
        }
        if (payload[0] == '2'){  //Fan only
          bitWrite(charBuffNew[1], 2, 0);
          bitWrite(charBuffNew[1], 3, 1);
          bitWrite(charBuffNew[1], 4, 0);
        }
        if (payload[0] == '3'){  //Auto
          bitWrite(charBuffNew[1], 2, 1);
          bitWrite(charBuffNew[1], 3, 1);
          bitWrite(charBuffNew[1], 4, 0);
        }
        if (payload[0] == '4'){  //Heating
          bitWrite(charBuffNew[1], 2, 0);
          bitWrite(charBuffNew[1], 3, 0);
          bitWrite(charBuffNew[1], 4, 1);
        }
      }
      if (topic[topiclen - 1] == 'T'){
        changeWaiting = 1;
        char tmpChar[3] = {payload[0],payload[1], '\0'};  // Convert it to a null terminated string.
        unsigned int tempval = atoi(tmpChar)-15;  // Take off the offset of 15 Degrees.
        bitWrite(charBuffNew[6], 0, bitRead(tempval, 0));  // Write the bits
        bitWrite(charBuffNew[6], 1, bitRead(tempval, 1));
        bitWrite(charBuffNew[6], 2, bitRead(tempval, 2));
        bitWrite(charBuffNew[6], 3, bitRead(tempval, 3));
      }
      if (topic[topiclen - 1] == 'F'){
        changeWaiting = 1;
        if (payload[0] == '0'){  //Low
          bitWrite(charBuffNew[1],5,0);
          bitWrite(charBuffNew[1],6,0);
        }
        if (payload[0] == '1'){  //Med
          bitWrite(charBuffNew[1],5,1);
          bitWrite(charBuffNew[1],6,0);
        }
        if (payload[0] == '2'){  //High
          bitWrite(charBuffNew[1],5,0);
          bitWrite(charBuffNew[1],6,1);
        }
      }
      if (topic[topiclen - 1] == 'P'){
        changeWaiting = 1;
        //Serial.println("Power");
        if (payload[0] == '1'){
          bitWrite(charBuffNew[1],1,1);
        }else{
          bitWrite(charBuffNew[1],1,0);
        }
      }
    }
    if (justChanged == 1 && changeWaiting == 1) changeWaiting = 0;
}

byte calcChecksum(){
  unsigned int checksum;
  checksum2 = 0;
  for (int i = 0; i < 12; i++){
    //Serial.print(charBuff[i]);
    //Serial.print(".");
    checksum2 = checksum2 + charBuffNew[i];
  }

    checksum = checksum2 ^ 0x55;
    return checksum - 256;
}

void serialFlush(){
  while(swSer.available() > 0) {
    char t = swSer.read();
  }
}


void sendConfig(){
  charBuffNew[0] = 40;  // Slave
  charBuffNew[2] = 0;   //Clear out any other misc bits.
  charBuffNew[3] = 0;   //we dont need
  charBuffNew[4] = 0;
  charBuffNew[8] = 0;
  charBuffNew[9] = 0;
  charBuffNew[10] = 0;
  charBuffNew[11] = 0;
  
  //Calculate the checksum for the data
  charBuffNew[12] = calcChecksum();

  //Send it of to the AC
  Serial.println("Sending to AC");
  
  for (int i=0; i < 12; i++){
    Serial.print(charBuffNew[i],DEC);
    Serial.print(",");
  }
  Serial.println("");  
  swSer.write(charBuffNew,13);
  
  // Make sure we are not listening to the data we sent...
  //serialFlush();
}

//MQTT
PubSubClient MQTTClient(server, 1883, callback, wifiClient);
char * subPath;
// = {'h', 'a', '/', 'm', 'o', 'd', '/', '5', '5', '5', '7', '/', '#','\0'};
byte topicNumber = 12;

void mqttConnect() {
  if (!MQTTClient.connected()) {
	  int maxlen = strlen(PREFIX) + strlen(id) + 3;
	  char * subPath = (char *)malloc(maxlen);
	  if(subPath == NULL) Serial.println("failed to allocate memory.");
	  snprintf(subPath, maxlen, "%s%s/#\0", PREFIX, id);
    Serial.println("Connecting to broker");
    if (MQTTClient.connect(id)){
      Serial.print("Subscribing to: ");
      Serial.println(subPath);
      MQTTClient.subscribe(subPath);
      Serial.println("Subscribed!");
    } else {
      Serial.println("Failed to connect!");
    }
  }
}


void publishTopicValue(char* strString, char* value) {
  Serial.print("S:");
  Serial.print(strString);
  Serial.print("/");
  Serial.println(value);
  MQTTClient.publish(strString, value);
}

void publishTopicValueLen(char* strString, char* value, int len) {
  Serial.print("S:");
  Serial.print(strString);
  Serial.print("/");
  Serial.println(value);
  MQTTClient.beginPublish(strString, len, 0);
  MQTTClient.write((byte *)value, len);
  MQTTClient.endPublish();
}

char * make_topic(char * topic_type) {
    int maxlen = strlen(PREFIX) + strlen(id) + strlen(topic_type) + 3;
  char * subPath = (char *)malloc(maxlen);
  if(subPath == NULL) Serial.println("failed to allocate memory.");
  
  snprintf(subPath, maxlen, "%s%s/%s\0", PREFIX, id, topic_type);
  return subPath;
}

void publishSettings(){
    char * topic = make_topic("RAW");

  justChanged = 1;

  publishTopicValueLen(topic, (char*)charBuff, 13);

  free(topic);  
  //Power
  powerByte = bitRead(charBuff[1],1);

  // Fan speed 0-2 = Low, Med, High
  bitWrite(fanByte, 0, bitRead(charBuff[1],5));
  bitWrite(fanByte, 1, bitRead(charBuff[1],6));

  //Mode 0 = Cool, 1 = Dehumidify, 2 = Fan only, 3 = Auto, 4 = Heat
  bitWrite(modeByte,0, bitRead(charBuff[1],2));
  bitWrite(modeByte,1, bitRead(charBuff[1],3));
  bitWrite(modeByte,2, bitRead(charBuff[1],4));

  //Mode 0 = Off, 1 = On
  plasmaByte = (charBuff[2] & 0x04)? 1:0;

  //Set Temp - Binary 0011 -> 1111 = 18 - 30 Deg (decimal 3 offset in value, starts at 18, possibly cool can be set at 15?)
  bitWrite(tempByte,0, bitRead(charBuff[6],0));
  bitWrite(tempByte,1, bitRead(charBuff[6],1));
  bitWrite(tempByte,2, bitRead(charBuff[6],2));
  bitWrite(tempByte,3, bitRead(charBuff[6],3));

  //Zone control - Single bits for each zone
  bitWrite(zoneByte,0, bitRead(charBuff[5],3)); //Zone 4
  bitWrite(zoneByte,1, bitRead(charBuff[5],4)); //Zone 3
  bitWrite(zoneByte,2, bitRead(charBuff[5],5)); //Zone 2
  bitWrite(zoneByte,3, bitRead(charBuff[5],6)); //Zone 1

 // char strPath1[] = {'h', 'a', '/', 'm', 'o', 'd', '/', id[0], id[1], id[2], id[3], '/', 'P','\0'};  // Power State
 // char strPath2[] = {'h', 'a', '/', 'm', 'o', 'd', '/', id[0], id[1], id[2], id[3], '/', 'T','\0'};  //Set Temp
 // char strPath3[] = {'h', 'a', '/', 'm', 'o', 'd', '/', id[0], id[1], id[2], id[3], '/', 'M','\0'};  //Mode
 // char strPath4[] = {'h', 'a', '/', 'm', 'o', 'd', '/', id[0], id[1], id[2], id[3], '/', 'Z','\0'};  //Zones
 // char strPath5[] = {'h', 'a', '/', 'm', 'o', 'd', '/', id[0], id[1], id[2], id[3], '/', 'F','\0'};  //Fan

  char tempChar[2] = {powerByte + 48, '\0'};
  topic = make_topic("P");
  publishTopicValue(topic, tempChar);
  free(subPath);

  
  tempChar[0] = plasmaByte + 48;
  topic = make_topic("L");
  publishTopicValue(topic, tempChar);
  free(topic);
  
  tempChar[0] = modeByte + 48;
  topic = make_topic("M");
  publishTopicValue(topic, tempChar);
  free(topic);

  tempChar[0] = fanByte + 48;
  topic = make_topic("F");
  publishTopicValue(topic, tempChar);
  free(topic);
  
  char charStr[5] = {bitRead(zoneByte,3)+48, bitRead(zoneByte,2)+48, bitRead(zoneByte,1)+48, bitRead(zoneByte,0)+48, '\0'};
  topic = make_topic("Z");
  publishTopicValue(topic, charStr);
  free(topic);
  
  char tmpChar[3];
  char* myPtr = &tmpChar[0];
  snprintf(myPtr, 3, "%02u", tempByte+15);
  topic = make_topic("T");
  publishTopicValue(topic, tmpChar);
  free(topic);
  lastChecksum = charBuff[12];
}

//***********************************************

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println();

//Set up software serial at 104 baud to talk to AC
  swSer.begin(104);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  //exit after config instead of connecting
  wifiManager.setBreakAfterConfig(true);

  //reset settings - for testing
  //wifiManager.resetSettings();


  //tries to connect to last known settings
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP" with password "password"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect()) {
    Serial.println("WifiManager: failed to connect, we should reset as see if it connects");
    delay(3000);
    ESP.reset();
    delay(5000);
  }

  ArduinoOTA.onStart([]() {
    Serial.println("OTA Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA End");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA: Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("OTA: Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("OTA: Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("OTA: Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("OTA: Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("OTA: End Failed");
  });

  ArduinoOTA.setPassword((const char *)"123");

  //if you get here you have connected to the WiFi
  Serial.println("connected...yay :)");

  ArduinoOTA.begin();

  Serial.println("local ip");
  Serial.println(WiFi.localIP());
}


void loop() {
  ArduinoOTA.handle();
 unsigned long currentMillis = millis();

  if(currentMillis - previousMillis > interval) {
    previousMillis = currentMillis;
    mqttConnect();
    if (justChanged == 1){
      justChanged = 0;
      //serialFlush();  //Get rid of any data we received in the mean time.
    }
   }
   
   if((millis() - previousMQTTCommand > waitForCommand) && (changeWaiting == 1)) {  //More than 1 second and we have received an update via MQTT
     //We have received some updates and we need to send them off via serial.
     
     while (millis() - lastRx < 2000) {  //Make sure we are not stomping on someone else sending
       delay(100);                        //Main unit sends every 60 seconds, Master every 20
     }
     
     sendConfig();
     delay(200);
     sendConfig();  //Twice, just to be sure (This is how the factory unit operates...)
     changeWaiting = 0;
   }
   
   MQTTClient.loop();

   if (swSer.available()) {
     lastCharTime=millis();
     charBuff[charCount] = swSer.read();
     charCount++;
//	   Serial.print("Have bytes. ");
//	   Serial.println(charCount);
     if (charCount == 13){
       Serial.print("R: ");
       for (int i=0; i < 12; i++){
         Serial.print(charBuff[i],DEC);
         Serial.print(",");
       }
       Serial.println(charBuff[12],DEC);
       charCount = 0;

//       if (charBuff[0] == 168){ // && charBuff[12] != lastChecksum){  //Only publish data back to MQTT from the Master controller.
         lastRx = millis(); //Track when we received the last 168 (master) packet
         if (changeWaiting == 0) publishSettings(); // If there is nothing pending FROM MQTT, Send the data off to MQTT
//       }
     }
  }

  if (charCount < 13 && millis() - lastCharTime > 100){
    charCount = 0;  //Expired or we got half a packet. Ignore
  }

}
