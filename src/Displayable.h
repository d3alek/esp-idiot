#ifndef DISPLAYABLE_H
#define DISPLAYABLE_H 

#include <Sense.h>

class Displayable {
  public:
    Displayable();
    Displayable(const char*, Sense);
    
    const char* getString();
  private:
    char _string[15];
};
#endif
