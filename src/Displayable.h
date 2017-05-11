#ifndef DISPLAYABLE_H
#define DISPLAYABLE_H 

class Displayable {
  public:
    Displayable();
    Displayable(const char*, float);
    
    const char* getString();
  private:
    char _string[15];
};
#endif
