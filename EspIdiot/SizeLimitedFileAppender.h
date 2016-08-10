#ifndef SizeLimitedFileAppender_H
#define SizeLimitedFileAppender_H

#define BUFFER_SIZE 300
#define TEMP_FILE "SizeLimitedFileAppender.temp"
#define MAX_DELETE_FILE_ATTEMPTS 3

#include "Arduino.h"
#include "FS.h"

// following the example of https://code.google.com/p/arduino/source/browse/trunk/hardware/arduino/cores/arduino/HardwareSerial.h?r=982
class SizeLimitedFileAppender : public Stream {
  public:
    SizeLimitedFileAppender();
    ~SizeLimitedFileAppender();
    bool open(const char*, int);
    void close();
    bool valid();
    int size();
    virtual size_t write(uint8_t);
    virtual int available();
    virtual int read();
    virtual int peek();
    virtual void flush();
    using Print::write;
  private:
    File _file;
    const char* _fileName;
    int _sizeLimitInBytes;
    int _fileSize;
    bool _deleteFile();
    int deleteFileAttempts = 0;
};

#endif

