#include "OneWireSensors.h"

// code from http://playground.arduino.cc/Learning/OneWire
void OneWireSensors::readOneWire(IdiotLogger Logger, int oneWirePin, JsonObject& jsonObject) {
  OneWire oneWire(oneWirePin);
  int devicesFound = 0;
  byte i;
  byte present = 0;
  byte data[12];
  byte addr[8];

  oneWire.reset_search();
  
  while (true) {
    if ( !oneWire.search(addr)) {
        Logger.print("No more addresses.\n");
        break;
    }
    devicesFound++;
  
    if ( OneWire::crc8( addr, 7) != addr[7]) {
        Logger.print("CRC is not valid!\n");
        return;
    }
  
    char deviceAddr[17];
    bytesToHex(deviceAddr, addr);
    char device[25];
    strcpy(device, "OW-");
    strcat(device, deviceAddr);
    Logger.print("addr: ");
    Logger.print(device);
    if ( addr[0] == 0x10) {
        Logger.println(" DS18S20 family device");
    }
    else {
      if ( addr[0] == 0x28) {
        Logger.println(" DS18B20 family device. Not supported yet.");
        jsonObject[String(device)] = "not supported";
      }
      else {
        Logger.print(" Device family not recognized: 0x");
        Logger.println(addr[0],HEX);
        jsonObject[String(device)] = "not supported";
      }
      continue;
    }
  
    oneWire.reset();
    oneWire.select(addr);
    oneWire.write(0x44);         // start conversion, without parasite power
  
    delay(1000);     // maybe 750ms is enough, maybe not
    // we might do a ds.depower() here, but the reset will take care of it.
  
    present = oneWire.reset();
    oneWire.select(addr);    
    oneWire.write(0xBE);         // Read Scratchpad
  
    Logger.print("P=");
    Logger.print(present,HEX);
    Logger.print(" ");
    for ( i = 0; i < 9; i++) {           // we need 9 bytes
      data[i] = oneWire.read();
      Logger.print(data[i], HEX);
      Logger.print(" ");
    }
    Logger.print(" CRC=");
    Logger.print( OneWire::crc8( data, 8), HEX);
    Logger.println();
  
    int HighByte, LowByte, TReading, SignBit, Tc_100, Whole, Fract;
    LowByte = data[0];
    HighByte = data[1];
    TReading = (HighByte << 8) + LowByte;
    SignBit = TReading & 0x8000;  // test most sig bit
    if (SignBit) // negative
    {
      TReading = (TReading ^ 0xffff) + 1; // 2's comp
    }
    Tc_100 = (TReading*100/2); // for S family
    // Tc_100 = (6 * TReading) + TReading / 4;    // multiply by (100 * 0.0625) or 6.25 (for B family)
  
    Whole = Tc_100 / 100;  // separate off the whole and fractional portions
    Fract = Tc_100 % 100;
  
    char buf[20];
    sprintf(buf, "%c%d.%d",SignBit ? '-' : ' ', Whole, Fract < 10 ? 0 : Fract);
    jsonObject[String(device)] = String(buf);
    Logger.println(buf);
  }
  Logger.print("OneWire devices found: ");
  Logger.println(devicesFound);
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
