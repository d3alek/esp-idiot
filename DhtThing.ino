#define VERSION "37.1"

#include <Arduino.h>

#include <ESP8266WiFi.h>

#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>

#include "ESP8266WebServer.h" // including locally because http://stackoverflow.com/a/6506611/5799810

#include <DHT.h>
#include <PubSubClient.h>
#include <EEPROM.h>

#include "EspPersistentStore.h"

#include <ArduinoJson.h>

#include "IdiotLogger.h"

#include "SizeLimitedFileAppender.h"

#include "GPIO.h"

#include <OneWire.h>
#include "OneWireSensors.h"

#include "Action.h"
#include <Ticker.h>

#include "EspControl.h"
ADC_MODE(ADC_VCC);

#define OSWATCH_RESET_TIME 60 // 1 minute
Ticker tickerOSWatch;
static unsigned long last_loop;

#define DEFAULT_SLEEP_SECONDS 60*15
#define HARD_RESET_PIN 0
#define LOCAL_PUBLISH_FILE "localPublish.txt"
#define MAX_STATE_JSON_LENGTH 512
#define MAX_MQTT_CONNECT_ATTEMPTS 3
#define MAX_WIFI_CONNECTED_ATTEMPTS 3

#define MAX_LOCAL_PUBLISH_FILE_BYTES 150000 // 150kb

#define MAX_READ_SENSES_RESULT_SIZE 300

#define ELEVEN_DASHES "-----------"

#define DELTA_WAIT_SECONDS 2

const int chipId = ESP.getChipId();

const char uuidPrefix[] = "ESP";

// file which defines 
// const char* mqttHostname
// const int mqttPort
// const char* mqttUser
// const char* mqttPassword
#include "MQTTConfig.h"

WiFiClient wclient;
PubSubClient mqttClient(wclient);

float voltage = NAN;

int sleepSeconds = DEFAULT_SLEEP_SECONDS;

char uuid[15];
char updateTopic[20];
char updateResultTopic[30];
bool configChanged = false;
#define LOCAL_UPDATE_WAITS 5
int localUpdateWaits = 0;
int mqttConnectAttempts = 0;
int wifiConnectAttempts = 0;
long clientWaitStartedTime;

#include "State.h"
#include "IdiotWifiServer.h"

int state = boot;

IdiotLogger Logger;
IdiotWifiServer idiotWifiServer;

char mode[12];
char value[12];

// devices

int dht11Pin = -1;
int dht22Pin = -1;
int oneWirePin = -1;
char readSensesResult[MAX_READ_SENSES_RESULT_SIZE];

char finalState[MAX_STATE_JSON_LENGTH];

Action actions[30];
int actionsSize = 0;

void ICACHE_RAM_ATTR osWatch(void) {
    unsigned long t = millis();
    unsigned long last_run = abs(t - last_loop);
    if(last_run >= (OSWATCH_RESET_TIME * 1000)) {
      Logger.println("osWatch: restart");
      // save the hit here to eeprom or to rtc memory if needed
        ESP.restart();  // normal reboot 
        //ESP.reset();  // hard reset
    }
}

void toState(int newState) {
  if (state == newState) {
    return;
  }
  Logger.printf("\n[%s] -> [%s]\n", STATE_STRING[state], STATE_STRING[newState]);
  state = newState;
}

void updateFromS3(char* updatePath) {
  toState(ota_update);
  char updateUrl[100];
  strcpy(updateUrl, "http://idiot-esp.s3-website-eu-west-1.amazonaws.com/updates/");
  strcat(updateUrl, updatePath);
  Logger.print("Starting OTA update: ");
  Logger.println(updateUrl);
  t_httpUpdate_return ret = ESPhttpUpdate.update(updateUrl);

  Logger.printf("OTA update finished: %d\n", ret);
  switch(ret) {
      case HTTP_UPDATE_FAILED:
          Logger.printf("HTTP_UPDATE_FAILED Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
          break;

      case HTTP_UPDATE_NO_UPDATES:
          Logger.println("HTTP_UPDATE_NO_UPDATES");
          break;

      case HTTP_UPDATE_OK:
          Logger.println("HTTP_UPDATE_OK");
          break;
  }
  
  return;
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
  loadConfig(normalizedPayload);
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
  if (mqttClient.connect(uuid, mqttUser, mqttPassword)) {
    Logger.println("OK");
    constructTopicName(updateTopic, "update/");
    char deltaTopic[30];
    constructTopicName(deltaTopic, "things/");
    strcat(deltaTopic, "/delta");

    strcpy(updateResultTopic, updateTopic);
    strcat(updateResultTopic,"/result");
    
    sleepSeconds = DEFAULT_SLEEP_SECONDS;
    mqttClient.subscribe(updateTopic);
    Logger.println(updateTopic);
    mqttClient.subscribe(deltaTopic);
    Logger.println(deltaTopic);
    return true;
  }
  Logger.print("failed, rc=");
  Logger.println(mqttClient.state());
  return false;
}

void setupResetButton() {
  pinMode(HARD_RESET_PIN, INPUT);
}

void hardReset() {
  toState(hard_reset);
  PersistentStore.clear();
  EspControl.restart();
}

void loopResetButton() {
  if(!digitalRead(HARD_RESET_PIN)) {
    hardReset();
  }
}

long updateConfigStartTime;
long serveLocallyStartMs;
#define DEFAULT_SERVE_LOCALLY_SECONDS 2
float serveLocallySeconds;

void setup(void)
{
  Serial.println("Setup starting");
  last_loop = millis();
  tickerOSWatch.attach_ms(((OSWATCH_RESET_TIME / 3) * 1000), osWatch);
    
  SPIFFS.begin();
  Logger.begin(115200);
  Logger.setDebugOutput(true); 
  
  sprintf(uuid, "%s-%d", uuidPrefix, chipId);
  Logger.printf("UUID: %s VERSION: %s\n", uuid, VERSION);
  FSInfo fsInfo;
  SPIFFS.info(fsInfo);

  Logger.printf("Used bytes: %d Total bytes: %d\n", fsInfo.usedBytes, fsInfo.totalBytes);

  // GPIO init
  strcpy(mode, ELEVEN_DASHES);
  strcpy(value, ELEVEN_DASHES);
  
  PersistentStore.begin();
  setupResetButton();
  
  WiFi.mode(WIFI_STA);

  updateConfigStartTime = 0;

  serveLocallyStartMs = 0;
  serveLocallySeconds = DEFAULT_SERVE_LOCALLY_SECONDS;
}

void loop(void)
{
  last_loop = millis();
  loopResetButton();
  
  idiotWifiServer.handleClient();
  
  if (state == boot) {
    if (!PersistentStore.wifiCredentialsStored()) {
      toState(serve_locally);
      serveLocallySeconds = 60;
    }
    else {
      toState(connect_to_wifi);
    }

    return;
  }
  else if (state == serve_locally) {
    if (serveLocallyStartMs == 0) {
      WiFi.mode(WIFI_AP);
      idiotWifiServer.start(uuid, LOCAL_PUBLISH_FILE, Logger);
      serveLocallyStartMs = millis();
      Logger.print("Serving locally for ");
      Logger.print(serveLocallySeconds);
      Logger.println(" seconds");
    }
    else if (millis() - serveLocallyStartMs > serveLocallySeconds * 1000) {
      toState(deep_sleep);
    }
    return;
  }
  else if (state == connect_to_wifi) {
    char wifiName[WIFI_NAME_MAX_SIZE];
    PersistentStore.readWifiName(wifiName);
    Logger.println(wifiName);
    char wifiPassword[WIFI_PASS_MAX_SIZE];
    PersistentStore.readWifiPassword(wifiPassword);
    Logger.println(wifiPassword);

    if (strcmp(WiFi.SSID().c_str(), wifiName) || strcmp(WiFi.psk().c_str(), wifiPassword)) {
      Logger.printf("Connecting to %s for the first time\n", wifiName);
      WiFi.begin(wifiName, wifiPassword);
    }
    else {
      WiFi.begin();
    }
    
    int wifiConnectResult = WiFi.waitForConnectResult();
    
    if (wifiConnectResult == WL_CONNECTED) {
      toState(connect_to_mqtt);
    }
    else {
      if (wifiConnectAttempts++ < MAX_WIFI_CONNECTED_ATTEMPTS) {
        delay(1000);
      }
      else {
        toState(load_config); 
      }      
    }
    return;
  }
  else if (state == connect_to_mqtt) {
    while (mqttConnectAttempts++ < MAX_MQTT_CONNECT_ATTEMPTS && !mqttConnect()) { 
      delay(1000);
    }
    toState(load_config);
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
    if (updateConfigStartTime == 0) {
      requestState();
      updateConfigStartTime = millis();
      configChanged = false;
    }
    
    if (configChanged) {
      Logger.println("Config changed.");
      saveConfig();
      yield();
    }
    else if (millis() - updateConfigStartTime < DELTA_WAIT_SECONDS * 1000) {
      mqttClient.loop();
      return; // do not move to next state yet
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
        Logger.printf("INPUT GPIO%d %c\n", gpioNumber, value[i]);
      }
      else if (mode[i] == 'o') {
        pinMode(gpioNumber, OUTPUT);
        digitalWrite(gpioNumber, value[i] == 'h' ? HIGH : LOW);
        Logger.printf("OUTPUT GPIO%d %c for 1 second\n", gpioNumber, value[i]);
        delay(1000);
        digitalWrite(gpioNumber, value[i] == 'h' ? LOW : HIGH);
      }
    }
    toState(read_senses);
  }
  else if (state == read_senses) {
    StaticJsonBuffer<MAX_READ_SENSES_RESULT_SIZE> jsonBuffer;
    JsonObject& senses = jsonBuffer.createObject();
    
    if (dht11Pin != -1) {
      DHT dht11(dht11Pin, DHT11);
      dht11.begin();
      int attempts = 0;
      Serial.printf("Reading DHT11 from pin %d\n", dht11Pin);
      while (!readTemperatureHumidity("DHT11", dht11, senses) && attempts < 3) {
        delay(2000);
        attempts++;
      }
    }
    
    if (dht22Pin != -1) {
      DHT dht22(dht22Pin, DHT22);
      dht22.begin();
      int attempts = 0;
      Serial.printf("Reading DHT22 from pin %d\n", dht22Pin);
      while (!readTemperatureHumidity("DHT22", dht22, senses) && attempts < 3) {
        delay(2000);
        attempts++;
      }
    }

    if (oneWirePin != -1) {    
      IdiotOneWire.readOneWire(Logger, oneWirePin, senses);
    }

    senses.printTo(readSensesResult, MAX_READ_SENSES_RESULT_SIZE);
    Logger.printf("readSensesResult: %s\n", readSensesResult);
    
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
      toState(serve_locally);
    }
  }
  else if (state == publish) {
    char topic[30];
    constructTopicName(topic, "things/");
    strcat(topic, "/update");
    
    Logger.println(finalState);
    Logger.print("Publishing to ");
    Logger.println(topic);
    mqttClient.publish(topic, finalState);
    yield();
    
    toState(serve_locally);
  }
  else if (state == deep_sleep) {
    PersistentStore.putLastAwake(millis());
    Logger.println(millis());
    
    Logger.flush();
    Logger.close();
    EspControl.deepSleep(sleepSeconds);
  }
}

void requestState() {
  char topic[30];
  constructTopicName(topic, "things/");
  strcat(topic, "/get");
  Logger.print("Publishing to ");
  Logger.println(topic);
  mqttClient.publish(topic, "{}");
}

void readInternalVoltage() {
  voltage = ESP.getVcc();
  Logger.print("Voltage: ");
  Logger.println(voltage);
}

bool readTemperatureHumidity(const char* dhtType, DHT dht, JsonObject& jsonObject) {
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
    char sense[15];
    buildSenseKey(sense, dhtType, "t");
    jsonObject[String(sense)] = temp_c;
    buildSenseKey(sense, dhtType, "h");
    jsonObject[String(sense)] = humidity;
    
    return true;
  }
}

void buildSenseKey(char* sense, const char* sensor, const char* readingName) {
  strcpy(sense, sensor);
  strcat(sense, "-");
  strcat(sense, readingName);
}

void loadConfig(char* string) {
  StaticJsonBuffer<400> jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(string);
  if (!root.success()) {
    Logger.print("Could not parse JSON from config string: ");
    Logger.println(string);
    return;
  }
  
  if (root.containsKey("state")) {
    loadConfig(root["state"]["config"]); // properly formatted config stored in flash
  }
  else if (root.containsKey("delta")) {
    loadConfig(root["delta"]); // buggy IoT json
  }
  else {
    Logger.println("Empty delta");
  }
}

void loadConfig(JsonObject& config) {
  if (config.containsKey("version")) {
    const char* version = config["version"];
    if (strcmp(version, VERSION) == 0) {
      Logger.println("Already the correct version. Ignoring update delta.");
    }
    else {
      char fileName[30];
      strcpy(fileName, "idiot-esp-");
      strcat(fileName, version);
      strcat(fileName, ".bin");
      updateFromS3(fileName);
      ESP.restart();
    }
  }
  if (config.containsKey("sleep")) {
    configChanged = true;
    sleepSeconds = atoi(config["sleep"]);
  }
  
  if (config.containsKey("gpio")) {
    loadGpioConfig(config["gpio"]); // properly formatted config stored in flash
  }
  else {
    loadGpioConfig(config); // buggy IoT json
  }
  
  if (config.containsKey("actions")) {
    configChanged = true;
    JsonObject& actionsJson = config["actions"];
    for (JsonObject::iterator it=actionsJson.begin(); it!=actionsJson.end(); ++it)
    {
      Logger.println(it->key);
      Logger.println(it->value.asString());

      Action action;
      strcpy(action.sense, it->key);
      JsonObject& actionDetails = it->value.asObject();
      if (actionDetails == JsonObject::invalid()) {
        Logger.println("ERROR: Could not parse action details");
        continue;
      }
      for (JsonObject::iterator it2=actionDetails.begin(); it2!=actionDetails.end(); ++it2)
      {
        Logger.println(it2->key);
        Logger.println(it2->value.asString());
        parseThresholdDeltaString(it2->key, action);
      }
      actionsSize++;
    }
  } 
}

void loadGpioConfig(JsonObject& gpio) {
  if (gpio.containsKey("mode")) {
    configChanged = true;
    strcpy(mode, gpio["mode"]);
  }
  if (gpio.containsKey("value")) {
    configChanged = true;
    strcpy(value, gpio["value"]);
  }
  int modeSize = strlen(mode);
  char pinBuffer[3];
  for (int i = 0; i < modeSize; ++i) {
    int gpioNumber = GPIO_NUMBER[i];
    if (mode[i] == 's') {
      itoa(gpioNumber,pinBuffer,10);
      if (gpio.containsKey(pinBuffer)) {
        configChanged = true;
        makeDevicePinPairing(gpioNumber, gpio[pinBuffer].asString());
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

  JsonObject& actions = config.createNestedObject("actions");
  injectActions(actions);
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

void injectActions(JsonObject& actionsJson) {
  for (int i = 0; i < actionsSize; ++i) {
    Action action = actions[i];
    JsonObject& thisActionJson = actionsJson.createNestedObject(action.sense);
    char thresholdDeltaString[20];
    buildThresholdDeltaString(thresholdDeltaString, action.threshold, action.delta);
    JsonArray& gpios = thisActionJson.createNestedArray(thresholdDeltaString);
    for (int j = 0; j < action.gpiosSize; ++j) {
      gpios.add(action.gpios[j]);
    } 
  }
}

void buildThresholdDeltaString(char* thresholdDeltaString, float threshold, float delta) {
  sprintf(thresholdDeltaString, "%.2f~%.2f", threshold, delta);
}

void parseThresholdDeltaString(const char* thresholdDeltaString, Action action) {
  action.threshold = 0;
  action.delta = 0;
}

void buildStateString(char* stateJson) {
  StaticJsonBuffer<MAX_STATE_JSON_LENGTH> jsonBuffer;

  JsonObject& root = jsonBuffer.createObject();
  JsonObject& stateObject = root.createNestedObject("state");
  JsonObject& reported = stateObject.createNestedObject("reported");

  reported["version"] = VERSION;
  reported["wifi"] = WiFi.SSID();
  reported["state"] = STATE_STRING[state];
  reported["lawake"] = PersistentStore.readLastAwake();
  
  JsonObject& config = reported.createNestedObject("config");

  injectConfig(config);
  
  StaticJsonBuffer<MAX_READ_SENSES_RESULT_SIZE> readSensesResultBuffer;
  reported["senses"] = readSensesResultBuffer.parseObject(readSensesResult);
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
