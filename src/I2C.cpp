#include "I2C.h"

#define I2C_WAIT_DELAY 85

int count_ones(int value) {
    int count = 0;
    int mask = 1 << 10;
    while (mask > 0) {
        count += (value & mask) > 0;
        mask >>= 1;
    }

    return count;
}

void I2C::readI2C(int i2cPin1, int i2cPin2, JsonObject& jsonObject) {
  
  scan();

  int available, error, value, expected_one_count, actual_one_count;
  byte low_byte, high_byte, version_byte;
  for (int i = 0; i < devices_size; ++i) {
    int device = devices[i];

    char key[10] = "I2C-";
    sprintf(key, "I2C-%d", device);
  
    if (device == 32) { // Address of the capacitive soil moisture sensor
        I2CSoilMoistureSensor sensor; 
        sensor.begin(true); // true means wait 1 second
        Serial.print("? I2C Soil Moisture Sensor version: ");
        Serial.println(sensor.getVersion(), HEX);
        int capacitance = sensor.getCapacitance();
        Serial.printf("? capacitance: %d\n", capacitance);
        Serial.print("? temperature: ");
        float temperature = sensor.getTemperature() / (float) 10;
        Serial.println(temperature);
        String key_string = String(key)+"c";
        jsonObject[key_string] = capacitance;
        key_string = String(key) + "t";
        jsonObject[key_string] = temperature;
    }
    else if (device == 60) {
        Serial.println("? ignoring I2C screen");
    }
    else {
        error = false;
        version_byte = low_byte = high_byte = 0;

        Wire.beginTransmission(device);
        Wire.write(0x1);
        Wire.endTransmission(false);

        delay(I2C_WAIT_DELAY);

        Wire.beginTransmission(device);
        Wire.requestFrom(device, 1);
        available = Wire.available();
        if (available != 1) {
            error = true;
            Serial.printf("[I2C-%d version] Expected 1 available but got %d instead\n", device, available);
        }
        else {
            version_byte = Wire.read();
        }

        error = Wire.endTransmission(false) || error;

        delay(I2C_WAIT_DELAY);

        Wire.beginTransmission(device);
        Wire.write(0x2);
        Wire.endTransmission(false);

        delay(I2C_WAIT_DELAY);

        Wire.beginTransmission(device);
        Wire.requestFrom(device, 1);
        available = Wire.available();
        if (available != 1) {
            error = true;
            Serial.printf("[I2C-%d low byte] Expected 1 available but got %d instead\n", device, available);
        }
        else {
            low_byte = Wire.read();
        }

        error = Wire.endTransmission(false) || error;
        
        delay(I2C_WAIT_DELAY);

        Wire.beginTransmission(device);
        Wire.write(0x3);
        Wire.endTransmission(false);

        delay(I2C_WAIT_DELAY);

        Wire.beginTransmission(device);
        Wire.requestFrom(device, 1);
        if (available != 1) {
            error = true;
            Serial.printf("[I2C-%d high byte] Expected 1 available but got %d instead\n", device, available);
        }
        else {
            high_byte = Wire.read();
        }

        error = Wire.endTransmission() || error;

        delay(I2C_WAIT_DELAY);

        value = word(high_byte, low_byte);

        expected_one_count = get_expected_one_count(value, version_byte);

        value = get_value(value, version_byte); 

        actual_one_count = count_ones(value);
        
        String key_string = String(key);
        if (version_byte < MINIMUM_VERSION) {
            Serial.printf("? marking I2C-%d as wrong - old version: %d < %d\n", device, version_byte, MINIMUM_VERSION);
            jsonObject[key_string] = String("w") + value;
        }
        else if (error) {
            Serial.printf("? marking I2C-%d as wrong - endTransmission error %d\n", device, error);
            jsonObject[key_string] = String("w") + value;
        }
        else {
            jsonObject[key_string] = value;
            Serial.printf("? I2C-%d = %d\n", device, value);
        }
    }
  }
}

/*
 * In newer analog-to-I2C versions, moving the bit-count lower down the significant bits because the most significant bits get corrupted most often
 */
int I2C::get_expected_one_count(int value, byte version) {
    switch (version) {
        case 2:
            return binary_subset(value, 12, 15);
        case 3:
            return binary_subset(value, 11, 15);
    }
}

int I2C::get_value(int value, byte version) {
    switch (version) {
        case 2:
            return binary_subset(value, 0, 11);
        case 3:
            return binary_subset(value, 0, 10);
    }
}

int I2C::binary_subset(int value, int from_lower, int to_lower) {
    return (value >> from_lower) & ((1 << (to_lower-from_lower + 1)) - 1);
}


void I2C::scan() {
    int error, address;

    Serial.println("[scan I2C]");

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
            Serial.printf("+I2C-%d ", address);
            if (address < 8) {
                Serial.println("!!! no I2C device should be < 8 - assuming floating I2C bus");
                devices_size = 0;
                return;
            }

            devices[devices_size++] = address;
            if (devices_size >= MAX_DEVICES) {
                Serial.println("!!! I2C device limit reached");
                break;
            }
        }
        else if (error==4) 
        {
            Serial.printf("!!! unknow error at address %d\n", address);
        }
    }

    Serial.printf("\n? %d I2C devices found\n", devices_size);
}

I2C IdiotI2C;
