#include<TinyWireS.h>
#define I2C_SLAVE_ADDR 9
#define STATUS_UPDATE_FREQUENCY 300

// consult http://highlowtech.org/wp-content/uploads/2011/10/ATtiny45-85.png for pin numbering

#define LED_PIN 1
#define ANALOG_PIN 2 // actual pin 4 but analog and digital pins mismatch

int count = 0;

long last_status_light_update = 0;

int sample;

// calling functions from here does not seem to work, so using only global variables
void answer_request()
{  
  TinyWireS.send(sample);
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(ANALOG_PIN, INPUT);

  TinyWireS.begin(I2C_SLAVE_ADDR);
  TinyWireS.onRequest(answer_request);

  last_status_light_update = millis();
  fully_collected_samples = false;

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
}
