#include <TinyWireS.h>
#include <slave_address.h>
#define STATUS_UPDATE_FREQUENCY 300

// consult http://highlowtech.org/wp-content/uploads/2011/10/ATtiny45-85.png for pin numbering

#define LED_PIN 1
#define ANALOG_PIN 2 // actual pin 4 but analog and digital pins mismatch

#define MAX_SAMPLES 5

long last_status_light_update = 0;

int sample = 42; // distinguishable number

// calling functions from here does not seem to work, so using only global variables

volatile uint8_t to_send[2] {
    0x0, // low byte
    0x0 // high byte
};
volatile byte sending = 0;

void answer_request()
{  
  TinyWireS.send(to_send[sending]);
  sending++;
  if (sending > 1) {
    sending = 0;
  }
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(ANALOG_PIN, INPUT);

  TinyWireS.begin(I2C_SLAVE_ADDR);
  TinyWireS.onRequest(answer_request);

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

void collect_sample() {
  sample = analogRead(ANALOG_PIN);
  noInterrupts();
  to_send[0] = lowByte(sample);
  to_send[1] = highByte(sample);
  interrupts();
}
