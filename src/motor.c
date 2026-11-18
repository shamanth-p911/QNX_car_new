/* Virtual motor: no physical motor is connected. */
#include <errno.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/neutrino.h>
#include "ecu.h"
#include "motor.h"

void *motor_thread(void *arg) {
    Ecu *ecu = (Ecu *)arg;
    struct _pulse pulse;
    for (;;) {
        if (MsgReceivePulse(ecu->motor_chid, &pulse, sizeof(pulse), NULL) == -1) {
            if (errno == EINTR) continue;
            perror("Motor pulse receive");
            break;
        }
        if (pulse.code != PULSE_MOTOR) continue;
        int speed = pulse.value.sival_int;
        if (speed < 0 || speed > 100) continue;
        pthread_mutex_lock(&ecu->state_lock);
        ecu->requested_speed = speed;
        pthread_mutex_unlock(&ecu->state_lock);
        /* Effective speed is overridden to zero by safety state at display. */
    }
    return NULL;
}
