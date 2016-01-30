#ifndef ACTION_H
#define ACTION_H
struct Action {
    char sense[15];
    float delta;
    float threshold;
    int gpios[10];
    int gpiosSize;
};
#endif
