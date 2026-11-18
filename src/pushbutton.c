#include <errno.h>
#include <stdio.h>
#include <sys/neutrino.h>
#include "ecu.h"
#include "pushbutton.h"

void *pushbutton_thread(void *arg) {
    Ecu *ecu = (Ecu *)arg;
    struct _pulse pulse;
    int prev_left = 1, prev_right = 1;
    uint64_t last_left_ms = 0, last_right_ms = 0;

    for (;;) {
        if (MsgReceivePulse(ecu->button_chid, &pulse, sizeof(pulse), NULL) == -1) {
            if (errno == EINTR) continue;
            perror("Button pulse receive");
            break;
        }
        if (pulse.code != PULSE_BUTTON) continue;
        int raw = pulse.value.sival_int;
        int left_raw = (raw >> 1) & 1;
        int right_raw = raw & 1;
        uint64_t now = ecu_now_ms();
        int changed = 0;

        pthread_mutex_lock(&ecu->state_lock);
        if (prev_left && !left_raw && (last_left_ms == 0 || now - last_left_ms >= 180)) {
            ecu->indicator_mode = (ecu->indicator_mode == 1) ? 0 : 1;
            last_left_ms = now;
            changed = 1;
        }
        if (prev_right && !right_raw && (last_right_ms == 0 || now - last_right_ms >= 180)) {
            ecu->indicator_mode = (ecu->indicator_mode == 2) ? 0 : 2;
            last_right_ms = now;
            changed = 1;
        }
        pthread_mutex_unlock(&ecu->state_lock);
        prev_left = left_raw;
        prev_right = right_raw;

        if (changed && MsgSendPulse(ecu->led_coid, -1, PULSE_LED, 0) == -1) {
            perror("Button->LED pulse");
            break;
        }
    }
    return NULL;
}
