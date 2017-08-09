#include "EspControl.h"

void IdiotEspControl::deepSleep(int seconds) {
    Serial.printf("Going to sleep for %d seconds\n", seconds);
    ESP.deepSleep(seconds*1000000, WAKE_RF_DEFAULT);
}

void IdiotEspControl::restart() {
  ESP.restart();
}

void IdiotEspControl::blink() {
  if (ledOn()) {
    turnLedOff();
    delay(500);
    turnLedOn();
  }
  else {
    turnLedOn();
    delay(500);
    turnLedOff();    
  }
}

bool IdiotEspControl::ledOn() {
  return digitalRead(BUILTIN_LED) == LOW;
}

void IdiotEspControl::turnLedOn() {
  digitalWrite(BUILTIN_LED, LOW);
}

void IdiotEspControl::turnLedOff() {
  digitalWrite(BUILTIN_LED, HIGH);
}

void IdiotEspControl::toggleLed() {
  if (ledOn()) {
    turnLedOff();
  }
  else {
    turnLedOn();
  }
}

IdiotEspControl EspControl;
