#ifndef DISPLAY_CONTROLLER_H
#define DISPLAY_CONTROLLER_H

#include <OLED.h>
#include <Displayable.h>
#include <State.h>
#include <ArduinoJson.h>

#define MAX_DISPLAYABLES 5
#define UPDATE_DISPLAY_SECONDS 3

class DisplayController {
    public:
        static float parseFloat(JsonVariant& valueObject);

        DisplayController(OLED);
        void begin();
        void refresh(state_enum);
        void refresh(state_enum, bool);
        void update(JsonObject&);
        void changeMode();
        void displayDetailed(int);

        OLED oled;
        Displayable displayables[MAX_DISPLAYABLES];
        int displayables_size;
        int displayables_counter;
        unsigned long last_refresh_millis;
        volatile int mode;
        volatile bool changed;
};
#endif
