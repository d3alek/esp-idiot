#ifndef ACTION_H
#define ACTION_H

class Action {
  public:
    Action();
    Action(const char*);
    static void buildThresholdDeltaString(char*, int, int);
    static void fromConfig(const char*, const char*, Action*);
    void parseThresholdDeltaString(const char*);
    void buildSenseAndGpioString(char*);
    void setGpio(int);
    
    const char* getSense();
    int getThreshold();
    int getDelta();
    int getGpio();
    
    void parseSenseAndGpio(const char*);
  private:
    char _sense[30];
    int _delta;
    int _threshold;
    int _gpio;

    void _init();
};

#endif
