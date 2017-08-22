#ifndef STATE_H
#define STATE_H

#define FOREACH_STATE(STATE) \
        STATE(boot)   \
        STATE(connect_to_wifi) \
        STATE(wifi_wait) \
        STATE(connect_to_internet) \
        STATE(connect_to_mqtt) \
        STATE(start_server) \
        STATE(serve_locally) \
        STATE(load_config) \
        STATE(update_config) \
        STATE(wait_for_config) \
        STATE(setup_lora) \
        STATE(send_lora) \
        STATE(read_senses) \
        STATE(publish) \
        STATE(ota_update)    \
        STATE(cool_off)  \
        STATE(chill)  \
        STATE(deep_sleep)  \
        STATE(hard_reset)  \

#define GENERATE_ENUM(ENUM) ENUM,
#define GENERATE_STRING(STRING) #STRING,
typedef enum {
        FOREACH_STATE(GENERATE_ENUM)
} state_enum;

static const char *STATE_STRING[] = {
        FOREACH_STATE(GENERATE_STRING)
};

#endif
