#ifndef GPIO_MODE_H
#define GPIO_MODE_H

#define MAX_GPIO_MODES 6
#define AUTO 2

#define PIN_A 4
#define PIN_B 5
#define PIN_C 12
#define PIN_D 13
#define PIN_E 14
#define PIN_F 16

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
