#ifndef SENSE_H 
#define SENSE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#define WRONG_VALUE -1003

class Sense {
    public:
        Sense();
        Sense fromString(const char*);
        Sense fromJson(JsonVariant);
        float value;
        float expectation;
        int ssd;
        bool wrong;
        String toString();
        Sense withValue(float);
        Sense withExpectationSSD(float, int);
        Sense withWrong(bool);
    private:
        void parse(const char*);
};
#endif

