
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>
#include <sys/neutrino.h>

#include "led.h"

#define BLINK_TIME_NS 500000000LL

static int send_led_command(int fd, int left, int right)
{
    char command[32];

    snprintf(command, sizeof(command),
             "LED:%d,%d\n", left, right);

    size_t length = strlen(command);
    size_t sent = 0;

    while (sent < length) {
        ssize_t n = write(fd, command + sent,
                          length - sent);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }

        if (n == 0)
            return -1;

        sent += (size_t)n;
    }

    return 0;
}

void *led_thread(void *arg)
{
    LedConfig *config = (LedConfig *)arg;

    struct _pulse pulse;

    int left_active = 0;
    int right_active = 0;
    int blink_on = 0;

    printf("LED blinking thread started\n");

    if (send_led_command(config->serial_fd, 0, 0) == -1) {
        perror("Initial LED command");
        return NULL;
    }

    while (1) {

        /*
         * If an indicator is active, wait up to
         * 500 ms for a new QNX pulse.
         *
         * If no pulse arrives, the timeout
         * triggers the next blink transition.
         */

        if (left_active || right_active) {

            uint64_t timeout = BLINK_TIME_NS;

            if (TimerTimeout(
                    CLOCK_MONOTONIC,
                    _NTO_TIMEOUT_RECEIVE,
                    NULL,
                    &timeout,
                    NULL) == -1) {
                perror("TimerTimeout");
                break;
            }
        }

        int result = MsgReceivePulse(
            config->channel_id,
            &pulse,
            sizeof(pulse),
            NULL
        );

        if (result == -1) {

            if (errno == EINTR)
                continue;

            if (errno == ETIMEDOUT) {

                // Toggle blinking phase
                blink_on = !blink_on;

            } else {
                perror("MsgReceivePulse");
                break;
            }

        } else if (pulse.code == PULSE_LED_UPDATE) {

            int value = pulse.value.sival_int;

            left_active = (value >> 1) & 1;
            right_active = value & 1;

            // Start a newly selected indicator ON
            blink_on = (left_active || right_active) ? 1 : 0;
        }

        int left_output = left_active && blink_on;
        int right_output = right_active && blink_on;

        if (send_led_command(
                config->serial_fd,
                left_output,
                right_output) == -1) {

            perror("LED serial write");
            break;
        }

        printf("LED: LEFT=%d RIGHT=%d\n",
               left_output, right_output);

        fflush(stdout);
    }

    return NULL;
}
