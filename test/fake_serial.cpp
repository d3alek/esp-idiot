#include <cstring>
#include <cstdio>
#include <iostream>
#include <iomanip>
#include <stdarg.h>

#include "fake_serial.h"

void FakeSerial::begin(unsigned long speed) {
  return;
}

void FakeSerial::end() {
  return;
}

size_t FakeSerial::write( const unsigned char buf[], size_t size ) {
  using namespace std;
  ios_base::fmtflags oldFlags = cout.flags();
  streamsize oldPrec = cout.precision();
  char oldFill = cout.fill();
  
  cout << "Serial::write: ";
  cout << internal << setfill('0');

  for( unsigned int i = 0; i < size; i++ ){
    cout << setw(2) << hex << (unsigned int)buf[i] << " ";
  }
  cout << endl;
  
  cout.flags(oldFlags);
  cout.precision(oldPrec);
  cout.fill(oldFill);

  return size;
}

void FakeSerial::print(int string) {
    std::cout << string;
}

void FakeSerial::println(int string) {
    std::cout << string << "\n";
}

void FakeSerial::println(unsigned char b, int type) {
    std::cout << std::hex << int(b) << "\n";
}

void FakeSerial::println() {
    std::cout << "\n";
}

void FakeSerial::print(unsigned char b, int type) {
    std::cout << std::hex << int(b);
}

void FakeSerial::print(const char* string) {
    std::cout << string;
}

void FakeSerial::println(const char* string) {
    std::cout << string << "\n";
}

int FakeSerial::printf(const char * format, ...) {
    va_list args;
    va_start(args, format);
    char result[100];
    vsprintf(result, format, args);
    std::cout << result;
    return 0;
}

void FakeSerial::print(String string) {
    std::cout << string;
}

void FakeSerial::println(String string) {
    std::cout << string << "\n";
}

FakeSerial Serial;
