#define VERSION "z2v0.3"

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

#include "SX1272.h"
#include <SPI.h>
#define SX1272_debug_mode 2
#define RADIO_RFM92_95
#define BAND900

// lora gateway
#define GATEWAY_ADDR 1

// lora slave
#define LORA_MODE  1
#define SLAVE_ADDR 5

#define I2C_POWER 16 // attiny's reset - 0 turns them off 1 turns them on

#define HARD_RESET_PIN 0

#define MAX_STATE_JSON_LENGTH 740

#define MAX_READ_SENSES_RESULT_SIZE 512
#define DELTA_WAIT_SECONDS 2
#define WIFI_WAIT_SECONDS 5
#define COOL_OFF_WAIT_SECONDS 15
#define I2C_POWER_WAIT_SECONDS 3
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
bool configReceived = false;
bool gpioStateChanged = false;

state_enum state = boot;

IdiotWifiServer idiotWifiServer;

int dht11Pin = -1;
int dht22Pin = -1;
int oneWirePin = -1;
char readSensesResult[MAX_READ_SENSES_RESULT_SIZE] = "{}";

char finalState[MAX_STATE_JSON_LENGTH];

Action actions[MAX_ACTIONS_SIZE];
int actionsSize;


unsigned long coolOffStartTime;
unsigned long i2cPowerStartTime;
unsigned long wifiWaitStartTime;
unsigned long updateConfigStartTime;
unsigned long serveLocallyStartMs;
float serveLocallySeconds;

bool lora_gateway = false;

unsigned long boot_time = -1;

#include <Led.h>
Led led(2); // built-in led

// source: https://github.com/esp8266/Arduino/issues/1532
#include <Ticker.h>
Ticker tickerOSWatch;
#define OSWATCH_RESET_TIME 30

static unsigned long last_loop;

void ICACHE_RAM_ATTR osWatch(void) {
  unsigned long t = millis();
  unsigned long last_run = abs(t - last_loop);
  if(last_run >= (OSWATCH_RESET_TIME * 1000)) {
    Serial.println("!!! osWatch: reset");
    ESP.reset();  // hard reset
  }
}

void toState(state_enum newState) {
  if (state == newState) {
    return;
  }
  Serial.printf("\n[%s] -> [%s]\n", STATE_STRING[state], STATE_STRING[newState]);

  state = newState;
}

// A manual reset is needed after a Serial flash, otherwise this throws the ESP into oblivion
// See https://github.com/esp8266/Arduino/issues/1722
void updateFromS3(char* updatePath) {
  toState(ota_update);

  char updateUrl[100];
  strcpy(updateUrl, UPDATE_URL);
  strcat(updateUrl, updatePath);
  Serial.printf("[starting OTA update] %s\n", updateUrl);
  t_httpUpdate_return ret = ESPhttpUpdate.update(updateUrl);

  Serial.printf("OTA update finished: %d\n", ret);
  switch(ret) {
      case HTTP_UPDATE_FAILED:
          Serial.printf("HTTP_UPDATE_FAILED Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
          break;

      case HTTP_UPDATE_NO_UPDATES:
          Serial.println("HTTP_UPDATE_NO_UPDATES");
          break;

      case HTTP_UPDATE_OK:
          Serial.println("HTTP_UPDATE_OK");
          break;
  }
  
  return;
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    Serial.printf("\n? message arrived [%s]\n", topic);
    char normalizedPayload[length+1];
    for (int i=0;i<length;i++) {
        normalizedPayload[i] = (char)payload[i];
    }
    normalizedPayload[length]='\0';
    Serial.println(normalizedPayload);
    configReceived = true;
    loadConfig(normalizedPayload, true);
}

// Appends UUID to topic prefix and saves it in topic. Using separate
// topic and topic prefix due to memory corruption issues otherwise.
void constructTopicName(char* topic, const char* topicPrefix) {
    strcpy(topic, topicPrefix);
    strcat(topic, uuid);
}

bool mqttConnect() {
    Serial.print("? MQTT ");
    if (mqttClient.connected()) {
        Serial.println("ok");
        return true;
    }
    mqttClient.setServer(mqttHostname, mqttPort).setCallback(mqttCallback);
    if (mqttClient.connect(uuid, mqttUser, mqttPassword)) {
        Serial.println("connected");
        constructTopicName(updateTopic, "update/");
        char deltaTopic[30];
        constructTopicName(deltaTopic, "things/");
        strcat(deltaTopic, "/delta");

        strcpy(updateResultTopic, updateTopic);
        strcat(updateResultTopic,"/result");

        mqttClient.subscribe(updateTopic);
        mqttClient.subscribe(deltaTopic);
        Serial.printf("? listening to %s and %s\n", updateTopic, deltaTopic); // do not change syntax here carelessly, esp_logger parses it
        return true;
    }
    Serial.print("failed, rc=");
    Serial.println(mqttClient.state());
    return false;
}

void hardReset() {
    toState(hard_reset);

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

    if (ESP.getResetReason().equals("Hardware Watchdog")) {
        Serial.println("Clearing state because Hardware Watchdog reset detected.");
        ESP.eraseConfig();
        ESP.reset();
    }

    Serial.printf("? reset reason: %s\n? reset info: %s\n", ESP.getResetReason().c_str(), ESP.getResetInfo().c_str());

    Serial.println("[setup]");

    sprintf(uuid, "%s-%d", uuidPrefix, chipId);
    Serial.printf("? thing: %s version: %s\n", uuid, VERSION);

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
    if (millis() - lastInterruptTime > debounceDelay) {
        lastInterruptTime = millis();
    }
}

void loop(void)
{
    last_loop = millis();
    idiotWifiServer.handleClient();
    led.loop();

    if (mqttClient.connected()) {
        mqttClient.loop();
    }

    if (state == boot) {
        configChanged = false;
        configReceived = false;
        gpioStateChanged = false;

        coolOffStartTime = 0;
        i2cPowerStartTime = 0;
        updateConfigStartTime = 0;
        wifiWaitStartTime = 0;

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
            idiotWifiServer.start(uuid);
            serveLocallyStartMs = millis();
            Serial.print("Serving locally for ");
            Serial.print(serveLocallySeconds);
            Serial.println(" seconds");
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
        WiFi.printDiag(Serial);
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("? already connected");
            toState(connect_to_internet);
            return;
        }
        char wifiName[WIFI_NAME_MAX_SIZE];
        PersistentStore.readWifiName(wifiName);
        char wifiPassword[WIFI_PASS_MAX_SIZE];
        PersistentStore.readWifiPassword(wifiPassword);
        Serial.printf("WiFi config: %s %s\n", wifiName, wifiPassword);
        if (strlen(wifiPassword) == 0) {
            WiFi.begin(wifiName);
        }
        else {
            WiFi.begin(wifiName, wifiPassword);
        }

        wifiWaitStartTime = millis();
        toState(wifi_wait);
        return;
    }
    else if (state == wifi_wait) {
        if (WiFi.status() == WL_CONNECTED) {
            toState(connect_to_internet);
        }
        else if (millis() - wifiWaitStartTime > WIFI_WAIT_SECONDS * 1000){
            Serial.println("\n? waited for wifi enough, continue without.");
            toState(load_config); 
        }
        else {
            Serial.print('.');
            delay(50);
        }
        return;
    }
    else if (state == connect_to_internet) {
        const char* googleGenerate204 = "http://clients3.google.com/generate_204";
        HTTPClient http;
        Serial.printf("[check for redirection] %s\n", googleGenerate204);
        http.begin(googleGenerate204);
        int httpCode = http.GET();

        if (httpCode != 204) {
            Serial.printf("? redirection detected. GET code: %d\n", httpCode);

            Serial.println(http.getString());

            Serial.println("[get past redirection]");
            http.begin("http://1.1.1.1/login.html");
            http.addHeader("Content-Type", "application/x-www-form-urlencoded");
            httpCode = http.POST(String("username=guest&password=guest&buttonClicked=4"));

            Serial.printf("? result: %d\n", httpCode);
            Serial.println(http.getString());

            Serial.println("[check for redirection again]");
            http.begin(googleGenerate204);
            httpCode = http.GET();
            if (httpCode != 204) {
                Serial.printf("? redirection detected. GET code: %d\n", httpCode);
                Serial.println("!!! WiFi connected but no access to internet - maybe stuck behind a login page.");
                WiFi.disconnect(); // so that next connect_wifi we reconnect 
                toState(load_config);
            }
            else {
                Serial.println("? successfully passed the login page.");
                toState(connect_to_mqtt);
            }
        }
        else {
            Serial.println("? connected to internet");
            toState(connect_to_mqtt);
        }
    }
    else if (state == connect_to_mqtt) {
        mqttConnect();
        toState(load_config);
        return;
    }
    else if (state == load_config) {
        char config[CONFIG_MAX_SIZE];
        PersistentStore.readConfig(config);
        Serial.printf("[stored config]\n%s\n", config);
        yield();
        loadConfig(config, false);
        if (mqttClient.connected()) {
            toState(update_config);
        }
        else {
            toState(setup_lora);
        }
    }
    else if (state == update_config) { 
        if (updateConfigStartTime == 0) {
            requestState();
            updateConfigStartTime = millis();
            configChanged = false;
            configReceived = false;
        }

        if (!configReceived && millis() - updateConfigStartTime < DELTA_WAIT_SECONDS * 1000) {
            Serial.print('.');    
            return;
        }
        else {
            if (!configReceived) {
                // maybe server has problems or our connection to it has problems, disconnect to be sure
                mqttClient.disconnect();
            }
            if (configChanged) {
                saveConfig();
            }
            toState(setup_lora);
        }
    }
    else if (state == setup_lora) {
        int e;
        if (lora_gateway) {
            // Print a start message
            Serial.println(F("SX1272 module and Arduino: receive packets without ACK"));

            // Power ON the module
            e = sx1272.ON();
            Serial.print(F("Setting power ON: state "));
            Serial.println(e, DEC);

            // Set transmission mode and print the result
            e = sx1272.setMode(1);
            Serial.print(F("Setting Mode: state "));
            Serial.println(e, DEC);

            // Set header
            e = sx1272.setHeaderON();
            Serial.print(F("Setting Header ON: state "));
            Serial.println(e, DEC);

            // Select frequency channel
            e = sx1272.setChannel(CH_05_900);
            Serial.print(F("Setting Channel: state "));
            Serial.println(e, DEC);


            // Select output power (Max, High or Low)
            e = sx1272.setPower('x');
            Serial.print(F("Setting Power: state "));
            Serial.println(e, DEC);

            // Set the node address and print the result
            e = sx1272.setNodeAddress(GATEWAY_ADDR);
            Serial.print(F("Setting node address: state "));
            Serial.println(e, DEC);

            // Print a success message
            Serial.println(F("SX1272 successfully configured"));
            Serial.println();
            toState(listen_lora);
        }
        else { // LoRa slave based on https://github.com/marciolm/EspComLoRa/blob/master/EspComLora_Simple_temp_deepsleep/EspComLora_Simple_temp_deepsleep.ino
            Serial.println("LoRa slave");
            sx1272.ON();
            e = sx1272.setMode(LORA_MODE);
            Serial.printf("Setting Mode: %d\n", e);
            // sx1272._enableCarrierSense=true; // dunno what this is
            e = sx1272.setChannel(CH_05_900);
            Serial.printf("Setting Channel: %d\n", e);
            e = sx1272.setPower('x');
            Serial.printf("Setting Power: %d\n", e);

            e = sx1272.setNodeAddress(SLAVE_ADDR);
            Serial.printf("Setting Node Address: %d\n", e);
            Serial.println(F("SX1272 successfully configured"));
            toState(send_lora);
        }
    }
    else if (state == listen_lora) {
        int e = sx1272.receivePacketTimeout(10000);
        char packet[100];
        Serial.printf("Receive packet result: %d\n", e);
        if (e == 0) {
            for (unsigned int i = 0; i < sx1272.packet_received.length; i++) {
                packet[i] = (char)sx1272.packet_received.data[i];
            }
            Serial.printf("? packet is %s\n", packet);
            led.blink_fast(3); 
        }
        else {
            led.blink_slow(1);
        }
        toState(publish);
    }
    else if (state == send_lora) {
        sx1272.setPacketType(PKT_TYPE_DATA);
        char message[] = "Hello gateway!";
        int e = sx1272.sendPacketTimeout(GATEWAY_ADDR, message);
        Serial.printf("Send packet result: %d\n", e);
        if (e == 0) {
            led.blink_fast(3); 
        }
        else {
            led.blink_slow(1);
        }

        toState(publish);
    }
    else if (state == publish) {
        buildStateString(finalState);
        yield();
        Serial.printf("[final state]\n%s\n", finalState);
        if (mqttClient.connected()) {
            char topic[30];
            constructTopicName(topic, "things/");
            strcat(topic, "/update");

            Serial.printf("publish to %s\n", topic);
            bool success = mqttClient.publish(topic, finalState);
            if (!success) {
                Serial.println("!!! failed to publish");    
                mqttClient.disconnect();
            }

            toState(cool_off);
        }
        else {
            Serial.println("? MQTT not connected so skip publishing.");
            toState(cool_off);
        }
    }
    else if (state == cool_off) {
        if (coolOffStartTime == 0) {
            coolOffStartTime = millis();
        }
        else if (millis() - coolOffStartTime < COOL_OFF_WAIT_SECONDS * 1000) {
            Serial.print('.');
            delay(100);
        }
        else {
            toState(boot);
        }
    }
}

void enrichSenses(JsonObject& senses, char* previous_senses_string) {
    Serial.println("[enrich senses]");
    // enrich senses with previous expectation, ssd, assume wrong if it starts with "w"
    StaticJsonBuffer<MAX_READ_SENSES_RESULT_SIZE> jsonBuffer;
    JsonObject& previous_senses = jsonBuffer.parseObject(previous_senses_string);
    if (!previous_senses.success()) {
        Serial.println("? could not parse previous senses, assuming none");
    }

    bool wrong;
    const char* key;
    int value;
    for (JsonObject::iterator it=senses.begin(); it!=senses.end(); ++it) {
        wrong = false;
        key = it->key;
        if (!strcmp(key, "time")) {
            continue;
        }
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

bool meetsExpectations(const char* name, Sense sense) {
    int value = sense.value;
    int expectation = sense.expectation;
    if (expectation == WRONG_VALUE || sense.ssd == WRONG_VALUE) {
        return true;
    }
    
    float variance = sqrt(sense.ssd / float(SENSE_EXPECTATIONS_WINDOW-1));
    bool result = (expectation - 2*variance <= value && value <= expectation + 2*variance);
    Serial.printf("? %s %s: %d <= %d <= %d\n", name, (result ? "ok": "wrong"), int(expectation - 2*variance), value, int(expectation + 2*variance));
    return result;
}

void validate(JsonObject& senses) {
    Serial.println("[validate senses]");
    for (JsonObject::iterator it=senses.begin(); it!=senses.end(); ++it) {
        const char* key = it->key;
        if (!strcmp(key, "time")) {
            continue;
        }
        Sense sense = Sense().fromJson(it->value);
        int value = sense.value;
        if (sense.wrong) {
            continue;
        }

        bool wrong = false;

        if (sense.value == WRONG_VALUE) {

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

        if (!wrong && !meetsExpectations(key, sense)) {
            wrong = true;
        }
        
        if (wrong) {
            senses[key] = sense.withWrong(true).toString();
        }
    }
}

// Modified Welford algorithm to use windwowed-mean instead of true mean
void updateExpectations(JsonObject& senses) {
    Serial.println("[update expectations]");
    int previous_expectation, new_expectation, previous_ssd, new_ssd, value;
    int delta, delta2;
    const char* key;
    Sense sense;
    float prior_weight = float(SENSE_EXPECTATIONS_WINDOW-1)/SENSE_EXPECTATIONS_WINDOW;
    float posterior_weight = 1./SENSE_EXPECTATIONS_WINDOW;
    float ssd_update;
    for (JsonObject::iterator it=senses.begin(); it!=senses.end(); ++it) {
        key = it->key;
        if (!strcmp(key, "time")) {
            continue;
        }
        sense = sense.fromJson(it->value);
        value = sense.value;
        previous_expectation = sense.expectation;
        previous_ssd = sense.ssd;

        if (previous_expectation == WRONG_VALUE || previous_ssd == WRONG_VALUE) {
            Serial.printf("? no expectations yet for %s, seeding with current value %d\n", key, value);
            previous_expectation = value;
            previous_ssd = 5 * 5 * SENSE_EXPECTATIONS_WINDOW * SENSE_EXPECTATIONS_WINDOW; // variance is at least 5^2 
        }

        delta = value - previous_expectation;
        new_expectation = previous_expectation * prior_weight + value * posterior_weight; 
        if (new_expectation < value) {
            new_expectation += 1; // adding 1 because of integer rounding
        }
        delta2 = value - new_expectation;
        ssd_update = delta*delta2 * posterior_weight;
        if (ssd_update < 1) {
            ssd_update = 1; // always overestimate variance instead of underestimating it due to integer rounding
        }
        new_ssd = previous_ssd * prior_weight + ssd_update;
        
        Serial.printf("? %s expectation: %d->%d ; ssd %d->%d\n", key, previous_expectation, new_expectation, previous_ssd, new_ssd);
        senses[key] = sense.withExpectationSSD(new_expectation, new_ssd).toString();
    }
}

void doActions(JsonObject& senses) {
    Serial.println("[do actions]");
    int i = 0;
    bool autoAction[actionsSize];

    for (i = 0; i < actionsSize; ++i) {
        Action action = actions[i];
        Serial.print("? configured action ");
        action.print();
        if (GpioMode.isAuto(action.getGpio())) {
            autoAction[i] = true;
            Serial.print(" is enabled");
        }
        else {
            autoAction[i] = false;
            Serial.print(" is disabled as pin mode is not auto");
        }
    }

    for (JsonObject::iterator it=senses.begin(); it!=senses.end(); ++it)
    {
        const char* key = it->key;
        Sense sense;
        if (!strcmp(key, "time")) {
            sense = sense.withValue(it->value).withWrong(false); // time sense, if present, is never wrong
        }  
        else {
            sense = sense.fromJson(it->value);
        }
        for (i = 0; i < actionsSize; ++i) {
            if (!autoAction[i]) {
                continue;
            }
            Action action = actions[i];
            if (!strcmp(action.getSense(), key)) {
                bool time_action = !strcmp(key, "time");
                int value = sense.value; 
                Serial.printf("? found sense %s for the action with value [%d]\n", key, value);
                if (value == WRONG_VALUE) {
                    Serial.printf("!!! could not parse or wrong value\n", value);
                }
                if (sense.wrong) {
                    Serial.printf("? ignoring because value is marked as wrong\n", key);
                    Serial.println("? preserving GPIO state");
                    GpioState.set(action.getGpio(), digitalRead(action.getGpio()));
                    continue;
                }
                bool aboveThresholdGpioState = action.getAboveThresholdGpioState();
                if (time_action) {
                    if (value >= action.getThreshold() && value <= action.getThreshold() + action.getDelta()) {
                        Serial.printf("? time is within action period - GPIO should be %s\n", aboveThresholdGpioState == LOW ? "low" : "high");
                        ensureGpio(action.getGpio(), aboveThresholdGpioState);
                    }
                    else {
                        Serial.printf("? time is outside of action period - GPIO should be %s\n", aboveThresholdGpioState == LOW ? "high" : "low");
                        ensureGpio(action.getGpio(), !aboveThresholdGpioState);
                    }
                }
                else {
                    if (value <= action.getThreshold() - action.getDelta()) {
                        Serial.printf("? value is below threshold - GPIO should be %s\n", aboveThresholdGpioState == LOW ? "high" : "low");
                        ensureGpio(action.getGpio(), !aboveThresholdGpioState);
                    }
                    else if (value >= action.getThreshold() + action.getDelta()) {
                        Serial.printf("? value is above threshold - GPIO should be %s\n", aboveThresholdGpioState == LOW ? "low" : "high");
                        ensureGpio(action.getGpio(), aboveThresholdGpioState);
                    }
                    else {
                        Serial.println("? value is within delta - preserving GPIO state");
                        GpioState.set(action.getGpio(), digitalRead(action.getGpio()));
                    }
                }
            }
        }
    }

    Serial.println("[set hardcoded modes]");
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
    Serial.printf("? gpio %d to %d\n", gpio, state);
    pinMode(gpio, OUTPUT);
    digitalWrite(gpio, state);
  }
  GpioState.set(gpio, state);
}

void requestState() {
    char topic[30];
    constructTopicName(topic, "things/");
    strcat(topic, "/get");
    Serial.printf("? publish to %s\n", topic);
    bool success = mqttClient.publish(topic, "{}");
    if (!success) {
        Serial.println("!!! failed to publish");    
        mqttClient.disconnect();
    }
}

bool readTemperatureHumidity(const char* dhtType, DHT dht, JsonObject& jsonObject) {
  Serial.print("DHT ");
  
  float temp_c, humidity;
  
  humidity = dht.readHumidity();          // Read humidity (percent)
  temp_c = dht.readTemperature(false);     // Read temperature as Celsius
  if (isnan(humidity) || isnan(temp_c)) {
    Serial.print("fail ");
    return false;
  }
  else {
    Serial.println("OK");
    Serial.print("Temperature: ");
    Serial.println(temp_c);
    Serial.print("Humidity: ");
    Serial.println(humidity);
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
    Serial.printf("!!! could not parse JSON from config string:\n%s\n[save valid config]\n", string);
    saveConfig();
    return;
  }
  
  loadConfigFromJson(root, from_server);
}

void loadConfigFromJson(JsonObject& config, bool from_server) {
    if (from_server && config.containsKey("version")) {
        const char* version = config["version"];
        if (strcmp(version, VERSION) == 0) {
            Serial.println("? already the correct version. Ignoring update delta.");
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
    if (config.containsKey("lora")) {
        configChanged = true;
        if (!strcmp(config["lora"], "gateway")) {
            lora_gateway = true;
        }
        else {
            lora_gateway = false;
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

        action.print();
        Serial.println();
        if (!success) {
            Serial.println("!!! could not parse action");
            continue;
        }
        if (actionsSize + 1 >= MAX_ACTIONS_SIZE) {
            Serial.println("!!! too many actions already, ignoring the rest");
            break;
        }

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

  config["lora"] = lora_gateway ? "gateway" : "slave";
}

void saveConfig() {
  StaticJsonBuffer<CONFIG_MAX_SIZE+100> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();

  injectConfig(root);

  char configString[CONFIG_MAX_SIZE];
  root.printTo(configString, CONFIG_MAX_SIZE);
  Serial.printf("[save config]\n%s\n", configString);
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
    Serial.println("!!! resulting JSON is too long, expect errors");
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

