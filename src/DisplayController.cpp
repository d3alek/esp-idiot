#include <DisplayController.h>

DisplayController::DisplayController(OLED oled) {
    this->oled = oled;
    displayables_size = 0;
    displayables_counter = 0;
    last_refresh_millis = 0;
    mode = 0;
    changed = true;

}

void DisplayController::begin() {
    oled.begin();
    oled.on();
}

void DisplayController::displayDetailed(int displayable_index) {
    if (displayable_index + 1 <= displayables_size) {
        oled.print((char*)displayables[displayable_index].getString(), 3, 1);
        char s[5];
        sprintf(s, "%d/%d", displayable_index+1, displayables_size);
        oled.print(s, 7, 3);
    }
}
void DisplayController::refresh(state_enum state) {
    refresh(state, false);
}

void DisplayController::refresh(state_enum state, bool force) {
    if (state == serve_locally) {
        return;
    }
    if (!force && !changed) {
        return;
    }
    changed = false;
    oled.clear();
    oled.print("Zelenik");

    if (state == ota_update) {
        oled.print("Updating...", 3, 1);
        return;
    }
    if (mode == 0) {
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
    else if (mode <= displayables_size) {
        displayDetailed(mode-1);
    }
}

void DisplayController::update(JsonObject& senses) {
    int counter = 0;
    const char* key;
    for (JsonObject::iterator it = senses.begin(); it != senses.end(); ++it) {
        key = it->key;
        if (!strcmp(key, "time")) {
            continue;
        }
        displayables[counter++] = Displayable(it->key, Sense().fromJson(it->value));
        if (counter >= MAX_DISPLAYABLES) {
            break;
        }
    }
    displayables_size = counter;
    changed = true;
}

void DisplayController::changeMode() {
    mode = (mode + 1) % (displayables_size + 1);
    changed = true;
}
