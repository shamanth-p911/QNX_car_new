
#ifndef PUSHBUTTON_H
#define PUSHBUTTON_H

typedef struct {
    int serial_fd;
    int pulse_coid;
} PushbuttonConfig;

void *pushbutton_thread(void *arg);

#endif
