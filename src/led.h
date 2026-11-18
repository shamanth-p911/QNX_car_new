
#ifndef LED_H
#define LED_H

#include <sys/neutrino.h>

#define PULSE_LED_UPDATE _PULSE_CODE_MINAVAIL

typedef struct {
    int serial_fd;
    int channel_id;
} LedConfig;

void *led_thread(void *arg);

#endif
