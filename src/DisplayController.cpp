#include <DisplayController.h>

DisplayController::DisplayController(OLED oled) {
    this->oled = oled;
    displayables_size = 0;
    displayables_counter = 0;
    last_refresh_millis = 0;
    page = 0;
    changed = true;

    rssi = -1;
    rssi_packet = -1;
    snr = -1;

    to_print = false;
    for (int i = 0; i < MAX_LINES; ++i) {
        strcpy(to_print_lines[i], "");
    }

}

void DisplayController::begin() {
    oled.begin();
    oled.on();
}

void DisplayController::displayDetailed(int displayable_index) {
    oled.print("Zelenik Detailed");
    if (displayable_index + 1 <= displayables_size) {
        oled.clear();
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
    if (to_print) {
        oled.clear();
        to_print = false;
        for (int i = 0; i < MAX_LINES; ++i) {
            oled.print(to_print_lines[i], i, 0);
            strcpy(to_print_lines[i], "");
        }
        return;
    }

    if (state == ota_update) {
        oled.clear();
        oled.print("Updating...", 3, 1);
        return;
    }
    int pages = getPagesCount();

    if (page == 0) {
        if (displayables_size > 0 ) {
            oled.clear();
            oled.print("Zelenik Senses");
            if (millis() - last_refresh_millis > UPDATE_DISPLAY_SECONDS*1000) {
                last_refresh_millis = millis();
                displayables_counter++;
            }
            if (displayables_counter >= displayables_size) {
                displayables_counter = 0;
            }
            oled.print((char*)displayables[displayables_counter].getString(), 3, 1);
        }
    }
    else if (page == 1) {
        print_on_refresh(0, "Zelenik LoRa");
        print_on_refresh(2, String("RSSI: ") + rssi);
        print_on_refresh(4, String("RSSIpacket: ") + rssi_packet);
        print_on_refresh(6, String("SNR: ") + snr);
    }
    else if (page < pages) {
        displayDetailed(page-2);
    }
}

void DisplayController::print_on_refresh(int line, String string) {
    print_on_refresh(line, string.c_str());
}
/* This function is superior to OLED::print 
 * because it refreshes the whole screen at once, later 
 * when DisplayController::refresh is called.
 * With the refresh rate being as slow as it is now, 
 * it is an improvement to refresh it once 
 * and update all the lines. */
void DisplayController::print_on_refresh(int line, const char* string) {
    if (line >= MAX_LINES) {
        Serial.printf("Line number %d should be at maximum %d. Ignoring print_on_refresh call.\n", line, MAX_LINES);
        return;
    }
    if (strlen(string) >= MAX_LINE_LENGTH) {
        Serial.printf("Line length %d should be at maximum %d. Truncating down to maximum\n", strlen(string), MAX_LINE_LENGTH);
    }
    strncpy(to_print_lines[line], string, MAX_LINE_LENGTH);
    to_print = true;
    changed = true;
}

void DisplayController::update_senses(JsonObject& senses) {
    Serial.println("[display updated senses]");
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

void DisplayController::update_lora(int rssi, int rssi_packet, int snr) {
    Serial.println("[display updated lora]");

    this->rssi = rssi;
    this->rssi_packet = rssi_packet;
    this->snr = snr;

    changed = true;
}

int DisplayController::getPagesCount() {
    return displayables_size + 2;
}

void DisplayController::changePage() {
    page = (page + 1) % getPagesCount();
    changed = true;
}
