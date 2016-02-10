#include "EspPersistentStore.h"
#include <Arduino.h>
#include <EEPROM.h>

bool began = false;

void EspPersistentStore::begin() {
  EEPROM.begin(512);
  began = true;
}

void EspPersistentStore::clear() {
  if (!began) {
    Serial.println("Begin should be called first");
  }
  EEPROM.write(WIFI_NAME_STORED_BIT, 0);
  EEPROM.write(WIFI_PASSWORD_STORED_BIT, 0);
  EEPROM.commit();
}

void EspPersistentStore::_putString(int startingOffset, const char* string) {
  if (!began) {
    Serial.println("Begin should be called first");
  }
  int i;
  for (i = 0; string[i] != '\0'; ++i) {
    EEPROM.write(startingOffset + i, string[i]);
  }
  EEPROM.write(startingOffset + i, '\0');
  EEPROM.commit();
}

void EspPersistentStore::_readString(int startingOffset, char* string, int maxSize) {
  if (!began) {
    Serial.println("Begin should be called first");
  }
  int i = 0;
  do {
    string[i] = EEPROM.read(startingOffset + i);
  } while (string[i++] != '\0' && i < maxSize);
}

void EspPersistentStore::putWifiPassword(const char* password) {
  _putString(WIFI_PASSWORD_OFFSET, password);
  EEPROM.write(WIFI_PASSWORD_STORED_BIT, 1);
  EEPROM.commit();
}

void EspPersistentStore::putWifiName(const char* name) {
  _putString(WIFI_NAME_OFFSET, name);
  EEPROM.write(WIFI_NAME_STORED_BIT, 1);
  EEPROM.commit();
}

void EspPersistentStore::putConfig(const char* config) {
  _putString(CONFIG_OFFSET, config);
  EEPROM.commit();
}

void EspPersistentStore::readConfig(char* config) {
  _readString(CONFIG_OFFSET, config, CONFIG_MAX_SIZE);
}

bool wifiNameStored() {
  if (!began) {
    Serial.println("Begin should be called first");
  }
  return EEPROM.read(WIFI_NAME_STORED_BIT);
}

bool wifiPasswordStored() {
  if (!began) {
    Serial.println("Begin should be called first");
  }
  return EEPROM.read(WIFI_PASSWORD_STORED_BIT);
}

void EspPersistentStore::readWifiName(char* wifiName) {
  if (!wifiNameStored()) {
    Serial.println("Could not read wifi name because none stored");
    wifiName = NULL;
  }

  _readString(WIFI_NAME_OFFSET, wifiName, WIFI_NAME_MAX_SIZE);
}

void EspPersistentStore::readWifiPassword(char* wifiPassword) {
  if (!wifiPasswordStored()) {
    Serial.println("Could not read wifi password because none stored");
    wifiPassword = NULL;
  }

  _readString(WIFI_PASSWORD_OFFSET, wifiPassword, WIFI_PASS_MAX_SIZE);
}

bool EspPersistentStore::wifiCredentialsStored() {
  return wifiNameStored() && wifiPasswordStored();
}

EspPersistentStore PersistentStore;
