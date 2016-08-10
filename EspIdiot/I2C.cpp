#include "I2C.h"

void I2C::readI2C(IdiotLogger Logger, int i2cPin1, int i2cPin2, JsonObject& jsonObject) {
  Wire.pins(i2cPin1, i2cPin2);
  Wire.begin();

  scan(Logger);

  int value;
  for (int i = 0; i < devices_size; ++i) {
    int device = devices[i];
    Wire.requestFrom(device, 1);
    int read_size = 0;
    while (Wire.available()) {
        value = Wire.read();
        read_size++;
    }
 
    Logger.print(read_size);
    Logger.println(" bytes read");
    char key[10] = "I2C-";
    sprintf(key, "I2C-%d", device);

    Logger.print(key);
    Logger.print(" reads ");
    Logger.println(value);

    jsonObject[String(key)] = value;
  }
}

void I2C::scan(IdiotLogger Logger) {
  int error, address;

  Logger.println("Scanning I2C...");

  devices_size = 0;
  for(address = 1; address < 127; address++ ) 
  {
    // The i2c_scanner uses the return value of
    // the Write.endTransmisstion to see if
    // a device did acknowledge to the address.
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0)
    {
      Logger.print("I2C device found at ");
      Logger.println(address);

	  devices[devices_size++] = address;
    }
    else if (error==4) 
    {
      Logger.print("Unknow error at address ");
      Logger.println(address);
    }    
  }

  Logger.print(devices_size);
  Logger.println(" I2C devices found");
}

I2C IdiotI2C;
