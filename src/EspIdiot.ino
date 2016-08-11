#define VERSION "53"

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

#include <OneWire.h>
#include "OneWireSensors.h"

#include <Wire.h>
#include "I2C.h"

#include "Action.h"

#include "EspControl.h"

#include "GpioState.h"

// file which defines mqttHostname, mqttPort, mqttUser, mqttPassword
#include "MQTTConfig.h"

#include "State.h"
#include "IdiotWifiServer.h"

#define I2C_PIN_1 12
#define I2C_PIN_2 14

ADC_MODE(ADC_VCC);

#define DEFAULT_SLEEP_SECONDS 60*15
#define HARD_RESET_PIN 0
#define LOCAL_PUBLISH_FILE "localPublish.txt"

#define MAX_STATE_JSON_LENGTH 512

#define MAX_MQTT_CONNECT_ATTEMPTS 3
#define MAX_WIFI_CONNECTED_ATTEMPTS 3
#define MAX_LOCAL_PUBLISH_FILE_BYTES 150000 // 150kb
#define MAX_READ_SENSES_RESULT_SIZE 300
#define DELTA_WAIT_SECONDS 2
#define DEFAULT_PUBLISH_INTERVAL 60
#define DEFAULT_SERVE_LOCALLY_SECONDS 2
#define GPIO_SENSE "gpio-sense"
#define MAX_ACTIONS_SIZE 10
#define SENSOR_POWER_PIN 13

unsigned long publishInterval;

const int chipId = ESP.getChipId();

const char uuidPrefix[] = "ESP";

WiFiClient wclient;
PubSubClient mqttClient(wclient);

float voltage = NAN;

int sleepSeconds = DEFAULT_SLEEP_SECONDS;

char uuid[15];
char updateTopic[20];
char updateResultTopic[30];
bool configChanged = false;
bool gpioStateChanged = false;
int mqttConnectAttempts = 0;
int wifiConnectAttempts = 0;

int state = boot;

IdiotLogger Logger(false);
IdiotWifiServer idiotWifiServer;

int dht11Pin = -1;
int dht22Pin = -1;
int oneWirePin = -1;
int gpioSensePin = -1;
char readSensesResult[MAX_READ_SENSES_RESULT_SIZE];

char finalState[MAX_STATE_JSON_LENGTH];

Action actions[MAX_ACTIONS_SIZE];
int actionsSize;

unsigned long lastPublishedAtMillis;
unsigned long updateConfigStartTime;
unsigned long serveLocallyStartMs;
float serveLocallySeconds;

bool couldNotParseAction;

// source: https://github.com/esp8266/Arduino/issues/1532
#include <Ticker.h>
Ticker tickerOSWatch;
#define OSWATCH_RESET_TIME 30

static unsigned long last_loop;

void ICACHE_RAM_ATTR osWatch(void) {
  unsigned long t = millis();
  unsigned long last_run = abs(t - last_loop);
  if(last_run >= (OSWATCH_RESET_TIME * 1000)) {
    Logger.println("osWatch: reset");
    ESP.reset();  // hard reset
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

void setup(void)
{
  last_loop = millis();
  tickerOSWatch.attach_ms(((OSWATCH_RESET_TIME / 3) * 1000), osWatch);
  
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();
  SPIFFS.begin();
  
  if (ESP.getResetReason().equals("Hardware Watchdog")) {
    Serial.println("Clearing state because Hardware Watchdog reset detected.");
    SPIFFS.format();
    ESP.eraseConfig();
    ESP.reset();
  }
  
  Logger.begin();

  Serial.println(ESP.getResetReason());
  Serial.println(ESP.getResetInfo());

  Serial.println("Setup starting");
  
  sprintf(uuid, "%s-%d", uuidPrefix, chipId);
  Logger.printf("UUID: %s VERSION: %s\n", uuid, VERSION);
  FSInfo fsInfo;
  SPIFFS.info(fsInfo);

  Logger.printf("Used bytes: %d Total bytes: %d\n", fsInfo.usedBytes, fsInfo.totalBytes);

  PersistentStore.begin();
  setupResetButton();
  
  lastPublishedAtMillis = 0;
  
  WiFi.mode(WIFI_STA);
}

void loop(void)
{
  last_loop = millis();
  
  loopResetButton();
  
  idiotWifiServer.handleClient();
  
  if (state == boot) {
    wifiConnectAttempts = 0;
    mqttConnectAttempts = 0;
    
    configChanged = false;
    gpioStateChanged = false;
    
    updateConfigStartTime = 0;

    serveLocallyStartMs = 0;
    serveLocallySeconds = DEFAULT_SERVE_LOCALLY_SECONDS;
    publishInterval = DEFAULT_PUBLISH_INTERVAL;
    actionsSize = 0;
    
    dht11Pin = -1;
    dht22Pin = -1;
    oneWirePin = 4;
    gpioSensePin = -1;

    couldNotParseAction = false;
    if (!PersistentStore.wifiCredentialsStored()) {
      toState(serve_locally);
      serveLocallySeconds = 60;
    }
    else {
      toState(connect_to_wifi);
    }

    ensureGpio(SENSOR_POWER_PIN, 0);

    return;
  }
  else if (state == serve_locally) {
    if (serveLocallyStartMs == 0) {
      WiFi.disconnect();
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
    else {
      delay(10);
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

    if (strlen(wifiPassword) == 0) {
      WiFi.begin(wifiName);
    }
    else {
      WiFi.begin(wifiName, wifiPassword);
    }
    int wifiConnectResult = WiFi.waitForConnectResult();
    
    if (wifiConnectResult == WL_CONNECTED) {
      toState(connect_to_internet);
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
  else if (state == connect_to_internet) {
    const char* googleGenerate204 = "http://clients3.google.com/generate_204";
    HTTPClient http;
    Logger.print("[HTTP] Testing for redirection using");
    Logger.println(googleGenerate204);
    http.begin(googleGenerate204);
    int httpCode = http.GET();

    if (httpCode != 204) {
      Logger.print("[HTTP] Redirection detected. GET code: ");
      Logger.println(httpCode);

      String payload = http.getString();
      Logger.println(payload);

      Logger.println("[HTTP] Trying to get past it...");
      http.begin("http://1.1.1.1/login.html");
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
      httpCode = http.POST(String("username=guest&password=guest&buttonClicked=4"));

      Logger.print("[HTTP] POST code: ");
      Logger.println(httpCode);
      Logger.println(http.getString());
    }

    Logger.println("[HTTP] Testing for redirection again");
    http.begin(googleGenerate204);
    httpCode = http.GET();
    if (httpCode != 204) {
      Logger.print("[HTTP] Redirection detected. GET code: ");
      Logger.println(httpCode);
      Logger.println("[HTTP] Could not connect to the internet - stuck behind a login page.");
      toState(load_config);
    }
    else {
      Logger.println("[HTTP] Successfully passed the login page.");
      toState(connect_to_mqtt);
    }
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
    Logger.println("Loaded config:");
    Logger.println(config);
    yield();
    loadConfig(config);
    if (mqttClient.state() == MQTT_CONNECTED) {
      toState(update_config);
    }
    else {
      toState(read_senses);
    }
  }
  else if (state == update_config) { 
    if (updateConfigStartTime == 0) {
      requestState();
      updateConfigStartTime = millis();
      configChanged = false;
    }
    
    if (!configChanged && millis() - updateConfigStartTime < DELTA_WAIT_SECONDS * 1000) {
      mqttClient.loop();
      return; // do not move to next state yet
    }
    else {
      if (configChanged || couldNotParseAction) {
        saveConfig();
      }
      toState(read_senses);
    }
  }
  else if (state == read_senses) {
    ensureGpio(SENSOR_POWER_PIN, 1);
    delay(500);
    
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

    if (gpioSensePin != -1) {
      senses[GPIO_SENSE] = gpioSensePin;
    }

    IdiotI2C.readI2C(Logger, I2C_PIN_1, I2C_PIN_2, senses);

    senses.printTo(readSensesResult, MAX_READ_SENSES_RESULT_SIZE);
    Logger.printf("readSensesResult: %s\n", readSensesResult);

    doActions(senses);

    ensureGpio(SENSOR_POWER_PIN, 0);
    
    readInternalVoltage();
    
    toState(local_publish);
  }
  else if (state == local_publish) {
    if (!configChanged && !gpioStateChanged && lastPublishedAtMillis != 0 && millis() - lastPublishedAtMillis < publishInterval*1000) {
      Logger.println("Skip publishing as published recently.");
      toState(deep_sleep);
    }
    else {
      buildStateString(finalState);
      yield();
      SizeLimitedFileAppender localPublishFile;
      if (!localPublishFile.open(LOCAL_PUBLISH_FILE, MAX_LOCAL_PUBLISH_FILE_BYTES)) {
        Logger.println("Could not open local publish file. Skipping local_publish.");
      }
      else {
        localPublishFile.println(finalState);
        yield();
        localPublishFile.close();
        lastPublishedAtMillis = millis();
      }
      if (mqttClient.state() == MQTT_CONNECTED) {
        toState(publish);
      }
      else {
        toState(deep_sleep);
      }
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
    lastPublishedAtMillis = millis();
    yield();
    
    toState(deep_sleep);
  }
  else if (state == deep_sleep) {
    PersistentStore.putLastAwake(millis());
    Logger.println(millis());

    if (sleepSeconds == 0) {
      WiFi.disconnect();
      delay(10*1000); // 10 second delay to cool things down
      toState(boot);
      return;
    }
    
    Logger.flush();
    Logger.close();
    EspControl.deepSleep(sleepSeconds);
  }
}

void doActions(JsonObject& senses) {
  GpioState.clear();
  for (JsonObject::iterator it=senses.begin(); it!=senses.end(); ++it)
  {
    const char* key = it->key;
    for (int i = 0; i < actionsSize; ++i) {
      Action action = actions[i];
      Logger.print("Configured action ");
      action.printTo(Logger);
      if (!strcmp(action.getSense(), key)) {
        int value = parseValue(it->value); 
        bool aboveThresholdGpioState = action.getAboveThresholdGpioState();
        Logger.printf("Found sense for the action with value [%d]\n", value);
        if (value <= action.getThreshold() - action.getDelta()) {
          Logger.println("Value is below threshold. GPIO should be ");
          Logger.println(aboveThresholdGpioState == LOW ? "high" : "low");
          ensureGpio(action.getGpio(), !aboveThresholdGpioState);
        }
        else if (value >= action.getThreshold() + action.getDelta()) {
          Logger.println("Value is above threshold. GPIO should be ");
          Logger.println(aboveThresholdGpioState == LOW ? "low" : "high");
          ensureGpio(action.getGpio(), aboveThresholdGpioState);
        }
        else {
          Logger.println("Preserving GPIO state");
          GpioState.set(action.getGpio(), digitalRead(action.getGpio()));
        }
      }
    }
  }
}

int parseValue(JsonVariant& valueObject) {
    int value = -2;
    if (valueObject.is<int>()) {
        value = valueObject;
    }
    else if (valueObject.is<const char*>()) {
        const char* valueString = valueObject;
        Logger.printf("Value String [%s]\n", valueString);
        if (valueString == NULL) {
            Logger.println("Could not parse value integer as value is null");
        }
        else {
            value = atoi(valueObject);
        }
    }
    return value;
}

void ensureGpio(int gpio, int state) {
  if (digitalRead(gpio) != state) {
    gpioStateChanged = true;
    Logger.print("Changing GPIO ");
    Logger.print(gpio);
    Logger.print(" to ");
    Logger.println(state);
    
    pinMode(gpio, OUTPUT);
    digitalWrite(gpio, state);
  }
  GpioState.set(gpio, state);
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
  StaticJsonBuffer<CONFIG_MAX_SIZE+100> jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(string);
  if (!root.success()) {
    Logger.print("Could not parse JSON from config string: ");
    Logger.println(string);
    return;
  }
  
  if (root.containsKey("state")) {
    loadConfigFromJson(root["state"]["config"]); // properly formatted config stored in flash
  }
  else if (root.containsKey("delta")) {
    loadConfigFromJson(root["delta"]); // buggy IoT json
  }
  else {
    Logger.println("Empty delta");
  }
}

void loadConfigFromJson(JsonObject& config) {
  if (config.containsKey("version")) {
    const char* version = config["version"];
    if (strcmp(version, VERSION) == 0) {
      configChanged = true; // to push the new version to the cloud, notifying of the successful update
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
    loadActions(config["actions"]);
  }
  else {
    loadActions(config);
  }
}

void loadActions(JsonObject& actionsJson) {
  for (JsonObject::iterator it=actionsJson.begin(); it!=actionsJson.end(); ++it)
  {
    const char* key = it->key;
    if (Action::looksLikeAction(key)) {
      configChanged = true;
      Action action;
      bool success = Action::fromConfig(key, it->value, &action);
      if (!success) {
        Logger.print("Could not parse action: ");
        action.printTo(Logger);
        couldNotParseAction = true;
        continue;
      }
      Logger.print("Found configured action: ");
      action.printTo(Logger);

      bool foundSameSenseGpio = false;
      for (int i = 0; i < actionsSize; ++i) {
        if (strcmp(actions[i].getSense(), action.getSense()) == 0
            && actions[i].getGpio() == action.getGpio()) {
              Logger.print("Replacing: ");
              actions[i].printTo(Logger);
              foundSameSenseGpio = true;
              if (action.getDelta() == -2) {
                Logger.println("Removing action because delta is -2");
                removeAction(i);
              }
              else {
                actions[i] = action;
              }
              break;
            }
      }
      if (!foundSameSenseGpio) {
        if (action.getDelta() == -2) {
          Logger.println("Removing action because delta is -2");
        }
        else if (actionsSize + 1 >= MAX_ACTIONS_SIZE) {
          Logger.println("Too many actions already, ignoring this one/");
        }
        else {
          actions[actionsSize] = action;
          actionsSize++;
        }
      }
    }
  }
}

void removeAction(int index) {
  for (int i = index; i < actionsSize - 1; ++i) {
    actions[i] = actions[i+1];
  }
  actionsSize = actionsSize - 1;
}

void loadGpioConfig(JsonObject& gpio) {
  for (JsonObject::iterator it=gpio.begin(); it!=gpio.end(); ++it)
  {
    char pinBuffer[3];
    const char* key = it->key;
    if (looksLikePinNumber(key)) {
      int pinNumber = atoi(key);
      if (pinNumber == 0) {
        continue;
      }
      configChanged = true;
      makeDevicePinPairing(pinNumber, it->value.asString());
    }
  }
}

bool looksLikePinNumber(const char* string) {
  return strlen(string) < 3;
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
  else if (strcmp(device, GPIO_SENSE) == 0) {
    gpioSensePin = pinNumber;
  }
}

// make sure this is synced with makeDevicePinPairing
void injectConfig(JsonObject& config) {
  config["sleep"] = sleepSeconds;
  
  JsonObject& gpio = config.createNestedObject("gpio");
  if (dht11Pin != -1) {
    gpio.set(String(dht11Pin), "DHT11"); // this string conversion is necessary because otherwise ArduinoJson corrupts the key
  }
  if (dht22Pin != -1) {
    gpio.set(String(dht22Pin), "DHT22");
  }
  if (oneWirePin != -1) {
    gpio.set(String(oneWirePin), "OneWire");
  }
  if (gpioSensePin != -1) {
    gpio.set(String(gpioSensePin), GPIO_SENSE);
  }

  JsonObject& actions = config.createNestedObject("actions");
  injectActions(actions);
}

void saveConfig() {
  StaticJsonBuffer<CONFIG_MAX_SIZE+100> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  JsonObject& stateObject = root.createNestedObject("state");
  JsonObject& config = stateObject.createNestedObject("config");

  injectConfig(config);

  char configString[CONFIG_MAX_SIZE];
  root.printTo(configString, CONFIG_MAX_SIZE);
  Logger.println("Saving config:");
  Logger.println(configString);
  PersistentStore.putConfig(configString);
}

void injectActions(JsonObject& actionsJson) {
  for (int i = 0; i < actionsSize; ++i) {
    Action action = actions[i];
    char thresholdDeltaString[20];
    Action::buildThresholdDeltaString(thresholdDeltaString, action.getThreshold(), action.getDelta());
    char senseAndGpioString[30];
    action.buildSenseAndGpioString(senseAndGpioString);
    actionsJson[String(senseAndGpioString)] = String(thresholdDeltaString);
  }
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
  
  JsonObject& gpio = reported.createNestedObject("mode");
  injectGpioState(gpio);
  
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

void injectGpioState(JsonObject& gpio) { 
  for (int i = 0; i < GpioState.getSize(); ++i) {
    gpio[String(GpioState.getGpio(i))] = GpioState.getState(i);
  }
}

