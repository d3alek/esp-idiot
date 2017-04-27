#ifndef GPIO_STATE_H
#define GPIO_STATE_H

#define MAX_GPIO_STATES 6

class IdiotGpioState {
  public:
    void set(int, bool);
    int getSize();
    int getGpio(int);
    int getState(int);
    void clear();
  private:
    int _gpio[MAX_GPIO_STATES];
    bool _state[MAX_GPIO_STATES];
    int _size = 0;
};

extern IdiotGpioState GpioState;

#endif
