#ifndef ESP_BATTERY_H
#define ESP_BATTERY_H

#define FIVE_PERCENT 2800.
#define ABOVE_5_THRESHOLD 2775.
#define ABOVE_0_THRESHOLD 2590.
#define MAX_BATTERY 3300

class Battery {
  public:
      static float toPercent(int vcc) {
        if (vcc > MAX_BATTERY) {
          return 100;
        }

        if (vcc > FIVE_PERCENT) {
          return 100*(vcc - ABOVE_5_THRESHOLD) / (MAX_BATTERY - ABOVE_5_THRESHOLD);
        }

        if (vcc > ABOVE_0_THRESHOLD) {
          return 4.5*(vcc - ABOVE_0_THRESHOLD) / (FIVE_PERCENT - ABOVE_0_THRESHOLD);
        }

        return 0;
      }
};

#endif
