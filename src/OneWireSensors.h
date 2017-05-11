#ifndef ONEWIRESENSORS_H
#define ONEWIRESENSORS_H

#include <Arduino.h>

#include <ArduinoJson.h>
#include "OneWire.h"

#include "IdiotLogger.h"

class OneWireSensors {
  public:
    void readOneWire(IdiotLogger, int, JsonObject&); 
  private:
    void bytesToHex(char*, const byte*);
    void readDS18x20(IdiotLogger, OneWire&, byte[], char*, JsonObject&); 
};


extern OneWireSensors IdiotOneWire;

#endif
