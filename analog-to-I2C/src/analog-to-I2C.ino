#include <TinyWireS.h>
#include <slave_address.h>
#define STATUS_UPDATE_FREQUENCY 300

// consult http://highlowtech.org/wp-content/uploads/2011/10/ATtiny45-85.png for pin numbering

#define LED_PIN 1
#define ANALOG_PIN 2 // actual pin 4 but analog and digital pins mismatch

#define MAX_SAMPLES 5

long last_status_light_update = 0;

int samples[MAX_SAMPLES];
volatile int average = 42; // distinguishable number

// calling functions from here does not seem to work, so using only global variables
void answer_request()
{  
  TinyWireS.send(average);
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

int active_sample;
int samples_sum;
int i;

void collect_sample() {
  active_sample = (active_sample + 1) % MAX_SAMPLES;
  samples[active_sample] = analogRead(ANALOG_PIN);

  samples_sum = 0;
  for (i = 0; i < MAX_SAMPLES; ++i) {
    samples_sum += samples[i];
  }

  average = samples_sum / MAX_SAMPLES;
}
