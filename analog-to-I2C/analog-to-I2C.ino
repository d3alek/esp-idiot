#include<TinyWireS.h>
#define I2C_SLAVE_ADDR 9
#define STATUS_UPDATE_FREQUENCY 300

#define MAX_SAMPLES_SIZE 10

#define ERROR_NO_SAMPLES 1

// consult http://highlowtech.org/wp-content/uploads/2011/10/ATtiny45-85.png for pin numbering

#define LED_PIN 1
#define ANALOG_PIN 2 // actual pin 4 but analog and digital pins mismatch

int count = 0;

long last_status_light_update = 0;

int samples[MAX_SAMPLES_SIZE];
int samples_size = 0;

bool fully_collected_samples = false;
int i, sum;

int mean = 0;

// calling functions from here does not seem to work, so using only global variables
void answer_request()
{  
  TinyWireS.send(mean);
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(ANALOG_PIN, INPUT);

  TinyWireS.begin(I2C_SLAVE_ADDR);
  TinyWireS.onRequest(answer_request);

  samples_size = 0;
  last_status_light_update = 0;
  fully_collected_samples = false;
}

void loop() {
  TinyWireS_stop_check();
  collect_sample();
  
  mean = calculate_mean();
  update_status_light();
}

void update_status_light() {
  if (millis() - last_status_light_update > STATUS_UPDATE_FREQUENCY) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    last_status_light_update = millis();
  }
}

void collect_sample() {
  if (samples_size >= MAX_SAMPLES_SIZE) {
    samples_size = 0;
    fully_collected_samples = true;
  }
  
  samples[samples_size] = analogRead(ANALOG_PIN);
  samples_size++;
}

int calculate_mean() {
  sum = 0;
  for (i = 0; i < (fully_collected_samples ? MAX_SAMPLES_SIZE : samples_size); ++i) {
    sum += samples[i];
  }

  return sum/MAX_SAMPLES_SIZE;
}

