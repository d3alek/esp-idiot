#ifndef GPIO_MODE_H
#define GPIO_MODE_H

#define MAX_GPIO_MODES 5
#define AUTO 2
#define PIN_A 5
#define PIN_B 4
#define PIN_C 13

class IdiotGpioMode {
  public:
    void set(int, const char*);
    int getSize();
    int getGpio(int);
    int getMode(int);
    bool isAuto(int);
    void clear();
  private:
    int _gpio[MAX_GPIO_MODES];
    int _mode[MAX_GPIO_MODES];
    int _size = 0;
};

extern IdiotGpioMode GpioMode;

#endif
