#include <TinyWireS.h>
#include <slave_address.h>
#define STATUS_UPDATE_FREQUENCY 300

// consult http://highlowtech.org/wp-content/uploads/2011/10/ATtiny45-85.png for pin numbering

#define LED_PIN 1
#define ANALOG_PIN 2 // actual pin 4 but analog and digital pins mismatch

long last_status_light_update = 0;

int sample = 42; // distinguishable number

// calling functions from here does not seem to work, so using only global variables

volatile uint8_t to_send[4] {
    0x0,
    0x2, // version
    0x0, // low byte
    0x0 // high byte
};
volatile uint8_t reg_position = 0;

void answer_request()
{  
    if (reg_position < 1 || reg_position > 3) {
        TinyWireS.send(0);
    }
    else { 
        TinyWireS.send(to_send[reg_position]);
    }
}

void receive_event(uint8_t count) {
    if (count != 1) {
        return;
    }

    reg_position = TinyWireS.receive();
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(ANALOG_PIN, INPUT);

  TinyWireS.begin(I2C_SLAVE_ADDR);
  TinyWireS.onRequest(answer_request);
  TinyWireS.onReceive(receive_event);

  last_status_light_update = millis();

  digitalWrite(LED_PIN, 1);
}

void loop() {
  TinyWireS_stop_check();
  collect_sample();
  
  update_status_light();
}

void update_status_light() {
  if (millis() - last_status_light_update > STATUS_UPDATE_FREQUENCY) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    last_status_light_update = millis();
  }
}

int append_one_count(int sample) {
    byte one_count = 0;
    int mask = 1 << 11;
    while (mask > 0) {
        one_count += ((sample & mask) > 0);
        mask >>= 1;
    }
    return sample | (one_count << 12);
}

void collect_sample() {
  sample = append_one_count(analogRead(ANALOG_PIN));
  noInterrupts();
  to_send[2] = lowByte(sample);
  to_send[3] = highByte(sample);
  interrupts();
}
