#ifndef ACTION_H
#define ACTION_H

class Action {
  public:
    Action();
    Action(const char*);
    static void buildThresholdDeltaString(char*, float, float);
    
    void parseThresholdDeltaString(const char*);
    void addGpio(int);
    
    const char* getSense();
    float getThreshold();
    float getDelta();
    int getGpiosSize();
    int getGpio(int);
  private:
    char _sense[15];
    float _delta;
    float _threshold;
    int _gpios[10];
    int _gpiosSize;

    void _init();
};

#endif
