#ifndef _TEST_
#include <Arduino.h>
#else
#include <mock_arduino.h>
#endif

#include "Action.h"

void Action::buildThresholdDeltaString(char* thresholdDeltaString, float threshold, float delta) {
  sprintf(thresholdDeltaString, "%.1f~%.1f", threshold, delta);
}

void Action::parseThresholdDeltaString(const char* thresholdDeltaString) {
  int bufferSize = 10;
  char buffer[bufferSize];
  int i;
  int stringLength = strlen(thresholdDeltaString);
  for (i = 0; i < stringLength && i < bufferSize - 1; ++i) {
    if (thresholdDeltaString[i] != '~') {
      buffer[i] = thresholdDeltaString[i];
    }
    else {
      break;
    }
  }
  buffer[i] = '\0';

  _threshold = atoi(buffer);
  int j;
  ++i; // skip the ~
  for (j = 0; i < stringLength && j < bufferSize - 1; ++j, i++) {
    buffer[j] = thresholdDeltaString[i];
  }
  buffer[j] = '\0';
  
  _delta = atoi(buffer);
}

void Action::addGpio(int gpio) {
  _gpios[_gpiosSize++] = gpio;
}

const char* Action::getSense() {
  return _sense;
}

Action::Action(const char* sense) {
  _init();
  strcpy(_sense, sense);
}

Action::Action() {
  _init();
}

void Action::_init() {
  strcpy(_sense,"");
  _delta = 0;
  _threshold = 0;
  _gpiosSize = 0;
}

float Action::getThreshold() {
  return _threshold;
}

float Action::getDelta() {
  return _delta;
}

int Action::getGpiosSize() {
  return _gpiosSize;
}

int Action::getGpio(int index) {
  return _gpios[index];
}


