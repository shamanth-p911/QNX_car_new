
#ifndef POTENTIOMETER_H
#define POTENTIOMETER_H

#include <sys/neutrino.h>

#define PULSE_POT_UPDATE (_PULSE_CODE_MINAVAIL + 1)

typedef struct {
    int channel_id;
} PotentiometerConfig;

void *potentiometer_thread(void *arg);

#endif
