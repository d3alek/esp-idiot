#include "OneWireSensors.h"

void OneWireSensors::readOneWire(int oneWirePin, JsonObject jsonObject) {
    Serial.println("[one wire]");
    OneWire oneWire(oneWirePin);
    int devicesFound = 0;
    byte addr[8];

    oneWire.reset_search();

    while (true) {
        if ( !oneWire.search(addr)) {
            break;
        }
        devicesFound++;

        if ( OneWire::crc8( addr, 7) != addr[7]) {
            Serial.print("? CRC is not valid!\n");
            return;
        }

        char deviceAddr[17];
        bytesToHex(deviceAddr, addr);
        char device[25];
        strcpy(device, "OW-");
        strcat(device, deviceAddr);
        if ( addr[0] == 0x10) {
            Serial.printf("? %s type DS18S20\n", device);
            readDS18x20(oneWire, addr, device, jsonObject);
        }
        else if ( addr[0] == 0x28) {
            Serial.printf("? %s type DS18B20\n", device);
            readDS18x20(oneWire, addr, device, jsonObject);
        }
        else {
            Serial.printf("!!! type not recognized from byte 0x%02X\n", addr[0]);
            continue;
        }
    }
    Serial.printf("? %d OneWire devices found\n", devicesFound);
}

// code from 
// https://github.com/PaulStoffregen/OneWire/blob/master/examples/DS18x20_Temperature/DS18x20_Temperature.pde
void OneWireSensors::readDS18x20(OneWire& oneWire, byte* addr, char* device, JsonObject jsonObject) {

    byte type_s;
    if (addr[0] == 0x10) {
        type_s = 1;
    }
    else if (addr[0] == 0x28) {
        type_s = 0;
    }
    else {
        Serial.println("Sensor addr not recongized as DS18x20.");
        return;
    }

    byte i;
    byte data[12];

    oneWire.reset();
    oneWire.select(addr);
    oneWire.write(0x44);         // start conversion, without parasite power

    delay(1000);     // maybe 750ms is enough, maybe not
    // we might do a ds.depower() here, but the reset will take care of it.

    oneWire.reset();
    oneWire.select(addr);    
    oneWire.write(0xBE);         // Read Scratchpad

    for ( i = 0; i < 9; i++) {           // we need 9 bytes
        data[i] = oneWire.read();
    }

    // Convert the data to actual temperature
    // because the result is a 16 bit signed integer, it should
    // be stored to an "int16_t" type, which is always 16 bits
    // even when compiled on a 32 bit processor.
    int16_t raw = (data[1] << 8) | data[0];
    if (type_s) {
        raw = raw << 3; // 9 bit resolution default
        if (data[7] == 0x10) {
            // "count remain" gives full 12 bit resolution
            raw = (raw & 0xFFF0) + 12 - data[6];
        }
    } else {
        byte cfg = (data[4] & 0x60);
        // at lower res, the low bits are undefined, so let's zero them
        if (cfg == 0x00) raw = raw & ~7;  // 9 bit resolution, 93.75 ms
        else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
        else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
        //// default is 12 bit resolution, 750 ms conversion time
    }
    float celsius = (float)raw / 16.0;

    jsonObject[String(device)] = celsius;
    Serial.printf("%s=%d\n", device, int(celsius));
}

// buffer must be at least 17 long, bytes are assumed to be 8
// http://stackoverflow.com/a/10599161/5799810
void OneWireSensors::bytesToHex(char* buffer, const byte* bytes) {
  for(int i = 0; i < 8; i++) {
      sprintf(buffer+2*i, "%02X", bytes[i]);
  }
  buffer[16]='\0';
}

OneWireSensors IdiotOneWire;
