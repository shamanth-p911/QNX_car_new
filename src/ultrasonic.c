#include <errno.h>
#include <stdio.h>
#include <sys/neutrino.h>
#include "ecu.h"
#include "ultrasonic.h"

void *ultrasonic_poll_thread(void *arg) {
    Ecu *ecu = (Ecu *)arg;
    for (;;) {
        /* QNX initiates each HC-SR04 measurement; ESP32 performs its timing. */
        if (ecu_write_line(ecu, "U?\n") == -1) {
            perror("Ultrasonic request write");
            break;
        }
        if (MsgSendPulse(ecu->safety_coid, -1, PULSE_SAFETY_TICK, 0) == -1) {
            perror("Ultrasonic safety tick");
            break;
        }
        ecu_sleep_ms(100);
    }
    return NULL;
}

void *ultrasonic_safety_thread(void *arg) {
    Ecu *ecu = (Ecu *)arg;
    struct _pulse pulse;
    for (;;) {
        if (MsgReceivePulse(ecu->safety_chid, &pulse, sizeof(pulse), NULL) == -1) {
            if (errno == EINTR) continue;
            perror("Safety pulse receive");
            break;
        }
        if (pulse.code != PULSE_DISTANCE && pulse.code != PULSE_SAFETY_TICK)
            continue;

        int led_refresh = 0;
        uint64_t now = ecu_now_ms();
        pthread_mutex_lock(&ecu->state_lock);
        if (pulse.code == PULSE_DISTANCE) {
            ecu->distance_cm = pulse.value.sival_int;
            ecu->distance_seen = 1;
            ecu->last_distance_ms = now;
        }

        int fresh = ecu->distance_seen &&
                    now - ecu->last_distance_ms <= SENSOR_STALE_MS;
        int close_or_invalid = ecu->distance_seen &&
                               (!fresh || ecu->distance_cm < 0 ||
                                ecu->distance_cm < OBSTACLE_CM);
        /* No readings within startup grace period: treat sensor as unsafe. */
        if (!ecu->distance_seen && now - ecu->boot_ms > SENSOR_STALE_MS)
            close_or_invalid = 1;

        if (close_or_invalid && !ecu->emergency) {
            ecu->emergency = 1;
            ecu->emergency_start_ms = now;
            led_refresh = 1;
        } else if (!close_or_invalid && ecu->emergency &&
                   now - ecu->emergency_start_ms >= HOLD_MS) {
            /* Restore only after 5 seconds AND the obstacle has cleared. */
            ecu->emergency = 0;
            led_refresh = 1;
        }
        pthread_mutex_unlock(&ecu->state_lock);

        if (led_refresh && MsgSendPulse(ecu->led_coid, -1, PULSE_LED, 0) == -1) {
            perror("Safety->LED pulse");
            break;
        }
    }
    return NULL;
}
