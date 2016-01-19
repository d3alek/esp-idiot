#ifndef GPIO_H
#define GPIO_H

#define FOREACH_GPIO(GPIO) \
        GPIO(gpio0)   \
        GPIO(gpio2)  \
        GPIO(gpio4) \
        GPIO(gpio5) \
        GPIO(gpio9) \
        GPIO(gpio10) \
        GPIO(gpio12) \
        GPIO(gpio13) \
        GPIO(gpio14) \
        GPIO(gpio15) \
        GPIO(gpio16) \

#define GENERATE_ENUM(ENUM) ENUM,
#define GENERATE_STRING(STRING) #STRING,
typedef enum {
        FOREACH_GPIO(GENERATE_ENUM)
} gpio_enum;

static const char *GPIO_STRING[] = {
        FOREACH_GPIO(GENERATE_STRING)
};

static const int GPIO_NUMBER[] = {
    0, 2, 4, 5, 9, 10, 12, 13, 14, 15, 16
};

#endif
