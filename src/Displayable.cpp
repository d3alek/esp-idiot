#include <Arduino.h>

#include "Displayable.h"

const char* Displayable::getString() {
    return _string;
}

// Assuming value is a number, saving 4 characters
Displayable::Displayable(const char* key, float value) {
  int maxKeyLength = 8;
  char truncatedKey[10];
  strcpy(truncatedKey, "");
  strncat(truncatedKey, key, maxKeyLength);
  strcat(truncatedKey, "\0");
  sprintf(_string, "%s: %s", truncatedKey, String(value, 1).c_str());
}

Displayable::Displayable() {
  strcpy(_string, "");
}
