#ifndef _TEST_
#include <Arduino.h>
#else
#include <mock_arduino.h>
#endif

#include "Action.h"

void Action::buildThresholdDeltaString(char* thresholdDeltaString, int threshold, int delta) {
  sprintf(thresholdDeltaString, "%d~%d", threshold, delta);
}

void Action::parseThresholdDeltaString(const char* thresholdDeltaString) {
  if (thresholdDeltaString == NULL) {
    return;
  }
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

void Action::setGpio(int gpio) {
  _gpio = gpio;
}

void Action::setAboveThresholdGpioState(bool state) {
    _aboveThresholdGpioState = state;
}

const char* Action::getSense() {
  return _sense;
}

bool Action::getAboveThresholdGpioState() {
    return _aboveThresholdGpioState;
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
  _delta = -1;
  _threshold = -1;
}

int Action::getThreshold() {
  return _threshold;
}

int Action::getDelta() {
  return _delta;
}

int Action::getGpio() {
  return _gpio;
}

bool Action::fromConfig(const char* senseAndGpioString, const char* thresholdDeltaString, Action* action) {
  if (strlen(senseAndGpioString) < 3) {
      return false;
  }
  action->parseSenseAndGpio(senseAndGpioString);
  action->parseThresholdDeltaString(thresholdDeltaString);
  if (action->getDelta() == -1) {
    return false;
  }
  return true;
}

void Action::parseSenseAndGpio(const char* senseAndGpioString) {
    int stringLength = strlen(senseAndGpioString);
    // string begins with A| which stands for Action  - skip the first 2 characters then
    int toSkip = 2;
    const char* gpioBegin = strchr(senseAndGpioString+toSkip, (int)'|') + 1;
    int senseLength = gpioBegin-senseAndGpioString-toSkip-1;
    strncpy(_sense, senseAndGpioString+toSkip, senseLength);
    _sense[senseLength]='\0';
    _gpio = atoi(gpioBegin);

    if (senseAndGpioString[stringLength-1] == 'L') {
        _aboveThresholdGpioState = 0;
    }
    else if (senseAndGpioString[stringLength-1] == 'H') {
        _aboveThresholdGpioState = 1;
    }
    else {
        _aboveThresholdGpioState = 1;
    }
}

void Action::buildSenseAndGpioString(char* senseAndGpioString) {
    strcpy(senseAndGpioString, "A|");
    strcat(senseAndGpioString, _sense);
    strcat(senseAndGpioString, "|");
    char buf[4];
    sprintf(buf, "%d", _gpio);
    strcat(senseAndGpioString, buf);
    strcat(senseAndGpioString, _aboveThresholdGpioState ? "H":"L");
}

bool Action::looksLikeAction(const char* string) {
  return string[0] == 'A' && string[1] == '|';
}

void Action::printTo(IdiotLogger logger) {
  logger.printf("%s[t:%d,d:%d,g:%d-%d]\n",_sense,_threshold,_delta, _gpio, _aboveThresholdGpioState);
}

