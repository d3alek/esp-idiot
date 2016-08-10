#ifndef IdiotLogger_h
#define IdiotLogger_h

#define LOG_FILE "logFile.txt"
#define MAX_LOG_FILE_SIZE 150000 // 150kb
#include "Arduino.h"
#include <FS.h>
#include "SizeLimitedFileAppender.h"

// following the example of https://code.google.com/p/arduino/source/browse/trunk/hardware/arduino/cores/arduino/HardwareSerial.h?r=982
class IdiotLogger : public Stream {
  public:
    IdiotLogger(bool logToFile);
    ~IdiotLogger();
    void begin();
    void close();
    virtual size_t write(uint8_t);
    virtual int available();
    virtual int read();
    virtual int peek();
    virtual void flush();
    using Print::write;
  private:
    SizeLimitedFileAppender _logFile;
    bool _logToFile;
};

#endif

