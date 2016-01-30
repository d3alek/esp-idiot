#include "SizeLimitedFileAppender.h"

SizeLimitedFileAppender::SizeLimitedFileAppender() {
  
}

bool SizeLimitedFileAppender::open(const char* name, int sizeLimitInBytes) {
  if (SPIFFS.exists(name)) {
    _file = SPIFFS.open(name, "r+");
  }
  else {
    _file = SPIFFS.open(name, "w+");
  }
  if (!_file) {
    return _deleteFile();
  }
  _fileName = name;
  _sizeLimitInBytes = sizeLimitInBytes;
  _fileSize = _file.size();
  Serial.print("SizeLimitedFileAppender[");
  Serial.print(name);
  Serial.print("] with current size: ");
  Serial.println(_fileSize);
  _file.seek(0, SeekEnd);
  return _file;
}

size_t SizeLimitedFileAppender::write(uint8_t c) {
  if (_fileSize + 1 > _sizeLimitInBytes) {
    Serial.println("Limited size reached. Keeping file's latest half.");
    _file.seek(_fileSize / 2, SeekSet);
    File temp = SPIFFS.open(TEMP_FILE, "w+");
    if (!temp) {
      Serial.println("Error opening temp file.");
      _deleteFile();
      return write(c);
    }
    char buffer[BUFFER_SIZE];
    Serial.print("To copy: ");
    Serial.println(_fileSize/2);
    _fileSize = 0;
    while (_file.readBytes(buffer, BUFFER_SIZE)) {
      _fileSize += temp.print(buffer);
      Serial.print(_fileSize);
      Serial.print(" ");
      yield();
    }
    _file.close();
    SPIFFS.remove(_fileName);
    SPIFFS.rename(TEMP_FILE, _fileName);
    _file = temp;
  }
  size_t res = _file.write(c);
  _fileSize += res;
  return res;
}

bool SizeLimitedFileAppender::_deleteFile() {
  Serial.print("Clear current file attempt: ");
  Serial.println(++deleteFileAttempts);
  if (deleteFileAttempts > MAX_DELETE_FILE_ATTEMPTS) {
    Serial.println("Formatting file system");
    SPIFFS.format();
    return false;
  }

  SPIFFS.remove(_fileName);
  
  return open(_fileName, _sizeLimitInBytes);
}

int SizeLimitedFileAppender::available() {
  return _file.available();
}

int SizeLimitedFileAppender::read() {
  return _file.read();
}

int SizeLimitedFileAppender::peek() {
  return _file.peek();
}

void SizeLimitedFileAppender::flush() {
  _file.flush();
}

void SizeLimitedFileAppender::close() {
  _file.close();
}

bool SizeLimitedFileAppender::valid() {
  return _file;
}

int SizeLimitedFileAppender::size() {
  return _file.size();
}

SizeLimitedFileAppender::~SizeLimitedFileAppender() {
  close();
}

