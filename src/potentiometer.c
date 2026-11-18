#include <errno.h>
#include <stdio.h>
#include <sys/neutrino.h>
#include "ecu.h"
#include "potentiometer.h"

void *potentiometer_thread(void *arg) {
    Ecu *ecu = (Ecu *)arg;
    struct _pulse pulse;
    for (;;) {
        if (MsgReceivePulse(ecu->pot_chid, &pulse, sizeof(pulse), NULL) == -1) {
            if (errno == EINTR) continue;
            perror("Potentiometer pulse receive");
            break;
        }
        if (pulse.code != PULSE_POT) continue;
        int raw = pulse.value.sival_int;
        if (raw < 0 || raw > 4095) continue;
        int speed = (raw * 100) / 4095;
        /* Keep motor logic separate, using a genuine QNX pulse. */
        if (MsgSendPulse(ecu->motor_coid, -1, PULSE_MOTOR, speed) == -1) {
            perror("Pot->motor pulse");
            break;
        }
    }
    return NULL;
}
