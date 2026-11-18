#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <sys/neutrino.h>
#include "ecu.h"
#include "led.h"

#define HALF_BLINK_NS 500000000ULL

static int send_leds(Ecu *ecu, int l, int r) {
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "LED:%d,%d\n", l, r);
    int rc = ecu_write_line(ecu, cmd);
    if (rc == 0) {
        pthread_mutex_lock(&ecu->state_lock);
        ecu->led_left = l;
        ecu->led_right = r;
        pthread_mutex_unlock(&ecu->state_lock);
    }
    return rc;
}

void *led_thread(void *arg) {
    Ecu *ecu = (Ecu *)arg;
    struct _pulse pulse;
    int phase = 0;
    if (send_leds(ecu, 0, 0) == -1) return NULL;

    for (;;) {
        int mode, emergency;
        pthread_mutex_lock(&ecu->state_lock);
        mode = ecu->indicator_mode;
        emergency = ecu->emergency;
        pthread_mutex_unlock(&ecu->state_lock);
        int active = emergency || mode != 0;

        if (active) {
            uint64_t timeout = HALF_BLINK_NS;
            if (TimerTimeout(CLOCK_MONOTONIC, _NTO_TIMEOUT_RECEIVE,
                             NULL, &timeout, NULL) == -1) {
                perror("LED TimerTimeout");
                break;
            }
        }

        int got = MsgReceivePulse(ecu->led_chid, &pulse, sizeof(pulse), NULL);
        if (got == -1) {
            if (errno == EINTR) continue;
            if (errno != ETIMEDOUT) { perror("LED pulse receive"); break; }
            phase = !phase;
        } else if (pulse.code == PULSE_LED) {
            phase = 1;  /* Show the selected indicators immediately on a change. */
        }

        pthread_mutex_lock(&ecu->state_lock);
        mode = ecu->indicator_mode;
        emergency = ecu->emergency;
        pthread_mutex_unlock(&ecu->state_lock);
        int l = phase && (emergency || mode == 1);
        int r = phase && (emergency || mode == 2);
        if (send_leds(ecu, l, r) == -1) {
            perror("LED serial send");
            break;
        }
    }
    return NULL;
}
