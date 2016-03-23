#pragma once

#include <iostream>
#include <fake_String.h>

#define HEX 1

class FakeSerial {
    public:
        void begin(unsigned long);
        void end();
        size_t write(const unsigned char*, size_t);
        void print(const char*);
        void println(const char*);
        void println(unsigned char, int);
        void print(unsigned char, int);
        void print(const int);
        void println(const int);
        void print(String);
        void println(String);
        void println();
        int printf ( const char * format, ... );
};

class IdiotLogger : public FakeSerial {

};



extern FakeSerial Serial;
