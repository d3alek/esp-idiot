#define VERSION "z16.992d"

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

#define DEFAULT_ONE_WIRE_PIN 2

#include <Wire.h>
#include "I2C.h"

#include "Action.h"

#include "EspControl.h"

#include "GpioState.h"
#include "GpioMode.h"

// file which defines mqttHostname, mqttPort, mqttUser, mqttPassword
#ifdef DEV
#include "MQTTConfigDev.h"
#else
#include "MQTTConfig.h"
#endif

#include "State.h"
#include "IdiotWifiServer.h"

#include "I2CSoilMoistureSensor.h"

#include "OLED.h"
#include "Displayable.h"
#include "DisplayController.h"

#include "Sense.h"

#define I2C_POWER 16 // attiny's reset - 0 turns them off 1 turns them on
#define I2C_PIN_1 14 // SDA
#define I2C_PIN_2 12 // SDC

#define HARD_RESET_PIN 0
#define LOCAL_PUBLISH_FILE "localPublish.txt"

#define MAX_STATE_JSON_LENGTH 740

#define MAX_MQTT_CONNECT_ATTEMPTS 3
#define MAX_WIFI_CONNECTED_ATTEMPTS 3
#define MAX_LOCAL_PUBLISH_FILE_BYTES 150000 // 150kb
#define MAX_READ_SENSES_RESULT_SIZE 512
#define DELTA_WAIT_SECONDS 2
#define DEFAULT_PUBLISH_INTERVAL 60
#define DEFAULT_SERVE_LOCALLY_SECONDS 2
#define GPIO_SENSE "gpio-sense"
#define MAX_ACTIONS_SIZE 10

#define SENSE_EXPECTATIONS_WINDOW 10

const int chipId = ESP.getChipId();

const char uuidPrefix[] = "ESP";

WiFiClient wclient;
PubSubClient mqttClient(wclient);

char uuid[15];
char updateTopic[20];
char updateResultTopic[30];
bool configChanged = false;
bool gpioStateChanged = false;
int mqttConnectAttempts = 0;
int wifiConnectAttempts = 0;

state_enum state = boot;

IdiotLogger Logger(false);
IdiotWifiServer idiotWifiServer;

int dht11Pin = -1;
int dht22Pin = -1;
int oneWirePin = -1;
char readSensesResult[MAX_READ_SENSES_RESULT_SIZE] = "{}";

char finalState[MAX_STATE_JSON_LENGTH];

Action actions[MAX_ACTIONS_SIZE];
int actionsSize;

unsigned long updateConfigStartTime;
unsigned long serveLocallyStartMs;
float serveLocallySeconds;

#define DISPLAY_CONTROL_PIN 0
OLED oled(I2C_PIN_1, I2C_PIN_2);
DisplayController display(oled);

unsigned long boot_time = -1;

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

void toState(state_enum newState) {
  if (state == newState) {
    return;
  }
  Logger.printf("\n[%s] -> [%s]\n", STATE_STRING[state], STATE_STRING[newState]);

  state = newState;
}

// A manual reset is needed after a Serial flash, otherwise this throws the ESP into oblivion
// See https://github.com/esp8266/Arduino/issues/1722
void updateFromS3(char* updatePath) {
  toState(ota_update);
  display.refresh(state, true);

  char updateUrl[100];
  strcpy(updateUrl, UPDATE_URL);
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
  loadConfig(normalizedPayload, true);
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

void hardReset() {
    toState(hard_reset);

    SPIFFS.format();
    ESP.eraseConfig();

    PersistentStore.clear();
    EspControl.restart();
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

    WiFi.mode(WIFI_STA);

    ensureGpio(PIN_A, 0);
    ensureGpio(PIN_B, 0);
    ensureGpio(PIN_C, 0);
    ensureGpio(I2C_POWER, 0);

    pinMode(HARD_RESET_PIN, INPUT);
    delay(1000);
    if(!digitalRead(HARD_RESET_PIN)) {
        hardReset();
    }

    
}

volatile unsigned long lastInterruptTime = 0;
volatile unsigned long debounceDelay = 300; 

void ICACHE_RAM_ATTR interruptDisplayButtonPressed() {
    Logger.println("interrupt");
    if (millis() - lastInterruptTime > debounceDelay) {
        Logger.println("change mode");
        display.changeMode();
        lastInterruptTime = millis();
    }
}

void loop(void)
{
  last_loop = millis();
  display.refresh(state);
  
  idiotWifiServer.handleClient();

  if (state == boot) {
    pinMode(DISPLAY_CONTROL_PIN, INPUT);
    attachInterrupt(DISPLAY_CONTROL_PIN, interruptDisplayButtonPressed, FALLING);

    wifiConnectAttempts = 0;
    mqttConnectAttempts = 0;
    
    configChanged = false;
    gpioStateChanged = false;
    
    updateConfigStartTime = 0;

    serveLocallyStartMs = 0;
    serveLocallySeconds = DEFAULT_SERVE_LOCALLY_SECONDS;
    actionsSize = 0;

    GpioState.clear();
    GpioMode.clear();
    
    dht11Pin = -1;
    dht22Pin = -1;
    oneWirePin = DEFAULT_ONE_WIRE_PIN;

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
      WiFi.disconnect();
      WiFi.mode(WIFI_AP);
      idiotWifiServer.start(uuid, LOCAL_PUBLISH_FILE, Logger);
      serveLocallyStartMs = millis();
      Logger.print("Serving locally for ");
      Logger.print(serveLocallySeconds);
      Logger.println(" seconds");
    }
    else if (millis() - serveLocallyStartMs > serveLocallySeconds * 1000) {
      toState(cool_off);
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
    Logger.print("[HTTP] Testing for redirection using ");
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
    else {
      Logger.println("[HTTP] Successful internet connectivity test.");
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
    loadConfig(config, false);
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
      if (configChanged) {
        saveConfig();
      }
      toState(read_senses);
    }
  }
  else if (state == read_senses) {
    pinMode(I2C_POWER, OUTPUT);
    digitalWrite(I2C_POWER, 1);
    delay(2000);

    StaticJsonBuffer<MAX_READ_SENSES_RESULT_SIZE> jsonBuffer;
    // parseObject and printTo the same char array do not play well, so pass it a copy here
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

    IdiotI2C.readI2C(Logger, I2C_PIN_1, I2C_PIN_2, senses);
    int brightness = int(((1024 - analogRead(A0))*100) / 1024);

    senses["A0"] = brightness;

    if (boot_time != -1) {
        senses["time"] = seconds_today();
    }

    Serial.println("Enriching senses");
    char readSensesResultCopy[MAX_READ_SENSES_RESULT_SIZE];
    strcpy(readSensesResultCopy, readSensesResult);
    enrichSenses(senses, readSensesResultCopy);

    Serial.println("Updating expectations");
    updateExpectations(senses);

    Serial.println("Validating senses");
    validate(senses);
    
    senses.printTo(readSensesResult, MAX_READ_SENSES_RESULT_SIZE);
    Logger.printf("readSensesResult: %s\n", readSensesResult);

    doActions(senses);

    digitalWrite(I2C_POWER, 0);

    display.update(senses);
    toState(publish);
  }
  else if (state == publish) {
    buildStateString(finalState);
    yield();
    Logger.println(finalState);
    if (mqttClient.state() == MQTT_CONNECTED) {
        char topic[30];
        constructTopicName(topic, "things/");
        strcat(topic, "/update");
        
        Logger.print("Publishing to ");
        Logger.println(topic);
        bool success = mqttClient.publish(topic, finalState);
        if (!success) {
            Logger.println("Failed to publish.");    
        }
        
        toState(cool_off);
    }
    else {
        Logger.println("MQTT not connected so skip publishing.");
        toState(cool_off);
    }
  }
  else if (state == cool_off) {
    detachInterrupt(DISPLAY_CONTROL_PIN); 
    WiFi.disconnect();
    toState(boot);
    delay(1000);
  }
}

void enrichSenses(JsonObject& senses, char* previous_senses_string) {
    // enrich senses with previous expectation, ssd, assume wrong if it starts with "w"
    StaticJsonBuffer<MAX_READ_SENSES_RESULT_SIZE> jsonBuffer;
    JsonObject& previous_senses = jsonBuffer.parseObject(previous_senses_string);
    if (!previous_senses.success()) {
        Serial.println("Could not parse previous senses, assuming none");
        return;
    }

    bool wrong;
    const char* key;
    int value;
    for (JsonObject::iterator it=senses.begin(); it!=senses.end(); ++it) {
        wrong = false;
        key = it->key;
        if (it->value.is<const char*>()) {
            const char* value_string = it->value; 
            if (strlen(value_string) > 0 && value_string[0] == 'w') {
                wrong = true;    
            }
            value = atoi(value_string+1);
        }
        else {
            value = it->value;
        }
        
        senses[key] = Sense().fromJson(previous_senses[key]).withValue(value).withWrong(wrong).toString();
    }
}

bool meetsExpectations(Sense sense) {
    int value = sense.value;
    int expectation = sense.expectation;
    
    float variance = sense.ssd / float(SENSE_EXPECTATIONS_WINDOW-1);
    if (expectation - variance <= value && value <= expectation + variance) {
        return true;
    }
    else {
        return false;
    }
}

void validate(JsonObject& senses) {
    for (JsonObject::iterator it=senses.begin(); it!=senses.end(); ++it) {
        const char* key = it->key;
        Sense sense = Sense().fromJson(it->value);
        int value = sense.value;
        bool wrong = false;
        if (sense.wrong == WRONG_VALUE) {

            wrong = true;
        } 
        else if (!strcmp(key, "I2C-32c")) {
            if (value < 200 || value > 1000) {
                wrong = true;
            }
        }
        else if (!strcmp(key, "I2C-32t")) {
            if (value < -50 || value > 100) {
                wrong = true;
            }
        }
        else if (!strcmp(key, "I2C-8") || !strcmp(key, "I2C-9") || !strcmp(key, "I2C-10")) {
            if (value < 0 || value > 1024) {
                wrong = true;
            }
        }
        else if (!strcmp(key, "OW")) {
            if (value < -100 || value > 100) {
                wrong = true;
            }
        }
        else if (!meetsExpectations(sense)) {
            wrong = true;
        }
        
        if (wrong) {
            senses[key] = sense.withWrong(true).toString();
        }
    }
}

// Modified Welford algorithm to use windwowed-mean instead of true mean
void updateExpectations(JsonObject& senses) {
    int previous_expectation, new_expectation, previous_ssd, new_ssd, value;
    int delta, delta2;
    const char* key;
    Sense sense;
    float prior_weight = float(SENSE_EXPECTATIONS_WINDOW-1)/SENSE_EXPECTATIONS_WINDOW;
    float posterior_weight = 1./SENSE_EXPECTATIONS_WINDOW;
    for (JsonObject::iterator it=senses.begin(); it!=senses.end(); ++it) {
        key = it->key;
        sense = sense.fromJson(it->value);
        value = sense.value;
        previous_expectation = sense.expectation;
        previous_ssd = sense.ssd;

        if (sense.wrong) {
            Logger.printf("Not updating expectations for %s because value marked as wrong", key);
            continue;
        }

        if (previous_expectation == WRONG_VALUE || previous_ssd == WRONG_VALUE) {
            Logger.printf("No expectations yet for %s, seeding with current value %d\n", key, value);
            previous_expectation = value;
            previous_ssd = 10;
        }

        delta = value - previous_expectation;
        new_expectation = previous_expectation * prior_weight + value * posterior_weight; 
        delta2 = value - new_expectation;
        new_ssd = previous_ssd * prior_weight + delta*delta2 * posterior_weight;
        
        Logger.printf("New expectation for %s is %d and new ssd is %d\n", key, new_expectation, new_ssd);
        senses[key] = sense.withExpectationSSD(new_expectation, new_ssd).toString();
    }
}

void doActions(JsonObject& senses) {
    int i = 0;
    bool autoAction[actionsSize];

    for (i = 0; i < actionsSize; ++i) {
        Action action = actions[i];
        Logger.print("Configured action ");
        action.printTo(Logger);
        if (GpioMode.isAuto(action.getGpio())) {
            autoAction[i] = true;
            Logger.print(" is enabled. ");
        }
        else {
            autoAction[i] = false;
            Logger.print(" is disabled as pin mode is not auto.");
        }
    }

    for (JsonObject::iterator it=senses.begin(); it!=senses.end(); ++it)
    {
        const char* key = it->key;
        Sense sense = Sense().fromJson(it->value);
        if (sense.wrong) {
            Logger.printf("Ignoring sense [%s] because value is marked as wrong\n", key);
            continue;
        }

        for (i = 0; i < actionsSize; ++i) {
            if (!autoAction[i]) {
                continue;
            }
            Action action = actions[i];
            if (!strcmp(action.getSense(), key)) {
                bool time_action = !strcmp(key, "time");
                int value = sense.value; 
                if (value == WRONG_VALUE) {
                    Logger.printf("Could parse or wrong value [%d]\n", value);
                    continue;
                }
                bool aboveThresholdGpioState = action.getAboveThresholdGpioState();
                Logger.printf("Found sense for the action with value [%d]\n", value);
                if (time_action) {
                    if (value >= action.getThreshold() && value <= action.getThreshold() + action.getDelta()) {
                        Logger.println("Time is within action period. GPIO should be ");
                        Logger.println(aboveThresholdGpioState == LOW ? "low" : "high");
                        ensureGpio(action.getGpio(), aboveThresholdGpioState);
                    }
                    else {
                        Logger.println("Time is outside of action period. GPIO should be ");
                        Logger.println(aboveThresholdGpioState == LOW ? "high" : "low");
                        ensureGpio(action.getGpio(), !aboveThresholdGpioState);
                    }
                }
                else {
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

    int gpio;
    for (i = 0; i < GpioMode.getSize(); ++i) {
        gpio = GpioMode.getGpio(i);
        if (!GpioMode.isAuto(gpio)) {
            ensureGpio(gpio, GpioMode.getMode(i));
        }
    }
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
  bool success = mqttClient.publish(topic, "{}");
  if (!success) {
    Logger.println("Failed to publish.");    
  }
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
    String sense = buildSenseKey(dhtType, "t");
    jsonObject[sense] = temp_c;
    sense = buildSenseKey(dhtType, "h");
    jsonObject[sense] = humidity;
    
    return true;
  }
}

String buildSenseKey(const char* sensor, const char* readingName) {
    return String(sensor) + "-" + String(readingName);
}

void loadConfig(char* string, bool from_server) {
  StaticJsonBuffer<CONFIG_MAX_SIZE+100> jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(string);
  if (!root.success()) {
    Logger.print("Could not parse JSON from config string: ");
    Logger.println(string);
    Logger.println("Writing in a new valid config.");
    saveConfig();
    return;
  }
  
  loadConfigFromJson(root, from_server);
}

void loadConfigFromJson(JsonObject& config, bool from_server) {
    if (from_server && config.containsKey("version")) {
        const char* version = config["version"];
        if (strcmp(version, VERSION) == 0) {
            configChanged = true; // to push the new version to the cloud, notifying of the successful update
            Logger.println("Already the correct version. Ignoring update delta.");
        }
        else {
            char fileName[20];
            strcpy(fileName, "");
            strcat(fileName, version);
            strcat(fileName, ".bin");
            updateFromS3(fileName);
            ESP.restart();
        }
    }
    if (config.containsKey("gpio")) {
        configChanged = true;
        loadGpioConfig(config["gpio"]);
    }
    if (config.containsKey("mode")) {
        configChanged = true;
        loadMode(config["mode"]);
    }
    if (config.containsKey("actions")) {
        configChanged = true;
        loadActions(config["actions"]);
    }
    if (config.containsKey("t")) {
        int unix_time = config["t"];
        boot_time = unix_time - seconds_since_boot();
    }
}

int seconds_since_boot() {
    return millis() / 1000;
}

int seconds_today() {
    return (boot_time + seconds_since_boot()) % (60*60*24);
}

void loadMode(JsonObject& modeJson) {
    for (JsonObject::iterator it=modeJson.begin(); it!=modeJson.end(); ++it) {
        char pinBuffer[3];
        const char* key = it->key;
        int pinNumber = atoi(key);
        GpioMode.set(pinNumber, it->value.asString());
    }
}

void loadActions(JsonArray& actionsJson) {
    actionsSize = 0;
    for (JsonArray::iterator it=actionsJson.begin(); it!=actionsJson.end(); ++it)
    {
        const char* action_string = *it;
        Action action;
        bool success = action.fromConfig(action_string);

        if (!success) {
            Logger.print("Could not parse action: ");
            action.printTo(Logger);
            continue;
        }
        if (actionsSize + 1 >= MAX_ACTIONS_SIZE) {
            Logger.println("Too many actions already, ignoring the rest");
            break;
        }

        action.printTo(Logger);
        actions[actionsSize++] = action;
    }
}

void loadGpioConfig(JsonObject& gpio) {
  for (JsonObject::iterator it=gpio.begin(); it!=gpio.end(); ++it)
  {
    char pinBuffer[3];
    const char* key = it->key;
    int pinNumber = atoi(key);
    makeDevicePinPairing(pinNumber, it->value.asString());
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

  JsonArray& actions = config.createNestedArray("actions");
  injectActions(actions);

  JsonObject& mode = config.createNestedObject("mode");
  injectGpioMode(mode);
}

void saveConfig() {
  StaticJsonBuffer<CONFIG_MAX_SIZE+100> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();

  injectConfig(root);

  char configString[CONFIG_MAX_SIZE];
  root.printTo(configString, CONFIG_MAX_SIZE);
  Logger.println("Saving config:");
  Logger.println(configString);
  PersistentStore.putConfig(configString);
}

void injectActions(JsonArray& actionsJson) {
  for (int i = 0; i < actionsSize; ++i) {
    Action action = actions[i];
    char action_string[50];
    action.buildActionString(action_string);
    actionsJson.add(String(action_string));
  }
}

void buildStateString(char* stateJson) {
  StaticJsonBuffer<MAX_STATE_JSON_LENGTH> jsonBuffer;

  JsonObject& root = jsonBuffer.createObject();
  JsonObject& stateObject = root.createNestedObject("state");
  JsonObject& reported = stateObject.createNestedObject("reported");

  reported["wifi"] = WiFi.SSID();
  reported["state"] = STATE_STRING[state];
  reported["version"] = VERSION;
  reported["b"] = boot_time;
  
  JsonObject& gpio = reported.createNestedObject("write");
  injectGpioState(gpio);
  
  JsonObject& config = reported.createNestedObject("config");

  injectConfig(config);
  
  StaticJsonBuffer<MAX_READ_SENSES_RESULT_SIZE> readSensesResultBuffer;
  // parseObject modifies the char array, but we need it on next iteration to calculate expectation and variance, so pass it a copy here
  char readSensesResultCopy[MAX_READ_SENSES_RESULT_SIZE];
  strcpy(readSensesResultCopy, readSensesResult);
  reported["senses"] = readSensesResultBuffer.parseObject(readSensesResultCopy);
  
  int actualLength = root.measureLength();
  if (actualLength >= MAX_STATE_JSON_LENGTH) {
    Logger.println("!!! Resulting JSON is too long, expect errors");
  }

  root.printTo(stateJson, MAX_STATE_JSON_LENGTH);
  return;
}

void injectGpioState(JsonObject& gpio) { 
  for (int i = 0; i < GpioState.getSize(); ++i) {
    gpio[String(GpioState.getGpio(i))] = GpioState.getState(i);
  }
}

void injectGpioMode(JsonObject& mode) { 
    int gpio;
    for (int i = 0; i < GpioMode.getSize(); ++i) {
        gpio = GpioMode.getGpio(i);
        if (GpioMode.isAuto(gpio)) {
            mode[String(gpio)] = "a";
        }
        else {
            mode[String(gpio)] = GpioMode.getMode(i);
        }
    }
}

