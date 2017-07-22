#include <Led.h>

Led::Led(int gpio) {
    this->gpio = gpio;
    pinMode(gpio, OUTPUT);
    off();
    stop();
}

void Led::loop() {
    if (led_on_at && led_on_at + frequency < millis()) {
        // should turn off
        off();
    }
    else if (led_off_at && led_off_at + frequency < millis()) {
        if (next == FAST_BLINK) {
            blink_fast(counter);
        }
        else {
            blink_slow(counter);
        }
    }
}

void Led::blink_fast(int times) {
    if (times == 0) {
        stop();
        return;
    }
    next = FAST_BLINK;
    frequency = FAST_BLINK_MS;
    on();
    counter = times - 1;
}

void Led::blink_slow(int times) {
    if (times == 0) {
        stop();
        return;
    }
    next = SLOW_BLINK;
    frequency = SLOW_BLINK_MS;
    on();
    counter = times - 1;
}

void Led::on() {
    digitalWrite(gpio, LOW);
    led_on_at = millis();
    led_off_at = 0;
}

void Led::off() {
    digitalWrite(gpio, HIGH);
    led_off_at = millis();
    led_on_at = 0;
}

void Led::stop() {
    led_on_at = led_off_at = 0;
}
