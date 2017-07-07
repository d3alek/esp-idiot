#ifndef I2C_H
#define I2C_H 

#include <Arduino.h>

#include <ArduinoJson.h>
#include "Wire.h"
#include "I2CSoilMoistureSensor.h"

#define MAX_DEVICES 10
#define MINIMUM_VERSION 2

class I2C {
  public:
    void readI2C(int, int, JsonObject&); 
  private:
    int devices[MAX_DEVICES];
    int devices_size;
    void scan();
    int get_expected_one_count(int, byte);
    int get_value(int, byte);
    int binary_subset(int, int, int);
};


extern I2C IdiotI2C;

#endif
