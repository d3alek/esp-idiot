#include <DisplayController.h>

DisplayController::DisplayController(OLED oled) {
    this->oled = oled;
    displayables_size = 0;
    displayables_counter = 0;
    last_refresh_millis = 0;

    oled.begin();
    oled.on();
}

void DisplayController::refresh(state_enum state) {
    if (state == serve_locally) {
        return;
    }
    oled.clear();
    oled.print("Zelenik");
    if (displayables_size > 0 ) {
        if (millis() - last_refresh_millis > UPDATE_DISPLAY_SECONDS*1000) {
            last_refresh_millis = millis();
            displayables_counter++;
        }
        if (displayables_counter >= displayables_size) {
            displayables_counter = 0;
        }
        oled.print((char*)displayables[displayables_counter].getString(), 3, 1);
    }

    oled.print((char*)STATE_STRING[state], 7, 1);
}

void DisplayController::update(JsonObject& senses) {
    int counter = 0;
    for (JsonObject::iterator it = senses.begin(); it != senses.end(); ++it) {
        displayables[counter++] = Displayable(it->key, parseInt(it->value));
        if (counter >= MAX_DISPLAYABLES) {
            break;
        }
    }
    displayables_size = counter;
}

int DisplayController::parseInt(JsonVariant& valueObject) {
    int value = -2;
    if (valueObject.is<int>()) {
        value = valueObject;
    }
    else if (valueObject.is<const char*>()) {
        const char* valueString = valueObject;
        if (valueString != NULL) {
            value = atoi(valueObject);
        }
    }
    return value;
}
