
#include <stdio.h>
#include <errno.h>
#include <sys/neutrino.h>

#include "potentiometer.h"

void *potentiometer_thread(void *arg)
{
    PotentiometerConfig *config =
        (PotentiometerConfig *)arg;

    struct _pulse pulse;
    int previous_percent = -1;

    printf("Potentiometer thread started\n");

    while (1) {
        int result = MsgReceivePulse(
            config->channel_id,
            &pulse,
            sizeof(pulse),
            NULL
        );

        if (result == -1) {
            if (errno == EINTR)
                continue;

            perror("Potentiometer pulse");
            break;
        }

        if (pulse.code == PULSE_POT_UPDATE) {
            int adc = (int)pulse.value.sival_int;

            if (adc < 0 || adc > 4095)
                continue;

            int percent = (adc * 100) / 4095;

            if (percent != previous_percent) {
                printf("POT: ADC=%d | THROTTLE=%d%%\n",
                       adc, percent);
                fflush(stdout);

                previous_percent = percent;
            }
        }
    }

    return NULL;
}
