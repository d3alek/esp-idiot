#include "IdiotLogger.h"

IdiotLogger::IdiotLogger() {
}

void IdiotLogger::setDebugOutput(bool debugOutput) {
  Serial.setDebugOutput(debugOutput);
}

void IdiotLogger::begin(long baud) {
  Serial.begin(baud);
  Serial.println();
  SPIFFS.begin();
  _logFile = SPIFFS.open("logFile.txt", "a+");
  if (!_logFile) {
    Serial.println("IdiotLogger: no valid log file specified. Logging only to Serial.");
  }
}

size_t IdiotLogger::write(uint8_t c) {
  Serial.write(c);
  if (_logFile) {
    _logFile.write(c);
  }
}

int IdiotLogger::available() {
  return Serial.available();
}


int IdiotLogger::read() {
  return Serial.read();
}

int IdiotLogger::peek() {
  return Serial.peek();
}

void IdiotLogger::flush() {
  Serial.flush();
  if (_logFile) {
    _logFile.flush();
  }
}

File IdiotLogger::getLogFile() {
  return _logFile;
}

bool IdiotLogger::clearFile() {
  _logFile.close();
  SPIFFS.remove(LOG_FILE);
  _logFile = SPIFFS.open(LOG_FILE, "a+");
  if (!_logFile) {
    Serial.println("IdiotLogger: no valid log file specified. Logging only to Serial.");
  }
}

