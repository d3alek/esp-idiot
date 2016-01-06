#define VERSION 19

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

#include <ArduinoJson.h>

ADC_MODE(ADC_VCC);

#define DHT22_CHIP_ID 320929
#define DHTPIN  2
#define DEFAULT_SLEEP_SECONDS 60*15
#define HARD_RESET_PIN 0
#define WIFI_CONNECT_RETRY_SECONDS 60
#define BUILTIN_LED 2 // on my ESP-12s the blue led is connected to GPIO2 not GPIO1
#define MAX_ATTEMPTS 3

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

IPAddress apIP(192, 168, 1, 1);
IPAddress netMsk(255, 255, 255, 0);

// Initialize DHT sensor 
// Using suggested method in
// https://github.com/esp8266/Arduino/blob/master/doc/libraries.md#esp-specific-apis
DHT dht(DHTPIN, chipId == DHT22_CHIP_ID ? DHT22 : DHT11); // ESP01 has a DHT22 attached, rest have a DHT11

float humidity, temp_c;  // Values read from sensor

// ThingSpeak Settings
char thingspeakWriteApiKey[30];
int sleepSeconds = DEFAULT_SLEEP_SECONDS;

int readSensorTries = 0;
int maxReadSensorTries = 5;

int voltage;

char uuid[15];
char updateTopic[20];
bool configChanged = false;

#define FOREACH_STATE(STATE) \
        STATE(boot)   \
        STATE(setup_wifi)  \
        STATE(connect_to_wifi) \
        STATE(connect_to_mqtt) \
        STATE(update_config) \
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

void blink() {
  turnLedOff();
  delay(500);
  turnLedOn();
}

void handleRoot() {
  blink();
  String content = "<html><body><form action='/wifi-credentials-entered' method='POST'>";
  content += "Wifi Access Point Name:<input type='text' name='wifiName' placeholder='wifi name'><br>";
  content += "Wifi Password:<input type='password' name='wifiPassword' placeholder='wifi password'><br>";
  content += "<input type='submit' name='SUBMIT' value='Submit'></form><br>";
  server.send(200, "text/html", content);
}

void handleWifiCredentialsEntered() {
  blink();
  if (server.hasArg("wifiName") && server.hasArg("wifiPassword")) {
    PersistentStore.putWifiName(server.arg("wifiName").c_str());
    PersistentStore.putWifiPassword(server.arg("wifiPassword").c_str());
    PersistentStore.putWifiAttemptsFailed(0);
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

void loadConfig(char* string) {
    StaticJsonBuffer<300> jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(string);
    if (!root.success()) {
      Serial.print("Could not parse JSON from config string: ");
      Serial.println(string);
      return;
    }
    JsonObject& config = root["state"]["config"];
    if (config.containsKey("thingspeak")) {
      strcpy(thingspeakWriteApiKey, config["thingspeak"]);
    }
    if (config.containsKey("sleep")) {
      sleepSeconds = atoi(config["sleep"]);
    }
}

void injectConfig(JsonObject& config) {
  config["sleep"] = sleepSeconds;
  config["thingspeak"] = thingspeakWriteApiKey;
}

void saveConfig() {
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  JsonObject& stateObject = root.createNestedObject("state");
  JsonObject& config = stateObject.createNestedObject("config");

  injectConfig(config);

  char configString[200];
  root.printTo(configString, 200);
  PersistentStore.putConfig(configString);
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  char normalizedPayload[length+1];
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
  else {
    loadConfig(normalizedPayload);
    configChanged = true;
  }
}

// Appends UUID to topic prefix and saves it in topic. Using separate
// topic and topic prefix due to memory corruption issues otherwise.
void constructTopicName(char* topic, const char* topicPrefix) {
  strcpy(topic, topicPrefix);
  strcat(topic, uuid);
}
bool mqttConnect() {
  Serial.print("MQTT ");
  mqttClient.setServer(mqttHostname, mqttPort).setCallback(mqttCallback);
  if (mqttClient.connect(uuid, "nqquaqbf", "OocuDtvW1p9F")) {
    Serial.println("OK");
    constructTopicName(updateTopic, "update/");
    char deltaTopic[30];
    constructTopicName(deltaTopic, "things/");
    strcat(deltaTopic, "/delta");
    sleepSeconds = DEFAULT_SLEEP_SECONDS;
    mqttClient.subscribe(updateTopic);
    Serial.println(updateTopic);
    mqttClient.subscribe(deltaTopic);
    Serial.println(deltaTopic);
    return true;
  }
  Serial.print("failed, rc=");
  Serial.println(mqttClient.state());
  return false;
}

const char* updateIndex = "<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>";

void turnLedOn() {
  digitalWrite(BUILTIN_LED, LOW);
}

void turnLedOff() {
  digitalWrite(BUILTIN_LED, HIGH);
}

void startWifiCredentialsInputServer() {
  Serial.println("Configuring access point...");
  Serial.setDebugOutput(true);
  delay(1000);
  pinMode(BUILTIN_LED, OUTPUT);
  turnLedOn();
  
  delay(1000);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, netMsk);
  WiFi.softAP(uuid, password);

  IPAddress myIP = WiFi.softAPIP();
  Serial.println(myIP);
  
  server.on("/", handleRoot);
  server.on("/wifi-credentials-entered", handleWifiCredentialsEntered);
  server.on("/update", HTTP_GET, [](){
      blink();
      server.sendHeader("Connection", "close");
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.send(200, "text/html", updateIndex);
    });
  server.on("/update", HTTP_POST, [](){
      blink();
      server.sendHeader("Connection", "close");
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.send(200, "text/plain", (Update.hasError())?"FAIL":"OK");
      ESP.restart();
    },[](){
      blink();
      HTTPUpload& upload = server.upload();
      if(upload.status == UPLOAD_FILE_START){
        Serial.setDebugOutput(true);
        WiFiUDP::stopAll();
        
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
          // update success
        } else {
          Update.printError(Serial);
        }
        Serial.setDebugOutput(false);
      }
      yield();
    });
  server.begin();
  blink();
}

void setupResetButton() {
  pinMode(HARD_RESET_PIN, INPUT);
}

void hardReset() {
  toState(hard_reset);
  PersistentStore.clear();
  deepSleep(1);
}

void loopResetButton() {
  if(!digitalRead(HARD_RESET_PIN)) {
    hardReset();
  }
}

void setup(void)
{
  Serial.begin(115200);

  sprintf(uuid, "%s-%d", uuidPrefix, chipId);
  strcpy(thingspeakWriteApiKey, "");
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
    int attempts = PersistentStore.readWifiAttemptsFailed();
    attempts++;
    Serial.print("Attempts so far: ");
    Serial.print(attempts);
    Serial.print("/");
    Serial.println(MAX_ATTEMPTS);
    if (attempts >= MAX_ATTEMPTS) {
      Serial.println("Doing a hard reset.");
      hardReset();
      deepSleep(1);
      return;
    }
    PersistentStore.putWifiAttemptsFailed(attempts);
    delay(1000);
    loopResetButton();
    deepSleep(WIFI_CONNECT_RETRY_SECONDS);
  }
}

void mqttLoop(int seconds) {
  long startTime = millis();
  while (millis() - startTime < seconds*1000) {
    mqttClient.loop();
    delay(100); 
  }
}

void publishState() {
  Serial.print("Publishing State: ");
  const int maxLength = 300;
  StaticJsonBuffer<maxLength> jsonBuffer;

  JsonObject& root = jsonBuffer.createObject();
  JsonObject& stateObject = root.createNestedObject("state");
  JsonObject& reported = stateObject.createNestedObject("reported");

  reported["version"] = VERSION;
  reported["wifi"] = WiFi.SSID().c_str();
  reported["state"] = STATE_STRING[state];

  JsonObject& config = reported.createNestedObject("config");

  injectConfig(config);
  
  if (state == publish) {
    JsonObject& senses = reported.createNestedObject("senses");
    if (!isnan(temp_c)) {
      senses["temperature"] = temp_c;  
    }
    if (!isnan(humidity)) {
      senses["humidity"] = humidity;
    }
    senses["voltage"] = voltage;
  }
  
  int actualLength = root.measureLength();
  if (actualLength >= maxLength) {
    Serial.println("!!! Resulting JSON is too long, expect errors");
  }
  char value[maxLength];
  root.printTo(value, maxLength);
  char topic[30];
  strcpy(topic,"things/");
  strcat(topic,uuid);
  mqttClient.publish(topic, value, true);
  Serial.println(value);
}

void loop(void)
{
  loopResetButton();
  
  if (state == setup_wifi) {
    server.handleClient();
    delay(1000);
    return;
  }

  mqttLoop(2); // wait 2 seconds for update messages to go through

  toState(update_config);
  if (PersistentStore.configStored()) {
    char config[200];
    PersistentStore.readConfig(config);
    if (config != NULL) {
      loadConfig(config);
    }
  }
  publishState();
  
  mqttLoop(5); // wait 5 seconds for deltas to go through
  
  if (configChanged) {
    saveConfig();
    publishState();
  }
  
  toState(read_senses);
  readTemperatureHumidity();
  readInternalVoltage();

  toState(publish);
  
  publishState();
  
  Serial.println("Published to mqtt.");
  
  if (strcmp(thingspeakWriteApiKey,"")) {
    updateThingspeak(temp_c, humidity, voltage, thingspeakWriteApiKey);
  }
  
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

void updateThingspeak(float temperature, float humidity, int voltage, const char* key)
{
  WiFiClient client;
  const int httpPort = 80;
  Serial.print("Updating Thingspeak ");
  Serial.print(key);
  
  if (client.connect("api.thingspeak.com", 80))
  {         
    client.print("GET ");
    client.print("/update?api_key=");
    client.print(key);
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
