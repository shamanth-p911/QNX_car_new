
#include <stdio.h>
#include <errno.h>
#include <sys/neutrino.h>

#include "pushbutton.h"
#include "led.h"

void *pushbutton_thread(void *arg)
{
    PushbuttonConfig *config =
        (PushbuttonConfig *)arg;

    struct _pulse pulse;

    int left_state = 0;
    int right_state = 0;

    int previous_left = 1;
    int previous_right = 1;

    printf("Pushbutton thread started\n");

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

            perror("Button pulse");
            break;
        }

        if (pulse.code != PULSE_BUTTON_UPDATE)
            continue;

        int raw = (int)pulse.value.sival_int;

        int left_raw = (raw >> 1) & 1;
        int right_raw = raw & 1;

        int changed = 0;

        if (previous_left == 1 && left_raw == 0) {
            left_state = !left_state;

            if (left_state)
                right_state = 0;

            changed = 1;
        }

        if (previous_right == 1 && right_raw == 0) {
            right_state = !right_state;

            if (right_state)
                left_state = 0;

            changed = 1;
        }

        previous_left = left_raw;
        previous_right = right_raw;

        if (changed) {
            int value = (left_state << 1) |
                         right_state;

            if (MsgSendPulse(
                    config->led_coid,
                    -1,
                    PULSE_LED_UPDATE,
                    value) == -1) {
                perror("LED pulse");
                break;
            }

            printf("BUTTON: LEFT=%d RIGHT=%d\n",
                   left_state, right_state);
            fflush(stdout);
        }
    }

    return NULL;
}
