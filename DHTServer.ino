#define VERSION 11

#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>

#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>

#include <DHT.h>
#include <PubSubClient.h>
#include <EEPROM.h>

#include "EspPersistentStore.h"

#include <ESP8266WebServer.h>

ADC_MODE(ADC_VCC);

#define ESP01_ID 16696635
#define DHTPIN  2
#define DEFAULT_SLEEP_SECONDS 60*15
#define HARD_RESET_PIN 0
#define WIFI_CONNECT_RETRY_SECONDS 60

const int chipId = ESP.getChipId();

/* Set these to your desired credentials. */
const char uuidPrefix[] = "ESP";
const char password[] = "very!private!";

ESP8266WebServer server(80);

const char mqttHostname[] = "m20.cloudmqtt.com";
const int mqttPort = 13356;
WiFiClient wclient;
ESP8266WiFiMulti WiFiMulti;
PubSubClient mqttClient(wclient);

// Initialize DHT sensor 
// Using suggested method in
// https://github.com/esp8266/Arduino/blob/master/doc/libraries.md#esp-specific-apis
DHT dht(DHTPIN, DHT11); // ESP01 has a DHT22 attached, rest have a DHT11

float humidity, temp_c;  // Values read from sensor

// ThingSpeak Settings
String thingspeakWriteApiKey = "VYGY61814LFE1KXP";
int sleepSeconds = DEFAULT_SLEEP_SECONDS;

int readSensorTries = 0;
int maxReadSensorTries = 5;

int voltage;

char uuid[15];
char updateTopic[20];
char sleepTopic[25];
char thingspeakKeyTopic[30];
const char updateTopicPrefix[] = "update/";
const char timeTopic[] = "global/time";
const char thingspeakKeyTopicPrefix[] = "config/thingspeak/";
const char sleepTopicPrefix[] = "config/sleep/";

char globalTime[20] = "unknown";

#define FOREACH_STATE(STATE) \
        STATE(boot)   \
        STATE(setup_wifi)  \
        STATE(connect_to_wifi) \
        STATE(connect_to_mqtt) \
        STATE(mqtt_loop)   \
        STATE(read_senses)   \
        STATE(publish) \
        STATE(ota_update)    \
        STATE(deep_sleep)  \
        STATE(hard_reset)  \

#define GENERATE_ENUM(ENUM) ENUM,
#define GENERATE_STRING(STRING) #STRING,

typedef enum {
    FOREACH_STATE(GENERATE_ENUM)
} state_enum;

static const char *STATE_STRING[] = {
    FOREACH_STATE(GENERATE_STRING)
};

int state = 0;

void toState(int newState) {
  Serial.println();
  Serial.print("[");
  Serial.print(STATE_STRING[state]);
  Serial.print("] -> [");
  Serial.print(STATE_STRING[newState]);
  Serial.println("]");
  Serial.println();
  state = newState;
}

void deepSleep(int seconds) {
  toState(deep_sleep);
  ESP.deepSleep(seconds*1000000, WAKE_RF_DEFAULT);
}

void handleRoot() {
  String content = "<html><body><form action='/wifi-credentials-entered' method='POST'>";
  content += "Wifi Access Point Name:<input type='text' name='wifiName' placeholder='wifi name'><br>";
  content += "Wifi Password:<input type='password' name='wifiPassword' placeholder='wifi password'><br>";
  content += "<input type='submit' name='SUBMIT' value='Submit'></form><br>";
  server.send(200, "text/html", content);
}

void handleWifiCredentialsEntered() {
  if (server.hasArg("wifiName") && server.hasArg("wifiPassword")) {
    PersistentStore.putWifiName(server.arg("wifiName").c_str());
    PersistentStore.putWifiPassword(server.arg("wifiPassword").c_str());
    server.send(200, "text/plain", "Thank you, going to try to connect using the given credentials now.");
    deepSleep(1); // sleep for 1 second because restart does not seem to work
  }
  else {
    server.send(200, "text/plain", "No wifi credentials found in POST request.");
  }
}

void updateFromS3(char* updatePath) {
  toState(ota_update);
  char updateUrl[100] = "http://esp-bin.s3-website-eu-west-1.amazonaws.com/";
  strcat(updateUrl, updatePath);
  Serial.print("Starting OTA update: ");
  Serial.println(updateUrl);
  t_httpUpdate_return ret = ESPhttpUpdate.update(updateUrl);

  Serial.print("OTA update finished: ");
  Serial.println(ret);
  switch(ret) {
      case HTTP_UPDATE_FAILED:
          Serial.println("HTTP_UPDATE_FAILED");
          break;

      case HTTP_UPDATE_NO_UPDATES:
          Serial.println("HTTP_UPDATE_NO_UPDATES");
          break;

      case HTTP_UPDATE_OK:
          Serial.println("HTTP_UPDATE_OK");
          break;
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  char normalizedPayload[length];
  for (int i=0;i<length;i++) {
    normalizedPayload[i] = (char)payload[i];
  }
  normalizedPayload[length]='\0';
  Serial.println(normalizedPayload);
  if (strcmp(topic, updateTopic) == 0) {
    unsigned char emptyMessage[0];
    mqttClient.publish(updateTopic, emptyMessage, 0, true);
    updateFromS3(normalizedPayload);
  }
  else if (strcmp(topic, timeTopic) == 0) {
    strcpy(globalTime, normalizedPayload);
  }
  else if (strcmp(topic, thingspeakKeyTopic) == 0) {
    thingspeakWriteApiKey = String(normalizedPayload);
  }
  else if (strcmp(topic, sleepTopic) == 0) {
    sleepSeconds = atoi(normalizedPayload);
  }
  else {
    Serial.print("Don't know how to handle topic: ");
    Serial.println(topic);
  }
}

// Appends UUID to topic prefix and saves it in topic. Using separate
// topic and topic prefix due to memory corruption issues otherwise.
void constructTopicName(char* topic, const char* topicPrefix) {
  topic[0] = 0;
  strcat(topic, topicPrefix);
  strcat(topic, uuid);
}
bool mqttConnect() {
  Serial.print("MQTT ");
  mqttClient.setServer(mqttHostname, mqttPort).setCallback(mqttCallback);
  if (mqttClient.connect(uuid, "nqquaqbf", "OocuDtvW1p9F")) {
    Serial.println("OK");
    constructTopicName(updateTopic, updateTopicPrefix);
    constructTopicName(sleepTopic, sleepTopicPrefix);
    constructTopicName(thingspeakKeyTopic, thingspeakKeyTopicPrefix);
    
    sleepSeconds = DEFAULT_SLEEP_SECONDS;
    mqttClient.subscribe(updateTopic);
    Serial.println(updateTopic);
    mqttClient.subscribe(timeTopic);
    Serial.println(timeTopic);
    mqttClient.subscribe(thingspeakKeyTopic);
    Serial.println(thingspeakKeyTopic);
    mqttClient.subscribe(sleepTopic);
    Serial.println(sleepTopic);
    return true;
  }
  Serial.print("failed, rc=");
  Serial.println(mqttClient.state());
  return false;
}

const char* updateIndex = "<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>";

void startWifiCredentialsInputServer() {
  Serial.print("Configuring access point...");
  delay(1000);
  /* You can remove the password parameter if you want the AP to be open. */
  WiFi.softAP(uuid, password);

  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);
  server.on("/", handleRoot);
  server.on("/wifi-credentials-entered", handleWifiCredentialsEntered);
  server.on("/update", HTTP_GET, [](){
      server.sendHeader("Connection", "close");
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.send(200, "text/html", updateIndex);
    });
  server.on("/update", HTTP_POST, [](){
      server.sendHeader("Connection", "close");
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.send(200, "text/plain", (Update.hasError())?"FAIL":"OK");
      ESP.restart();
    },[](){
      HTTPUpload& upload = server.upload();
      if(upload.status == UPLOAD_FILE_START){
        Serial.setDebugOutput(true);
        WiFiUDP::stopAll();
        Serial.printf("Update: %s\n", upload.filename.c_str());
        uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        if(!Update.begin(maxSketchSpace)){//start with max available size
          Update.printError(Serial);
        }
      } else if(upload.status == UPLOAD_FILE_WRITE){
        if(Update.write(upload.buf, upload.currentSize) != upload.currentSize){
          Update.printError(Serial);
        }
      } else if(upload.status == UPLOAD_FILE_END){
        if(Update.end(true)){ //true to set the size to the current progress
          Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
        } else {
          Update.printError(Serial);
        }
        Serial.setDebugOutput(false);
      }
      yield();
    });
  server.begin();
  Serial.println("HTTP server started");
}

void setupResetButton() {
  pinMode(HARD_RESET_PIN, INPUT);
}

void loopResetButton() {
  if(!digitalRead(HARD_RESET_PIN)) {
    toState(hard_reset);
    PersistentStore.clear();
    deepSleep(1);
  }
}

void setup(void)
{
  Serial.begin(115200);

  sprintf(uuid, "%s-%d", uuidPrefix, chipId);
  Serial.print("UUID: ");
  Serial.println(uuid);
  
  dht.begin();

  PersistentStore.begin();
  setupResetButton();
  loopResetButton();
  
  if (!PersistentStore.wifiCredentialsStored()) {
    toState(setup_wifi);
    startWifiCredentialsInputServer();
    return;
  }
  toState(connect_to_wifi);
  char wifiName[50];
  PersistentStore.readWifiName(wifiName);
  Serial.println(wifiName);
  char wifiPassword[50];
  PersistentStore.readWifiPassword(wifiPassword);
  Serial.println(wifiPassword);
  
  WiFi.begin(wifiName, wifiPassword);
  int wifiConnectResult = WiFi.waitForConnectResult();
  if (wifiConnectResult == WL_CONNECTED) {
    toState(connect_to_mqtt);
    while (!mqttConnect()) {
      delay(1000);
    }
  }
  else {
    Serial.print("Error connecting: ");
    Serial.println(wifiConnectResult);
    delay(1000);
    loopResetButton();
    deepSleep(WIFI_CONNECT_RETRY_SECONDS);
  }
}

void buildPath(char* topic, const char* pathPrefix, const char* pathSuffix) {
  strcpy(topic, pathPrefix);
  strcat(topic, "/");
  strcat(topic, uuid);
  strcat(topic, "/");
  strcat(topic, pathSuffix);
}

void mqttPublish(const char* pathPrefix, const char* pathSuffix, int value) {
  char topic[30];
  buildPath(topic, pathPrefix, pathSuffix);
  mqttClient.publish(topic, String(value).c_str(), true);
}

void mqttPublish(const char* pathPrefix, const char* pathSuffix, float value) {
  char topic[30];
  buildPath(topic, pathPrefix, pathSuffix);
  mqttClient.publish(topic, String(value).c_str(), true);
}

void mqttPublish(const char* pathPrefix, const char* pathSuffix, const char* value) {
  char topic[30];
  buildPath(topic, pathPrefix, pathSuffix);
  mqttClient.publish(topic, value, true);
}

void mqttLoop(int seconds) {
  toState(mqtt_loop);
  long startTime = millis();
  while (millis() - startTime < seconds*1000) {
    mqttClient.loop();
    delay(100); 
  }
}

void loop(void)
{
  loopResetButton();
  
  if (state == setup_wifi) {
    server.handleClient();
    return;
  }

  mqttLoop(5); // wait 5 seconds for any MQTT messages to go through
  toState(read_senses);
  readTemperatureHumidity();
  readInternalVoltage();

  toState(publish);
  mqttPublish("sense", "temperature", temp_c);
  mqttPublish("sense", "humidity", humidity);
  mqttPublish("state", "voltage", voltage);
  mqttPublish("state", "version", VERSION);
  mqttPublish("state", "time", globalTime);
  mqttPublish("state", "sleep", sleepSeconds);
  
  Serial.println("Published to mqtt.");
  updateThingspeak(temp_c, humidity, voltage, thingspeakWriteApiKey);
  
  unsigned long awakeMillis = millis();
  Serial.println(awakeMillis);

  deepSleep(sleepSeconds);
  
} 

void readInternalVoltage() {
  voltage = ESP.getVcc();
  Serial.print("Voltage: ");
  Serial.println(voltage);
}

void readTemperatureHumidity() {
    Serial.print("DHT ");
    delay(2000);
    ++readSensorTries;
    humidity = dht.readHumidity();          // Read humidity (percent)
    temp_c = dht.readTemperature(false);     // Read temperature as Celsius
    if (isnan(humidity) || isnan(temp_c)) {
      Serial.print(readSensorTries);
      Serial.print(" ");
      if (readSensorTries <= maxReadSensorTries) {
        readTemperatureHumidity();
      }
      else {
        Serial.println("Giving up.");
      }
    }
    else {
      Serial.println("OK");
      Serial.print("Temperature: ");
      Serial.println(temp_c);
      Serial.print("Humidity: ");
      Serial.println(humidity);
    }
}

void updateThingspeak(float temperature, float humidity, int voltage, String key)
{
  WiFiClient client;
  const int httpPort = 80;
  Serial.print("Updating Thingspeak " + key);
  
  if (client.connect("api.thingspeak.com", 80))
  {         
    client.print("GET ");
    client.print("/update?api_key=" + key);
    client.print("&field1="+String(temperature));
    client.print("&field2="+String(humidity));
    client.print("&field3="+String(voltage));
    client.print(" HTTP/1.0");
    client.println();
    client.println();
    
    Serial.println(" OK.");
  }
  else
  {
    Serial.println(" Failed.");   
    Serial.println();
  }
}
