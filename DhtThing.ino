#define VERSION 24.2

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

#include "IdiotLogger.h"

ADC_MODE(ADC_VCC);

#define DHT22_CHIP_ID 320929
#define DHTPIN  2
#define DEFAULT_SLEEP_SECONDS 60*15
#define HARD_RESET_PIN 0
#define BUILTIN_LED 2 // on my ESP-12s the blue led is connected to GPIO2 not GPIO1
#define LOCAL_PUBLISH_FILE "localPublish.txt"
#define MAX_STATE_JSON_LENGTH 300
#define MAX_MQTT_CONNECT_ATTEMPTS 3
#define MAX_WIFI_CONNECTED_ATTEMPTS 3

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

float humidity = NAN, temp_c = NAN;  // Values read from sensor
float voltage = NAN;

// ThingSpeak Settings
char thingspeakWriteApiKey[30];
int sleepSeconds = DEFAULT_SLEEP_SECONDS;

int readSensorTries = 0;
int maxReadSensorTries = 5;

char uuid[15];
char updateTopic[20];
char pingTopic[20];
char pongTopic[20];
bool configChanged = false;
#define LOCAL_UPDATE_WAITS 5
int localUpdateWaits = 0;
int mqttConnectAttempts = 0;
int wifiConnectAttempts = 0;

#define FOREACH_STATE(STATE) \
        STATE(boot)   \
        STATE(setup_wifi)  \
        STATE(connect_to_wifi) \
        STATE(connect_to_mqtt) \
        STATE(serve_locally) \
        STATE(load_config) \
        STATE(update_config) \
        STATE(local_update_config) \
        STATE(read_senses)   \
        STATE(publish) \
        STATE(local_publish)  \
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

int state = boot;

IdiotLogger Logger;

void toState(int newState) {
  Logger.println();
  Logger.print("[");
  Logger.print(STATE_STRING[state]);
  Logger.print("] -> [");
  Logger.print(STATE_STRING[newState]);
  Logger.println("]");
  Logger.println();
  state = newState;
}

void restart() {
  deepSleep(1);
}

void deepSleep(int seconds) {
  toState(deep_sleep);
  Logger.flush();
  ESP.deepSleep(seconds*1000000, WAKE_RF_DEFAULT);
}

void blink() {
  if (ledOn()) {
    turnLedOff();
    delay(500);
    turnLedOn();
  }
  else {
    turnLedOn();
    delay(500);
    turnLedOff();    
  }
}

void handleRoot() {
  blink();
  String content = "<html><body><form action='/wifi-credentials-entered' method='POST'>";
  content += "Wifi Access Point Name:<input type='text' name='wifiName' placeholder='wifi name'><br>";
  content += "Wifi Password:<input type='password' name='wifiPassword' placeholder='wifi password'><br>";
  content += "<input type='submit' name='SUBMIT' value='Submit'></form><br>";
  server.send(200, "text/html", content);
}

void serveLogs() {
  blink();
  File logFile = Logger.getLogFile();
  int logFileSize = logFile.size();
  logFile.flush();
  logFile.seek(0, SeekSet);
  server.sendHeader("Content-Length", String(logFileSize));
  server.sendHeader("Connection", "close");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/html", "");
  WiFiClient client = server.client();

  int sentSize = client.write(logFile, HTTP_DOWNLOAD_UNIT_SIZE);
  if (sentSize != logFileSize) {
    Logger.println("Sent different data length than expected");
    Logger.print("Expected: "); Logger.println(logFileSize);
    Logger.print("Actual: "); Logger.println(sentSize);
  }
}

void serveLocalPublish() {
  blink();
  File localPublishFile = SPIFFS.open(LOCAL_PUBLISH_FILE, "r");
  int fileSize = localPublishFile.size();
  
  server.sendHeader("Content-Length", String(fileSize));
  server.sendHeader("Connection", "close");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/html", "");
  WiFiClient client = server.client();

  int sentSize = client.write(localPublishFile, HTTP_DOWNLOAD_UNIT_SIZE);
  if (sentSize != fileSize) {
    Logger.println("Sent different data length than expected");
    Logger.print("Expected: "); Logger.println(fileSize);
    Logger.print("Actual: "); Logger.println(sentSize);
  }
}

void handleWifiCredentialsEntered() {
  blink();
  if (server.hasArg("wifiName") && server.hasArg("wifiPassword")) {
    PersistentStore.putWifiName(server.arg("wifiName").c_str());
    PersistentStore.putWifiPassword(server.arg("wifiPassword").c_str());
    server.send(200, "text/plain", "Thank you, going to try to connect using the given credentials now.");
    restart();
  }
  else {
    server.send(200, "text/plain", "No wifi credentials found in POST request.");
  }
}
const char* updateFromS3(char* updatePath) {
  toState(ota_update);
  char updateUrl[100] = "http://idiot-esp.s3-website-eu-west-1.amazonaws.com/updates/";
  strcat(updateUrl, updatePath);
  Logger.print("Starting OTA update: ");
  Logger.println(updateUrl);
  t_httpUpdate_return ret = ESPhttpUpdate.update(updateUrl);

  Logger.print("OTA update finished: ");
  Logger.println(ret);
  switch(ret) {
      case HTTP_UPDATE_FAILED:
          Logger.println("HTTP_UPDATE_FAILED");
          return "HTTP_UPDATE_FAILED";

      case HTTP_UPDATE_NO_UPDATES:
          Logger.println("HTTP_UPDATE_NO_UPDATES");
          return "HTTP_UPDATE_NO_UPDATES";

      case HTTP_UPDATE_OK:
          Logger.println("HTTP_UPDATE_OK");
          return "HTTP_UPDATE_OK";
  }

  return "HTTP_UPDATE_UNKNOWN_RETURN_CODE";
}

void loadConfig(char* string) {
    StaticJsonBuffer<300> jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(string);
    if (!root.success()) {
      Logger.print("Could not parse JSON from config string: ");
      Logger.println(string);
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
  if (strlen(thingspeakWriteApiKey) > 0) {
    config["thingspeak"] = thingspeakWriteApiKey;
  }
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
  Logger.print("Message arrived [");
  Logger.print(topic);
  Logger.print("] ");
  char normalizedPayload[length+1];
  for (int i=0;i<length;i++) {
    normalizedPayload[i] = (char)payload[i];
  }
  normalizedPayload[length]='\0';
  Logger.println(normalizedPayload);
  char updateResultTopic[50];
  strcpy(updateResultTopic, updateTopic);
  strcat(updateResultTopic,"/result");
  if (strcmp(topic, updateTopic) == 0) {
    unsigned char emptyMessage[0];
    mqttClient.publish(updateTopic, emptyMessage, 0, true);
    mqttClient.publish(updateResultTopic, updateFromS3(normalizedPayload), true);
  }
  else if (strcmp(topic, pingTopic) == 0) {
    mqttClient.publish(pongTopic, normalizedPayload);
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
  Logger.print("MQTT ");
  mqttClient.setServer(mqttHostname, mqttPort).setCallback(mqttCallback);
  if (mqttClient.connect(uuid, "nqquaqbf", "OocuDtvW1p9F")) {
    Logger.println("OK");
    constructTopicName(updateTopic, "update/");
    char deltaTopic[30];
    constructTopicName(deltaTopic, "things/");
    strcat(deltaTopic, "/delta");
    constructTopicName(pingTopic, "ping/");
    constructTopicName(pongTopic, "pong/");
    
    sleepSeconds = DEFAULT_SLEEP_SECONDS;
    mqttClient.subscribe(updateTopic);
    Logger.println(updateTopic);
    mqttClient.subscribe(deltaTopic);
    Logger.println(deltaTopic);
    mqttClient.subscribe(pingTopic);
    Logger.println(pingTopic);
    return true;
  }
  Logger.print("failed, rc=");
  Logger.println(mqttClient.state());
  return false;
}

const char* updateIndex = "<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>";

bool ledOn() {
  return digitalRead(BUILTIN_LED) == LOW;
}

void turnLedOn() {
  digitalWrite(BUILTIN_LED, LOW);
}

void turnLedOff() {
  digitalWrite(BUILTIN_LED, HIGH);
}

void clearLogs() {
  if (Logger.clearFile()) {
    Logger.println("Logs cleared.");
    server.send(200, "text/plain", "Success");
  }
  else {
    server.send(200, "text/plain", "Failure");
  }
}

void httpUpdateAnswer() {  
  blink();
  server.sendHeader("Connection", "close");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/plain", (Update.hasError())?"FAIL":"OK");
  restart();
}

void httpUpdateDo() {
  blink();
  HTTPUpload& upload = server.upload();
  if(upload.status == UPLOAD_FILE_START){
    WiFiUDP::stopAll();
    
    uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if(!Update.begin(maxSketchSpace)){//start with max available size
      Update.printError(Logger);
    }
  } else if(upload.status == UPLOAD_FILE_WRITE){
    if(Update.write(upload.buf, upload.currentSize) != upload.currentSize){
      Update.printError(Logger);
    }
  } else if(upload.status == UPLOAD_FILE_END){
    if(Update.end(true)){ //true to set the size to the current progress
      // update success
    } else {
      Update.printError(Logger);
    }
  }
  yield();
}

void httpUpdateGet() {
  blink();
  server.sendHeader("Connection", "close");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/html", updateIndex);
}

void startWifiCredentialsInputServer() {
  Logger.println("Configuring access point...");
  delay(1000);
  pinMode(BUILTIN_LED, OUTPUT);
  turnLedOn();
  
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, netMsk);
  WiFi.softAP(uuid, password);

  IPAddress myIP = WiFi.softAPIP();
  Logger.println(myIP);

  server.on("/", handleRoot);
  server.on("/wifi-credentials-entered", handleWifiCredentialsEntered);
  server.on("/update", HTTP_GET, httpUpdateGet);
  server.on("/update", HTTP_POST, httpUpdateAnswer, httpUpdateDo);
  server.on("/logs", serveLogs);
  server.on("/clear-logs", clearLogs);
  server.on("/local-publish", serveLocalPublish);
  server.begin();
  blink();
}

void startLocalControlServer() {
  Logger.println("Starting local control server...");
  delay(1000);
  pinMode(BUILTIN_LED, OUTPUT);
  turnLedOff();
  
  WiFi.softAPConfig(apIP, apIP, netMsk);
  WiFi.softAP(uuid, password);

  IPAddress myIP = WiFi.softAPIP();
  Logger.println(myIP);

  server.on("/logs", serveLogs);
  server.on("/clear-logs", clearLogs);
  server.on("/local-publish", serveLocalPublish);
  server.on("/wifi-credentials-entered", handleWifiCredentialsEntered);
  server.on("/update", HTTP_GET, httpUpdateGet);
  server.on("/update", HTTP_POST, httpUpdateAnswer, httpUpdateDo);
  server.begin();
}

void setupResetButton() {
  pinMode(HARD_RESET_PIN, INPUT);
}

void hardReset() {
  toState(hard_reset);
  PersistentStore.clear();
  restart();
}

void loopResetButton() {
  if(!digitalRead(HARD_RESET_PIN)) {
    hardReset();
  }
}

void setup(void)
{
  SPIFFS.begin();
  Logger.begin(115200);
  Logger.setDebugOutput(true); 
  
  sprintf(uuid, "%s-%d", uuidPrefix, chipId);
  strcpy(thingspeakWriteApiKey, "");
  Logger.print("UUID: ");
  Logger.println(uuid);
  Logger.print("VERSION: ");
  Logger.println(VERSION);

  dht.begin();

  PersistentStore.begin();
  setupResetButton();
  
  WiFi.mode(WIFI_AP_STA);
}

void mqttLoop(int seconds) {
  long startTime = millis();
  while (millis() - startTime < seconds*1000) {
    mqttClient.loop();
    delay(100); 
  }
}

void buildStateString(char* stateJson) {
  StaticJsonBuffer<MAX_STATE_JSON_LENGTH> jsonBuffer;

  JsonObject& root = jsonBuffer.createObject();
  JsonObject& stateObject = root.createNestedObject("state");
  JsonObject& reported = stateObject.createNestedObject("reported");

  reported["version"] = VERSION;
  reported["wifi"] = WiFi.SSID().c_str();
  reported["state"] = STATE_STRING[state];

  JsonObject& config = reported.createNestedObject("config");

  injectConfig(config);
  
  JsonObject& senses = reported.createNestedObject("senses");
  
  if (!isnan(temp_c)) {
    senses["temperature"] = temp_c;  
  }
  if (!isnan(humidity)) {
    senses["humidity"] = humidity;
  }
  if (!isnan(voltage)) {
    senses["voltage"] = voltage;
  }
  int actualLength = root.measureLength();
  if (actualLength >= MAX_STATE_JSON_LENGTH) {
    Logger.println("!!! Resulting JSON is too long, expect errors");
  }

  root.printTo(stateJson, MAX_STATE_JSON_LENGTH);
  Logger.println(stateJson);
  return;
}

void loop(void)
{
  loopResetButton();
  
  if (state == boot) {
    if (!PersistentStore.wifiCredentialsStored()) {
      toState(setup_wifi);
      startWifiCredentialsInputServer();
      return;
    }
    else {
      toState(connect_to_wifi);
    }
    return;
  }
  else if (state == connect_to_wifi) {
    char wifiName[50];
    PersistentStore.readWifiName(wifiName);
    Logger.println(wifiName);
    char wifiPassword[50];
    PersistentStore.readWifiPassword(wifiPassword);
    Logger.println(wifiPassword);
    
    WiFi.begin(wifiName, wifiPassword);
    int wifiConnectResult = WiFi.waitForConnectResult();
    
    if (wifiConnectResult == WL_CONNECTED) {
      toState(connect_to_mqtt);
    }
    else {
      if (wifiConnectAttempts++ < MAX_WIFI_CONNECTED_ATTEMPTS) {
        delay(1000);
      }
      else {
        toState(serve_locally); 
      }      
    }
    return;
  }
  else if (state == connect_to_mqtt) {
    while (mqttConnectAttempts++ < MAX_MQTT_CONNECT_ATTEMPTS && !mqttConnect()) { 
      delay(1000);
    }
    mqttLoop(2); // wait 2 seconds for update messages to go through
    toState(serve_locally);
    return;
  }
  else if (state == serve_locally) {
    startLocalControlServer();
    toState(load_config);
    return;
  }
  
  server.handleClient();
  
  if (state == setup_wifi) {
    return;
  }
  else if (state == load_config) {
    char config[CONFIG_MAX_SIZE];
    PersistentStore.readConfig(config);
    loadConfig(config);
    if (mqttClient.state() == MQTT_CONNECTED) {
      toState(update_config);
    }
    else {
      toState(local_update_config);
    }
  }
  else if (state == update_config) { 
    publishState();
    mqttLoop(5); // wait 5 seconds for deltas to go through
    if (configChanged) {
      saveConfig();
      publishState();
    }
    toState(read_senses);
  }
  else if (state == local_update_config) {
    if (localUpdateWaits++ < LOCAL_UPDATE_WAITS) {
      delay(1000);
      return;
    }
    else {
      toState(read_senses);
    }
  }
  else if (state == read_senses) {
    if (!readTemperatureHumidity()) {
      delay(2000);
      return; //to repeat next loop
    }
    readInternalVoltage();
    toState(local_publish);
  }
  else if (state == publish) {
    publishState();
    if (strlen(thingspeakWriteApiKey) > 0) {
      updateThingspeak(temp_c, humidity, voltage, thingspeakWriteApiKey);
    }
    
    unsigned long awakeMillis = millis();
    Logger.println(awakeMillis);
  
    deepSleep(sleepSeconds);
  }
  else if (state == local_publish) {
    char state[MAX_STATE_JSON_LENGTH];
    buildStateString(state);
    File localPublishFile = SPIFFS.open(LOCAL_PUBLISH_FILE, "a+");
    localPublishFile.println(state);
    localPublishFile.close();
    if (mqttClient.state() == MQTT_CONNECTED) {
      toState(publish);
    }
    else {
      deepSleep(sleepSeconds);
    }
  }
} 

void publishState() {
    char state[MAX_STATE_JSON_LENGTH];
    buildStateString(state);
    char topic[30];
    constructTopicName(topic, "things/");
    Logger.print("Publishing State to ");
    Logger.println(topic);
    mqttClient.publish(topic, state, true);
}

void readInternalVoltage() {
  voltage = ESP.getVcc();
  Logger.print("Voltage: ");
  Logger.println(voltage);
}

bool readTemperatureHumidity() {
    Logger.print("DHT ");
    
    ++readSensorTries;
    humidity = dht.readHumidity();          // Read humidity (percent)
    temp_c = dht.readTemperature(false);     // Read temperature as Celsius
    if (isnan(humidity) || isnan(temp_c)) {
      Logger.print(readSensorTries);
      Logger.print(" ");
      if (readSensorTries <= maxReadSensorTries) {
        return false;
      }
      else {
        Logger.println("Giving up.");
        return true;
      }
    }
    else {
      Logger.println("OK");
      Logger.print("Temperature: ");
      Logger.println(temp_c);
      Logger.print("Humidity: ");
      Logger.println(humidity);
    }

    return true;
}

void updateThingspeak(float temperature, float humidity, int voltage, const char* key)
{
  WiFiClient client;
  const int httpPort = 80;
  Logger.print("Updating Thingspeak ");
  Logger.print(key);
  
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
    
    Logger.println(" OK.");
  }
  else
  {
    Logger.println(" Failed.");   
    Logger.println();
  }
}
