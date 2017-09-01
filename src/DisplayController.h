#ifndef DISPLAY_CONTROLLER_H
#define DISPLAY_CONTROLLER_H

#include <OLED.h>
#include <Displayable.h>
#include <State.h>
#include <ArduinoJson.h>
#include <elapsedMillis.h>

#define MAX_DISPLAYABLES 5
#define UPDATE_DISPLAY_SECONDS 3
#define MAX_LINES 8
#define MAX_LINE_LENGTH 16

class DisplayController {
    public:
        static float parseFloat(JsonVariant& valueObject);

        DisplayController(OLED);
        void begin();
        void refresh(state_enum);
        void refresh(state_enum, bool);
        void update_senses(JsonObject&);
        void update_lora(int, int, float, float);
        void changePage();
        void displayDetailed(int);
        void print_on_refresh(int line, String string);
        void print_on_refresh(int line, const char* string);
        int getPagesCount();
        void displayLora();

        OLED oled;
        Displayable displayables[MAX_DISPLAYABLES];
        int displayables_size;
        int displayables_counter;
        volatile int page;
        volatile bool changed;
        bool to_print;
        char to_print_lines[MAX_LINES][MAX_LINE_LENGTH];
        float lora_rssi, lora_snr;
        int lora_sender, lora_message;
        elapsedMillis last_refresh_ms;
        elapsedMillis lora_update_ms;
        bool has_radio;
};
#endif
