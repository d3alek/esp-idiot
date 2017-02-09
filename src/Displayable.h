#ifndef DISPLAYABLE_H
#define DISPLAYABLE_H 

class Displayable {
  public:
    Displayable();
    Displayable(const char*, int);
    
    const char* getString();
  private:
    char _string[15];
};
#endif
