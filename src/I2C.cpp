#include "I2C.h"

void I2C::readI2C(IdiotLogger Logger, int i2cPin1, int i2cPin2, JsonObject& jsonObject) {
  Wire.pins(i2cPin1, i2cPin2);
  Wire.begin();
  Wire.setClockStretchLimit(1500); // in µs

  scan(Logger);

  int available, error, value;
  byte low_byte, high_byte, version_byte;
  for (int i = 0; i < devices_size; ++i) {
    int device = devices[i];

    char key[10] = "I2C-";
    sprintf(key, "I2C-%d", device);
  
    if (device == 32) { // Address of the capacitive soil moisture sensor
        I2CSoilMoistureSensor sensor; 
        sensor.begin(true); // true means wait 1 second
        Logger.print("I2C Soil Moisture Sensor Software Firmware Version: ");
        Logger.println(sensor.getVersion(), HEX);
        Logger.print("Soil Moisture Capacitance: ");
        int capacitance = sensor.getCapacitance();
        Logger.println(capacitance);
        Logger.print("Soil Moisture Temperature: ");
        float temperature = sensor.getTemperature() / (float) 10;
        Logger.println(temperature);
        jsonObject[String(key)+"c"] = capacitance;
        jsonObject[String(key)+"t"] = temperature;
    }
    else if (device == 60) {
        Logger.println("Ignoring I2C screen from senses");
    }
    else if (device == 8) {
        error = false;
        version_byte = low_byte = high_byte = 0;

        Wire.beginTransmission(device);
        Wire.write(0x1);
        Wire.endTransmission(false);

        delay(85);

        Wire.beginTransmission(device);
        Wire.requestFrom(device, 1);
        available = Wire.available();
        if (available != 1) {
            error = true;
            Logger.printf("[I2C-%d version] Expected 1 available but got %d instead\n", device, available);
        }
        else {
            version_byte = Wire.read();
        }

        error = Wire.endTransmission(false) || error;

        delay(85);

        Wire.beginTransmission(device);
        Wire.write(0x2);
        Wire.endTransmission(false);

        delay(85);

        Wire.beginTransmission(device);
        Wire.requestFrom(device, 1);
        available = Wire.available();
        if (available != 1) {
            error = true;
            Logger.printf("[I2C-%d low byte] Expected 1 available but got %d instead\n", device, available);
        }
        else {
            low_byte = Wire.read();
        }

        error = Wire.endTransmission(false) || error;
        
        delay(85);

        Wire.beginTransmission(device);
        Wire.write(0x3);
        Wire.endTransmission(false);

        delay(85);

        Wire.beginTransmission(device);
        Wire.requestFrom(device, 1);
        if (available != 1) {
            error = true;
            Logger.printf("[I2C-%d high byte] Expected 1 available but got %d instead\n", device, available);
        }
        else {
            high_byte = Wire.read();
        }

        error = Wire.endTransmission() || error;

        delay(85);

        value = word(high_byte, low_byte);

        if (version_byte != 1) {
            Logger.printf("Marking I2C read value as wrong because I2C device has wrong version. Expected 1 got %d\n", version_byte);
            jsonObject[String(key)] = String("w") + int((100*value) / 1024);
        }
        else if (error) {
            Logger.printf("Marking I2C read value as wrong because endTransmission returned error %d\n", error);
            jsonObject[String(key)] = String("w") + int((100*value) / 1024);
        }
        else {
            jsonObject[String(key)] = int((100*value) / 1024);
        }
       
    }
    else {
        error = false;
        version_byte = low_byte = high_byte = 0;
        Wire.beginTransmission(device);
        Wire.requestFrom(device, 1);
        available = Wire.available();
        if (available != 1) {
            error = true;
            Logger.printf("[I2C-%d low byte] Expected 1 available but got %d instead\n", device, available);
        }
        else {
            low_byte = Wire.read();
        }

        Wire.requestFrom(device, 1);
        if (available != 1) {
            error = true;
            Logger.printf("[I2C-%d high byte] Expected 1 available but got %d instead\n", device, available);
        }
        else {
            high_byte = Wire.read();
        }

        error = Wire.endTransmission() || error;

        value = word(high_byte, low_byte);

        if (error) {
            Logger.printf("Marking I2C read value as wrong because endTransmission returned error %d\n", error);
            jsonObject[String(key)] = String("w") + int((100*value) / 1024);
        }
        else {
            jsonObject[String(key)] = int((100*value) / 1024);
        }
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
