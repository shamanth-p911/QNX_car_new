
#ifndef PUSHBUTTON_H
#define PUSHBUTTON_H

#include <sys/neutrino.h>

#define PULSE_BUTTON_UPDATE (_PULSE_CODE_MINAVAIL + 2)

typedef struct {
    int channel_id;
    int led_coid;
} PushbuttonConfig;

void *pushbutton_thread(void *arg);

#endif
