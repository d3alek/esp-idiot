#ifndef ACTION_H
#define ACTION_H

#ifndef _TEST_
#include "IdiotLogger.h"
#endif

class Action {
  public:
    Action();
    Action(const char*);
    bool fromConfig(const char* Action);
    void buildActionString(char*);
    bool getAboveThresholdGpioState();
    
    const char* getSense();
    int getThreshold();
    int getDelta();
    int getGpio();
    
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
