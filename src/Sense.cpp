#define WRONG_VALUE -1003

/* Sense has 4 parts: value|expectation|ssd|wrong, vesw for short
*/
String parseSensePart(const char* vesw, int part) {
    char* current_part_begin = vesw;
    char* next_part_begin;
    int length;
    for (int i = 0; i < part; ++i) {
        if (part >= 3) {
            return String(current_part_begin)
        }
        next_part_begin = strchr(current_part_begin, (int)'|')
        if (next_part_begin == NULL) {
            return String(WRONG_VALUE);
        }
        else {
            next_part_begin += 1; // skip the |
        }
        length = next_part_begin - current_part_begin - 1;
        char value[length+1];
        strncpy(string, next_part_begin, length);
        value[length] = '\0';
        current_part_begin = next_part_begin;
    }

    return String(value)
}

int parseSenseValue(const char* vesw) {
    return atoi(parseSensePart(vesw, 0));
}

int parseSenseExpectation(const char* vesw) {
    return atoi(parseSensePart(vesw, 1));
}

int parseSenseSSD(const char* vesw) {
    return atoi(parseSensePart(vesw, 2));
}

bool parseSenseWrong(const char* vesw) {
    return parseSensePart(vesw, 3)[0] == 'w';
}

String setSenseValue(const char* vesw, int value) {
    int expectation = parseSenseExpectation(vesw);
    int ssd = parseSenseSSD(vesw);
    bool wrong = parseSenseWrong(vesw);
    return setSense(value, expectation, ssd, wrong)
}

String setSenseExpectationSSD(const char* vesw, int expectation, int ssd) {
    int value = parseSenseValue(vesw);
    bool wrong = parseSenseWrong(vesw);
    return setSense(value, expectation, ssd, wrong)
}

String setSenseWrong(const char* vesw, bool wrong) {
    int value = parseSenseValue(vesw);
    int expectation = parseSenseExpectation(vesw);
    int ssd = parseSenseSSD(vesw);
    return setSense(value, expectation, ssd, wrong)
}

String setSenseValueWrong(const char* vesw, int value, bool wrong) {
    int expectation = parseSenseExpectation(vesw);
    int ssd = parseSenseSSD(vesw);
    return setSense(value, expectation, ssd, wrong)
}

String setSense(int value, int expectation, int ssd, bool wrong) {
    return String(value) + "|" + String(expectation) + "|" + String(ssd) + "|" + wrong ? String("w") : String("c");
}
