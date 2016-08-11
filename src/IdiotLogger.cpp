#include "IdiotLogger.h"

IdiotLogger::IdiotLogger(bool logToFile) {
  _logToFile = logToFile;
}

void IdiotLogger::begin() {
  if (_logToFile) {
    _logFile.open(LOG_FILE, MAX_LOG_FILE_SIZE); // make sure SPIFFS.begin() is called before that.
    if (!_logFile.valid()) {
      Serial.println("IdiotLogger: no valid log file specified. Logging only to Serial.");
    }
  }
  else {
    Serial.println("File logging disabled. Logging only to Serial");
  }
}

size_t IdiotLogger::write(uint8_t c) {
  Serial.write(c);
  if (_logToFile && _logFile.valid()) {
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
  if (_logToFile && _logFile.valid()) {
    _logFile.flush();
  }
}

void IdiotLogger::close() {
  if (_logToFile && _logFile.valid()) {
    _logFile.close();  
  }
}

IdiotLogger::~IdiotLogger() {
  close();
}

