#define VERSION 28.13

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

#include "SizeLimitedFileAppender.h"

#include "GPIO.h"

#include <OneWire.h>

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

#define MAX_LOCAL_PUBLISH_FILE_BYTES 150000 // 150kb

#define MAX_READ_SENSORS_RESULT_SIZE 300

#define ELEVEN_DASHES "-----------"

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

float voltage = NAN;

int sleepSeconds = DEFAULT_SLEEP_SECONDS;

char uuid[15];
char updateTopic[20];
char updateResultTopic[30];
char pingTopic[20];
char pongTopic[20];
bool configChanged = false;
#define LOCAL_UPDATE_WAITS 5
int localUpdateWaits = 0;
int mqttConnectAttempts = 0;
int wifiConnectAttempts = 0;
long clientWaitStartedTime;

#define FOREACH_STATE(STATE) \
        STATE(boot)   \
        STATE(setup_wifi)  \
        STATE(connect_to_wifi) \
        STATE(connect_to_mqtt) \
        STATE(serve_locally) \
        STATE(load_config) \
        STATE(update_config) \
        STATE(local_update_config) \
        STATE(process_gpio) \
        STATE(read_sensors) \
        STATE(publish) \
        STATE(local_publish)  \
        STATE(ota_update)    \
        STATE(deep_sleep)  \
        STATE(client_wait) \
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

char mode[12];
char value[12];

// devices

int dht11Pin = -1;
int dht22Pin = -1;
int oneWirePin = -1;
char readSensorsResult[MAX_READ_SENSORS_RESULT_SIZE];


char finalState[MAX_STATE_JSON_LENGTH];

void toState(int newState) {
  if (state == newState) {
    return;
  }
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
  clientWaitStartedTime = millis();
  while (server.client() && clientWaitStartedTime + 5000 > millis()) {
    toState(client_wait);
    yield();
  }
  toState(deep_sleep);
  Logger.flush();
  Logger.close();
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
  Logger.close();
  File logFile = SPIFFS.open(LOG_FILE, "r");
  int logFileSize = logFile.size();
  logFile.flush();
  logFile.seek(0, SeekSet);
  server.sendHeader("Content-Length", String(logFileSize));
  server.sendHeader("Connection", "close");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/html", "");
  WiFiClient client = server.client();

  int sentSize = client.write(logFile, HTTP_DOWNLOAD_UNIT_SIZE);
  
  logFile.close();
  Logger.begin(115200);
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

    strcpy(updateResultTopic, updateTopic);
    strcat(updateResultTopic,"/result");
    
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

void format() {
  SPIFFS.format();
  server.sendHeader("Connection", "close");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/html", "OK");
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
  server.on("/format", format);
  server.on("/wifi-credentials-entered", handleWifiCredentialsEntered);
  server.on("/update", HTTP_GET, httpUpdateGet);
  server.on("/update", HTTP_POST, httpUpdateAnswer, httpUpdateDo);
  server.on("/logs", serveLogs);
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
  server.on("/format", format);
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
  Logger.print("UUID: ");
  Logger.println(uuid);
  Logger.print("VERSION: ");
  Logger.println(VERSION);
  Logger.println("FS info:");
  FSInfo fsInfo;
  SPIFFS.info(fsInfo);

  Logger.print("Used bytes: ");
  Logger.println(fsInfo.usedBytes);
  Logger.print(" Total bytes: ");
  Logger.println(fsInfo.totalBytes); 

  // GPIO init
  strcpy(mode, ELEVEN_DASHES);
  strcpy(value, ELEVEN_DASHES);
  
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
    yield();
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
    mqttLoop(2); // wait 2 seconds for deltas to go through
    if (configChanged) {
      saveConfig();
      yield();
      publishState();
    }
    toState(process_gpio);
  }
  else if (state == local_update_config) {
    if (localUpdateWaits++ < LOCAL_UPDATE_WAITS) {
      delay(1000);
      return;
    }
    else {
      toState(process_gpio);
    }
  }
  else if (state == process_gpio) {
    for (int i = gpio0 ; i  < gpio16 ; ++i) {
      int gpioNumber = GPIO_NUMBER[i];
      if (mode[i] == 'i') {
        pinMode(gpioNumber, INPUT);
        value[i] = digitalRead(gpioNumber) == HIGH ? 'h' : 'l';
        Logger.print("INPUT GPIO");
        Logger.print(gpioNumber);
        Logger.print(" ");
        Logger.println(value[i]);
      }
      else if (mode[i] == 'o') {
        pinMode(gpioNumber, OUTPUT);
        digitalWrite(gpioNumber, value[i] == 'h' ? HIGH : LOW);
        Logger.print("OUTPUT GPIO");
        Logger.print(gpioNumber);
        Logger.print(" ");
        Logger.println(value[i]);
      }
    }
    toState(read_sensors);
  }
  else if (state == read_sensors) {
    
    StaticJsonBuffer<MAX_READ_SENSORS_RESULT_SIZE> jsonBuffer;
    JsonObject& sensors = jsonBuffer.createObject();
    
    if (dht11Pin != -1) {
      JsonObject& dht11Json = sensors.createNestedObject("DHT11");
      DHT dht11(dht11Pin, DHT11);
      dht11.begin();
      int attempts = 0;
      Serial.print("Reading DHT11 from pin ");
      Serial.println(dht11Pin);
      while (!readTemperatureHumidity(dht11, dht11Json) && attempts < 5) {
        delay(2000);
        attempts++;
      }
    }
    
    if (dht22Pin != -1) {
      JsonObject& dht22Json = sensors.createNestedObject("DHT22");
      DHT dht22(dht22Pin, DHT22);
      dht22.begin();
      int attempts = 0;
      Serial.print("Reading DHT22 from pin ");
      Serial.println(dht22Pin);
      while (!readTemperatureHumidity(dht22, dht22Json) && attempts < 5) {
        delay(2000);
        attempts++;
      }
    }

    if (oneWirePin != -1) {
      JsonObject& oneWireJson = sensors.createNestedObject("OneWire");
      
      readOneWire(oneWirePin, oneWireJson);
    }

    sensors.printTo(readSensorsResult, MAX_READ_SENSORS_RESULT_SIZE);
    Logger.print("readSensorsResult: ");
    Logger.println(readSensorsResult);

    readInternalVoltage();
    
    toState(local_publish);
  }
  else if (state == local_publish) {
    buildStateString(finalState);
    yield();
    SizeLimitedFileAppender localPublishFile;
    localPublishFile.open(LOCAL_PUBLISH_FILE, MAX_LOCAL_PUBLISH_FILE_BYTES);
    localPublishFile.println(finalState);
    yield();
    localPublishFile.close();
    if (mqttClient.state() == MQTT_CONNECTED) {
      toState(publish);
    }
    else {
      deepSleep(sleepSeconds);
    }
  }
  else if (state == publish) {
    char topic[30];
    constructTopicName(topic, "things/");
    Logger.print("Publishing State to ");
    Logger.println(topic);
    mqttClient.publish(topic, finalState, true);
    yield();
    
    unsigned long awakeMillis = millis();
    Logger.println(awakeMillis);
  
    deepSleep(sleepSeconds);
  }
} 

// code from http://playground.arduino.cc/Learning/OneWire
void readOneWire(int oneWirePin, JsonObject& jsonObject) {
  OneWire oneWire(oneWirePin);
  byte i;
  byte present = 0;
  byte data[12];
  byte addr[8];

  oneWire.reset_search();
  if ( !oneWire.search(addr)) {
      Logger.print("No more addresses.\n");
      oneWire.reset_search();
      return;
  }

  Logger.print("R=");
  for( i = 0; i < 8; i++) {
    Logger.print(addr[i], HEX);
    Logger.print(" ");
  }

  if ( OneWire::crc8( addr, 7) != addr[7]) {
      Logger.print("CRC is not valid!\n");
      return;
  }

  char device[10];
  if ( addr[0] == 0x10) {
      Logger.print("Device is a DS18S20 family device.\n");
      strcpy(device, "DS18S20");
  }
  else {
    if ( addr[0] == 0x28) {
      Logger.print("Device is a DS18B20 family device.\n");
      strcpy(device, "DS18B20");
    }
    else {
      Logger.print("Device family is not recognized: 0x");
      Logger.println(addr[0],HEX);
      strcpy(device, "UNKNOWN");
    }
    jsonObject[String(device)] = "not supported";
    return;
  }

  oneWire.reset();
  oneWire.select(addr);
  oneWire.write(0x44);         // start conversion, with parasite power on at the end

  delay(1000);     // maybe 750ms is enough, maybe not
  // we might do a ds.depower() here, but the reset will take care of it.

  present = oneWire.reset();
  oneWire.select(addr);    
  oneWire.write(0xBE);         // Read Scratchpad

  Logger.print("P=");
  Logger.print(present,HEX);
  Logger.print(" ");
  for ( i = 0; i < 9; i++) {           // we need 9 bytes
    data[i] = oneWire.read();
    Logger.print(data[i], HEX);
    Logger.print(" ");
  }
  Logger.print(" CRC=");
  Logger.print( OneWire::crc8( data, 8), HEX);
  Logger.println();

  int HighByte, LowByte, TReading, SignBit, Tc_100, Whole, Fract;
  LowByte = data[0];
  HighByte = data[1];
  TReading = (HighByte << 8) + LowByte;
  SignBit = TReading & 0x8000;  // test most sig bit
  if (SignBit) // negative
  {
    TReading = (TReading ^ 0xffff) + 1; // 2's comp
  }
  Tc_100 = (TReading*100/2); // for S family
  // Tc_100 = (6 * TReading) + TReading / 4;    // multiply by (100 * 0.0625) or 6.25 (for B family)

  Whole = Tc_100 / 100;  // separate off the whole and fractional portions
  Fract = Tc_100 % 100;

  char buf[20];
  sprintf(buf, "%c%d.%d",SignBit ? '-' : ' ', Whole, Fract < 10 ? 0 : Fract);
  jsonObject[String(device)] = buf;
  Logger.println(buf);
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

bool readTemperatureHumidity(DHT dht, JsonObject& jsonObject) {
  Logger.print("DHT ");
  
  float temp_c, humidity;
  
  humidity = dht.readHumidity();          // Read humidity (percent)
  temp_c = dht.readTemperature(false);     // Read temperature as Celsius
  if (isnan(humidity) || isnan(temp_c)) {
    Logger.print("fail ");
    return false;
  }
  else {
    Logger.println("OK");
    Logger.print("Temperature: ");
    Logger.println(temp_c);
    Logger.print("Humidity: ");
    Logger.println(humidity);
    jsonObject["t"] = temp_c;
    jsonObject["h"] = humidity;
    
    return true;
  }
}

void loadConfig(char* string) {
  StaticJsonBuffer<400> jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(string);
  if (!root.success()) {
    Logger.print("Could not parse JSON from config string: ");
    Logger.println(string);
    return;
  }
  JsonObject& config = root["state"]["config"];
  
  if (config.containsKey("sleep")) {
    sleepSeconds = atoi(config["sleep"]);
  }
  if (config.containsKey("gpio")) {
    JsonObject& gpio = config["gpio"];
    if (gpio.containsKey("mode")) {
      strcpy(mode, gpio["mode"]);
    }
    if (gpio.containsKey("value")) {
      strcpy(value, gpio["value"]);
    }
    int modeSize = strlen(mode);
    char pinBuffer[3];
    for (int i = 0; i < modeSize; ++i) {
      int gpioNumber = GPIO_NUMBER[i];
      if (mode[i] == 's') {
        itoa(gpioNumber,pinBuffer,10);
        if (gpio.containsKey(pinBuffer)) {
          makeDevicePinPairing(gpioNumber, gpio[pinBuffer].asString());
        }
      }
    }
  }
}

// make sure this is synced with injectConfig
void makeDevicePinPairing(int pinNumber, const char* device) {
  if (strcmp(device, "DHT11") == 0) {
    dht11Pin = pinNumber;
  }
  else if (strcmp(device, "DHT22") == 0) {
    dht22Pin = pinNumber;
  }
  else if (strcmp(device, "OneWire") == 0) {
    oneWirePin = pinNumber;
  }
}

// make sure this is synced with makeDevicePinPairing
void injectConfig(JsonObject& config) {
  config["sleep"] = sleepSeconds;
  
  JsonObject& gpio = config.createNestedObject("gpio");
  gpio["mode"] = mode;
  gpio["value"] = value;
  if (dht11Pin != -1) {
    gpio.set(String(dht11Pin), "DHT11"); // this string conversion is necessary because otherwise ArduinoJson corrupts the key
  }
  if (dht22Pin != -1) {
    gpio.set(String(dht22Pin), "DHT22");
  }
  if (oneWirePin != -1) {
    gpio.set(String(oneWirePin), "OneWire");
  }
}

void saveConfig() {
  StaticJsonBuffer<300> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  JsonObject& stateObject = root.createNestedObject("state");
  JsonObject& config = stateObject.createNestedObject("config");

  injectConfig(config);

  char configString[300];
  root.printTo(configString, 300);
  PersistentStore.putConfig(configString);
}

void buildStateString(char* stateJson) {
  StaticJsonBuffer<MAX_STATE_JSON_LENGTH> jsonBuffer;

  JsonObject& root = jsonBuffer.createObject();
  JsonObject& stateObject = root.createNestedObject("state");
  JsonObject& reported = stateObject.createNestedObject("reported");

  reported["version"] = VERSION;
  reported["wifi"] = WiFi.SSID();
  reported["state"] = STATE_STRING[state];

  JsonObject& config = reported.createNestedObject("config");

  injectConfig(config);
  
  StaticJsonBuffer<MAX_READ_SENSORS_RESULT_SIZE> readSensorsResultBuffer;
  reported["sensors"] = readSensorsResultBuffer.parseObject(readSensorsResult);
    
  if (!isnan(voltage)) {
    reported["voltage"] = voltage;
  }
  int actualLength = root.measureLength();
  if (actualLength >= MAX_STATE_JSON_LENGTH) {
    Logger.println("!!! Resulting JSON is too long, expect errors");
  }

  root.printTo(stateJson, MAX_STATE_JSON_LENGTH);
  Logger.println(stateJson);
  return;
}
