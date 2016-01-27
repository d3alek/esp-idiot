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
};


extern OneWireSensors IdiotOneWire;

#endif
