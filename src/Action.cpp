#ifndef _TEST_
#include <Arduino.h>
#else
#include <mock_arduino.h>
#endif

#include "Action.h"

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

bool Action::fromConfig(const char* action_string) {
    const char* gpio_begin = strchr(action_string, (int)'|');
    if (gpio_begin == NULL) {
        return false;
    }
    else {
        gpio_begin += 1;
    }

    int sense_length = gpio_begin-action_string-1;
    strncpy(_sense, action_string, sense_length);
    _sense[sense_length]='\0';
    _gpio = atoi(gpio_begin);

    const char* write_begin = strchr(gpio_begin, (int)'|');
    if (write_begin == NULL) {
        return false;
    }
    else {
        write_begin += 1;
    }

    if (write_begin[0] == 'L') {
        _aboveThresholdGpioState = 0;
    }
    else {
        _aboveThresholdGpioState = 1;
    }

    const char* threshold_begin = strchr(write_begin, (int)'|');

    if (threshold_begin == NULL) {
        return false;
    }
    else {
        threshold_begin += 1;
    }

    _threshold = atoi(threshold_begin);

    const char* delta_begin = strchr(threshold_begin, (int)'|');
    if (delta_begin == NULL) {
        return false;
    }
    else {
        delta_begin += 1;
    }

    _delta = atoi(delta_begin);

    return true;
}

void Action::buildActionString(char* action_string) {
    sprintf(action_string, "%s|%d|%s|%d|%d", _sense, _gpio, _aboveThresholdGpioState ? "H":"L", _threshold, _delta);
}

void Action::print() {
  Serial.printf("%s[t:%d,d:%d,g:%d-%d]",_sense,_threshold,_delta, _gpio, _aboveThresholdGpioState);
}

