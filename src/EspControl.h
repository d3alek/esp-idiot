#ifndef ESP_CONTROL_H
#define ESP_CONTROL_H

#include "Arduino.h"

#define BUILTIN_LED 2 // on my ESP-12s the blue led is connected to GPIO2 not GPIO1

class IdiotEspControl {
    public:
      void restart();
      void blink();
      void turnLedOn();
      void turnLedOff();
      bool ledOn();
      void deepSleep(int);
      void toggleLed();
};

extern IdiotEspControl EspControl;

#endif
