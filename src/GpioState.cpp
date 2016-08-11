#ifndef _TEST_
#include <Arduino.h>
#else
#include <mock_arduino.h>
#endif

#include "GpioState.h"

void IdiotGpioState::clear() {
  _size = 0;
}

void IdiotGpioState::set(int gpio, bool state) {
  for (int i = 0; i < _size; ++i) {
    if (_gpio[i] == gpio) {
      _state[i] = state;
      return;
    }
  }
  
  if (_size == MAX_GPIO_STATES) {
    Serial.println("Max GPIO states recorded. Ignoring call to set");
    return;
  }

  _gpio[_size] = gpio;
  _state[_size] = state;

  _size++;
}

int IdiotGpioState::getSize() {
  return _size;
}

int IdiotGpioState::getGpio(int index) {
  if (index >= MAX_GPIO_STATES) {
    Serial.println("Requested index is more than MAX_GPIO_STATES. Ignoring call to getGpio.");
    return -1;
  }
  return _gpio[index];
}

int IdiotGpioState::getState(int index) {
  if (index >= MAX_GPIO_STATES) {
    Serial.println("Requested index is more than MAX_GPIO_STATES. Ignoring call to getState.");
    return -1;
  }
  return _state[index];
}

IdiotGpioState GpioState;

