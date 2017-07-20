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
        int expectation;
        int ssd;
        bool wrong;
        String toString();
        Sense withValue(float);
        Sense withExpectationSSD(int, int);
        Sense withWrong(bool);
    private:
        void parse(const char*);
};
#endif

