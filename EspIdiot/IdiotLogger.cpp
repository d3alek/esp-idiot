#include "IdiotLogger.h"

IdiotLogger::IdiotLogger() {
}

void IdiotLogger::setDebugOutput(bool debugOutput) {
  Serial.setDebugOutput(debugOutput);
}

void IdiotLogger::begin(long baud) {
  Serial.begin(baud);
  Serial.println();
  _logFile.open(LOG_FILE, MAX_LOG_FILE_SIZE); // make sure SPIFFS.begin() is called before that.
  if (!_logFile.valid()) {
    Serial.println("IdiotLogger: no valid log file specified. Logging only to Serial.");
  }
}

size_t IdiotLogger::write(uint8_t c) {
  Serial.write(c);
  if (_logFile.valid()) {
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
  if (_logFile.valid()) {
    _logFile.flush();
  }
}

void IdiotLogger::close() {
  if (_logFile.valid()) {
    _logFile.close();  
  }
}

IdiotLogger::~IdiotLogger() {
  close();
}

