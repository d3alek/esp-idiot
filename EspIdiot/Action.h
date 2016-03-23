#ifndef ACTION_H
#define ACTION_H

#ifndef _TEST_
#include "IdiotLogger.h"
#endif

class Action {
  public:
    Action();
    Action(const char*);
    static bool looksLikeAction(const char*);
    static void buildThresholdDeltaString(char*, int, int);
    static bool fromConfig(const char*, const char*, Action*);
    void parseThresholdDeltaString(const char*);
    void buildSenseAndGpioString(char*);
    void setGpio(int);
    void setAboveThresholdGpioState(bool);
    bool getAboveThresholdGpioState();
    
    const char* getSense();
    int getThreshold();
    int getDelta();
    int getGpio();
    
    void parseSenseAndGpio(const char*);
    void printTo(IdiotLogger);
  private:
    char _sense[30];
    int _delta;
    int _threshold;
    int _gpio;
    bool _aboveThresholdGpioState;

    void _init();
};

#endif
