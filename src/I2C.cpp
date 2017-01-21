#include "I2C.h"

void I2C::readI2C(IdiotLogger Logger, int i2cPin1, int i2cPin2, JsonObject& jsonObject) {
  Wire.pins(i2cPin1, i2cPin2);
  Wire.begin();

  scan(Logger);

  int value;
  for (int i = 0; i < devices_size; ++i) {
    int device = devices[i];

    char key[10] = "I2C-";
    sprintf(key, "I2C-%d", device);
  
    if (device == 32) { // Address of the capacitive soil moisture sensor
        I2CSoilMoistureSensor sensor; 
        sensor.begin();
        delay(3000);
        Logger.print("I2C Soil Moisture Sensor Software Firmware Version: ");
        Logger.println(sensor.getVersion(), HEX);
        Logger.print("Soil Moisture Capacitance: ");
        int capacitance = sensor.getCapacitance();
        Logger.print(capacitance);
        Logger.print(", Temperature: ");
        int temperature = sensor.getTemperature(); // divide by 10 to get real value. Not doing it here because arduino cannot sprintf floats.
        Logger.print(temperature);
        sprintf(valueString, "%d-%d", capacitance, temperature);
        jsonObject[String(key)] = String(valueString);
    }
    else {
        Wire.requestFrom(device, 1);
        int read_size = 0;
        while (Wire.available()) {
            value = Wire.read();
            read_size++;
        }
    
        Logger.print(read_size);
        Logger.println(" bytes read");
        Logger.print(key);
        Logger.print(" reads ");
        Logger.println(value);
        jsonObject[String(key)] = value;
    }    
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
      if (address < 8) {
        Logger.println("Must be an error - no I2C device should have an address below 8. Assuming floating I2C bus and returning no results.");
        devices_size = 0;
        return;     
      }
	    devices[devices_size++] = address;
      if (devices_size >= MAX_DEVICES) {
        Logger.println("I2C device limit reached");
        break;
      }
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
