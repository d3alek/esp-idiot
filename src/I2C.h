#ifndef I2C_H
#define I2C_H 

#include <Arduino.h>

#include <ArduinoJson.h>
#include "Wire.h"
#include "IdiotLogger.h"

#define MAX_DEVICES 10

class I2C {
  public:
    void readI2C(IdiotLogger, int, int, JsonObject&); 
  private:
    int devices[MAX_DEVICES];
    int devices_size;
    void scan(IdiotLogger);
};


extern I2C IdiotI2C;

#endif
