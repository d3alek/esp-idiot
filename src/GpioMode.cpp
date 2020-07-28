#include <Arduino.h>

#include "GpioMode.h"

void IdiotGpioMode::clear() {
  _size = 0;
}

void IdiotGpioMode::set(int gpio, const char* modeString) {
    if (gpio != PIN_A && gpio != PIN_B && gpio != PIN_C) {
        Serial.print("Cannot set pin ");
        Serial.print(gpio);
        Serial.print(" because it is netiher A, B or C. Ignoring call.");
        return;
    }
    int mode;

    if (strlen(modeString) != 1) {
        Serial.print("Ignoring GpioMode set as expected mode with length 1. Got: ");
        Serial.println(modeString);
        return;
    }

    if (modeString[0] == 'a') {
        mode = AUTO;
    }
    else {
        mode = atoi(modeString);
    }

    for (int i = 0; i < _size; ++i) {
        if (_gpio[i] == gpio) {
            _mode[i] = mode;
            return;
        }
    }

    if (_size == MAX_GPIO_MODES) {
        Serial.println("Max GPIO modes recorded. Ignoring call to set");
        return;
    }

    // GPIO set to auto which is also the default, so don't waste space by marking it
    if (mode == AUTO) {
        return;
    }

    _gpio[_size] = gpio;
    _mode[_size] = mode;

    _size++;
}

int IdiotGpioMode::getSize() {
  return _size;
}

int IdiotGpioMode::getGpio(int index) {
  if (index >= MAX_GPIO_MODES) {
    Serial.println("Requested index is more than MAX_GPIO_MODES. Returning 100.");
    return 100;
  }
  return _gpio[index];
}

int IdiotGpioMode::getMode(int index) {
  if (index >= MAX_GPIO_MODES) {
    Serial.println("Requested index is more than MAX_GPIO_MODES. Returning 100");
    return 100;
  }
  if (isAuto(getGpio(index))) {
    Serial.println("Requested index gpio is set to auto. Returning 101");
    return 101;
  }
  return _mode[index];
}

bool IdiotGpioMode::isAuto(int gpio) {
    for (int i = 0; i < _size; ++i) {
        if (_gpio[i] == gpio) {
            return _mode[i] == AUTO;
        }
    }

    return true; // defaults to auto
}

IdiotGpioMode GpioMode;

