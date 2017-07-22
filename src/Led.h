#ifndef LED_H
#define LED_H

#define FAST_BLINK_MS 200
#define SLOW_BLINK_MS 900 

#define SLOW_BLINK 0
#define FAST_BLINK 1
#include<Arduino.h>

class Led {
	public:
		Led(int);
        void loop();
        void blink_fast(int);
        void blink_slow(int);
        int gpio, next, frequency, counter;
        unsigned long led_on_at, led_off_at;
        void on();
        void off();
        void stop();
	private:

};

#endif
