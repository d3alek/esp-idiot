#include <Sense.h>


Sense::Sense() {
    value = expectation = ssd = WRONG_VALUE;
    wrong = true;
}
/* Sense has 4 parts: value|expectation|ssd|wrong, vesw for short
*/
Sense Sense::fromString(const char* vesw) {
    parse(vesw);
    return *this;
}

Sense Sense::fromJson(JsonVariant vesw) {
    if (vesw.is<const char*>()) {
        return fromString(vesw.as<const char*>());
    }
    Serial.println("JsonObject is not string, creating an all false Sense value");
    
    return *this;
}

void Sense::parse(const char* vesw) {
    const char* current_part_begin = vesw;
    char* next_part_begin;
    int length;
    char value_string[10];
    for (int i = 0; i < 4; ++i) {
        if (i >= 3) {
            wrong = String(current_part_begin)[0] == 'w';
            return;
        }
        next_part_begin = strchr(current_part_begin, (int)'|');
        if (next_part_begin == NULL) {
            Serial.printf("Encountered null next part begin on step %d, marking remaining as wrong\n", i);
            switch (i) {
                case 0:
                    value = WRONG_VALUE;
                    // cascade to others, so no break
                case 1:
                    expectation = WRONG_VALUE;
                    // cascade to others, so no break
                case 2:
                    ssd = WRONG_VALUE;
                    // cascade to others, so no break
                case 3:
                    wrong = true;
            }
            return;

        }
        else {
            next_part_begin += 1; // skip the |
        }
        length = next_part_begin - current_part_begin - 1;
        strncpy(value_string, current_part_begin, length);
        value_string[length] = '\0';
        switch(i) {
            case 0:
                value = atoi(value_string);
                break;
            case 1:
                expectation = atoi(value_string);
                break;
            case 2:
                ssd = atoi(value_string);
                break;
        }
        current_part_begin = next_part_begin;
    }
}

Sense Sense::withValue(int value) {
    this->value = value;
    this->wrong = false;
    return *this;
}

Sense Sense::withExpectationSSD(int expectation, int ssd) {
    this->expectation = expectation;
    this->ssd = ssd;
    return *this;
}

Sense Sense::withWrong(bool wrong) {
    this->wrong = wrong;
    return *this;
}

String Sense::toString() {
    return String(value) + "|" + String(expectation) + "|" + String(ssd) + "|" + (wrong ? String("w") : String("c"));
}
