
#define VERSION 34.61

#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>

#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>

#include <ESP8266WebServer.h> // including because http://stackoverflow.com/a/6506611/5799810

#include <DHT.h>
#include <PubSubClient.h>
#include <EEPROM.h>

#include "EspPersistentStore.h"

#include <Time.h>

#include <ArduinoJson.h>

#include "IdiotLogger.h"

#include "SizeLimitedFileAppender.h"

#include "GPIO.h"

#include <OneWire.h>
#include "OneWireSensors.h"

#include "Action.h"
#include <Ticker.h>

#include "EspControl.h"

#include <AmazonIOTClient.h>
#include <ESP8266AWSImplementations.h>

Esp8266HttpClient httpClient;
Esp8266DateTimeProvider dateTimeProvider;

AmazonIOTClient iotClient;

ADC_MODE(ADC_VCC);

#define OSWATCH_RESET_TIME 60 // 1 minute
Ticker tickerOSWatch;
static unsigned long last_loop;

#define DHT22_CHIP_ID 320929
#define DHTPIN  2
#define DEFAULT_SLEEP_SECONDS 60*15
#define HARD_RESET_PIN 0
#define LOCAL_PUBLISH_FILE "localPublish.txt"
#define MAX_STATE_JSON_LENGTH 512
#define MAX_MQTT_CONNECT_ATTEMPTS 3
#define MAX_WIFI_CONNECTED_ATTEMPTS 3

#define MAX_LOCAL_PUBLISH_FILE_BYTES 150000 // 150kb

#define MAX_READ_SENSES_RESULT_SIZE 300

#define ELEVEN_DASHES "-----------"

#define MAX_DELTA_LENGTH 500

const int chipId = ESP.getChipId();

const char uuidPrefix[] = "ESP";

const char mqttHostname[] = "m20.cloudmqtt.com";
const int mqttPort = 13356;
WiFiClient wclient;
ESP8266WiFiMulti WiFiMulti;
PubSubClient mqttClient(wclient);

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
      // save the hit here to eeprom or to rtc memory if needed
        ESP.restart();  // normal reboot 
        //ESP.reset();  // hard reset
    }
}

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

  iotClient.setAWSRegion("eu-west-1");
  iotClient.setAWSEndpoint("amazonaws.com");
  iotClient.setAWSDomain("A1HYLLFCLCSZTC.iot.eu-west-1.amazonaws.com");
  char awsPath[50];
  strcpy(awsPath, "/things/");
  strcat(awsPath, uuid);
  strcat(awsPath, "/shadow");
  iotClient.setAWSPath(awsPath);
  iotClient.setAWSKeyID("AKIAIYG2N7KAHJE7BNSQ");
  iotClient.setAWSSecretKey("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");
  iotClient.setHttpClient(&httpClient);
  iotClient.setDateTimeProvider(&dateTimeProvider);
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
  last_loop = millis();
  loopResetButton();
  
  if (state == boot) {
    if (!PersistentStore.wifiCredentialsStored()) {
      toState(setup_wifi);
      idiotWifiServer.start(uuid, LOCAL_PUBLISH_FILE, Logger);
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
    idiotWifiServer.start(uuid, LOCAL_PUBLISH_FILE, Logger);
    toState(load_config);
    return;
  }
  
  idiotWifiServer.handleClient();
  
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
    readConfigFromCloud();
    if (configChanged) {
      saveConfig();
      yield();
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
        Logger.print(value[i]);
        Logger.println(" for 1 second");
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
      Serial.print("Reading DHT11 from pin ");
      Serial.println(dht11Pin);
      while (!readTemperatureHumidity("DHT11", dht11, senses) && attempts < 3) {
        delay(2000);
        attempts++;
      }
    }
    
    if (dht22Pin != -1) {
      DHT dht22(dht22Pin, DHT22);
      dht22.begin();
      int attempts = 0;
      Serial.print("Reading DHT22 from pin ");
      Serial.println(dht22Pin);
      while (!readTemperatureHumidity("DHT22", dht22, senses) && attempts < 3) {
        delay(2000);
        attempts++;
      }
    }

    if (oneWirePin != -1) {    
      IdiotOneWire.readOneWire(Logger, oneWirePin, senses);
    }

    senses.printTo(readSensesResult, MAX_READ_SENSES_RESULT_SIZE);
    Logger.print("readSensesResult: ");
    Logger.println(readSensesResult);

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
      toState(deep_sleep);
    }
  }
  else if (state == publish) {
    char topic[30];
    constructTopicName(topic, "things/");
    Logger.println(finalState);
    Logger.print("Publishing to ");
    Logger.println(topic);
    mqttClient.publish(topic, finalState, true);
    yield();
    
    unsigned long awakeMillis = millis();
    Logger.println(awakeMillis);

    toState(deep_sleep);
  }
  else if (state == deep_sleep) {
    EspControl.deepSleep(sleepSeconds);
  }
}

void readConfigFromCloud() {
  Logger.println("Getting shadow... ");
  char* shadow = iotClient.get_shadow();
  Logger.println(shadow);
  char delta[MAX_DELTA_LENGTH];
  Logger.println("Parsing delta... ");
  parseDelta(delta, shadow);
  Logger.println(delta);
}

void parseDelta(char* delta, const char* shadow) {
  char* deltaKeywordStart = strstr(shadow, "delta");
  if (deltaKeywordStart == NULL) {
    Logger.print("No delta in shadow.");
    strcpy(delta, "");
    return;
  }
  char* deltaStart = strchr(deltaKeywordStart, '{');
  int opening = 1;
  int closing = 0;
  int i = 1;
  while (opening != closing && i < MAX_DELTA_LENGTH) {
    if (deltaStart[i] == '{') {
      opening++; 
    }
    else if (deltaStart[i] == '}') {
      closing++;
    }
    ++i;
  }
  strncpy(delta, deltaStart, i);
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
  if (config.containsKey("actions")) {
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
