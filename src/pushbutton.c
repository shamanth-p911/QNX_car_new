
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/neutrino.h>

#include "pushbutton.h"
#include "led.h"

void *pushbutton_thread(void *arg)
{
    PushbuttonConfig *config = (PushbuttonConfig *)arg;

    int left_state = 0;
    int right_state = 0;

    int previous_left = 1;
    int previous_right = 1;

    char buffer[64];
    int index = 0;
    char ch;

    printf("Pushbutton thread started\n");

    while (1) {
        ssize_t n = read(config->serial_fd, &ch, 1);

        if (n < 0) {
            if (errno == EINTR)
                continue;

            perror("Button serial read");
            break;
        }

        if (n == 0)
            continue;

        if (ch == '\r')
            continue;

        if (ch == '\n') {
            buffer[index] = '\0';

            int left_raw, right_raw;
            int changed = 0;

            if (sscanf(buffer, "L:%d,R:%d",
                       &left_raw, &right_raw) == 2 &&
                (left_raw == 0 || left_raw == 1) &&
                (right_raw == 0 || right_raw == 1)) {

                // GPIO18: Left button
                if (previous_left == 1 && left_raw == 0) {
                    left_state = !left_state;

                    if (left_state)
                        right_state = 0;

                    changed = 1;
                }

                // GPIO19: Right button
                if (previous_right == 1 && right_raw == 0) {
                    right_state = !right_state;

                    if (right_state)
                        left_state = 0;

                    changed = 1;
                }

                previous_left = left_raw;
                previous_right = right_raw;

                if (changed) {
                    // Pack both states into one integer
                    int value = (left_state << 1) |
                                 right_state;

                    // QNX-specific API
                    if (MsgSendPulse(
                            config->pulse_coid,
                            -1,
                            PULSE_LED_UPDATE,
                            value) == -1) {
                        perror("MsgSendPulse");
                        break;
                    }

                    printf("BUTTON: LEFT=%d RIGHT=%d\n",
                           left_state, right_state);
                    fflush(stdout);
                }
            }

            index = 0;
        }
        else if (index < (int)sizeof(buffer) - 1) {
            buffer[index++] = ch;
        }
        else {
            index = 0;
        }
    }

    return NULL;
}
