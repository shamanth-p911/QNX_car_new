
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <pthread.h>
#include <sched.h>
#include <sys/neutrino.h>

#include "pushbutton.h"
#include "led.h"
#include "potentiometer.h"

#define SERIAL_PORT "/dev/ser1"

#define POT_PRIORITY     20
#define LED_PRIORITY     12
#define BUTTON_PRIORITY  11
#define SERIAL_PRIORITY  10

static int configure_serial(int fd)
{
    struct termios tty;

    if (tcgetattr(fd, &tty) == -1)
        return -1;

    cfmakeraw(&tty);

    if (cfsetispeed(&tty, B115200) == -1 ||
        cfsetospeed(&tty, B115200) == -1)
        return -1;

    tty.c_cflag |= CLOCAL | CREAD;

    return tcsetattr(fd, TCSANOW, &tty);
}

static void set_priority(pthread_t tid,
                         int priority,
                         const char *name)
{
    struct sched_param param = {0};

    param.sched_priority = priority;

    int result = pthread_setschedparam(
        tid,
        SCHED_FIFO,
        &param
    );

    if (result != 0) {
        fprintf(stderr,
                "%s priority failed: %s\n",
                name, strerror(result));
    }
    else {
        printf("%s: SCHED_FIFO priority %d\n",
               name, priority);
    }
}

static int make_channel(void)
{
    return ChannelCreate(
        _NTO_CHF_PRIVATE |
        _NTO_CHF_FIXED_PRIORITY
    );
}

static int make_connection(int chid)
{
    return ConnectAttach(
        0, 0, chid, _NTO_SIDE_CHANNEL, 0
    );
}

int main(void)
{
    printf("QNX MINI ECU STARTING\n");

    int fd = open(SERIAL_PORT, O_RDWR | O_NOCTTY);

    if (fd == -1) {
        perror("Serial open");
        return EXIT_FAILURE;
    }

    if (configure_serial(fd) == -1) {
        perror("Serial configuration");
        close(fd);
        return EXIT_FAILURE;
    }

    // Create three independent QNX channels
    int pot_chid = make_channel();
    int button_chid = make_channel();
    int led_chid = make_channel();

    if (pot_chid == -1 ||
        button_chid == -1 ||
        led_chid == -1) {
        perror("ChannelCreate");
        return EXIT_FAILURE;
    }

    // Create connections for sending pulses
    int pot_coid = make_connection(pot_chid);
    int button_coid = make_connection(button_chid);
    int led_coid = make_connection(led_chid);

    if (pot_coid == -1 ||
        button_coid == -1 ||
        led_coid == -1) {
        perror("ConnectAttach");
        return EXIT_FAILURE;
    }

    PotentiometerConfig pot_config = {
        .channel_id = pot_chid
    };

    PushbuttonConfig button_config = {
        .channel_id = button_chid,
        .led_coid = led_coid
    };

    LedConfig led_config = {
        .serial_fd = fd,
        .channel_id = led_chid
    };

    pthread_t pot_tid;
    pthread_t button_tid;
    pthread_t led_tid;

    int result;

    // Create the potentiometer thread
    result = pthread_create(
        &pot_tid, NULL,
        potentiometer_thread,
        &pot_config
    );

    if (result != 0) {
        fprintf(stderr, "Pot thread: %s\n",
                strerror(result));
        return EXIT_FAILURE;
    }

    set_priority(pot_tid, POT_PRIORITY, "POT");

    // Create LED thread
    result = pthread_create(
        &led_tid, NULL,
        led_thread,
        &led_config
    );

    if (result != 0) {
        fprintf(stderr, "LED thread: %s\n",
                strerror(result));
        return EXIT_FAILURE;
    }

    set_priority(led_tid, LED_PRIORITY, "LED");

    // Create pushbutton thread
    result = pthread_create(
        &button_tid, NULL,
        pushbutton_thread,
        &button_config
    );

    if (result != 0) {
        fprintf(stderr, "Button thread: %s\n",
                strerror(result));
        return EXIT_FAILURE;
    }

    set_priority(button_tid, BUTTON_PRIORITY, "BUTTON");

    // Main thread is the serial receiver
    set_priority(
        pthread_self(),
        SERIAL_PRIORITY,
        "SERIAL"
    );

    printf("ALL QNX THREADS CREATED\n");
    fflush(stdout);

    char buffer[96];
    int index = 0;
    char ch;

    int previous_pot = -1;
    int previous_buttons = -1;

    // Main serial receiver loop
    while (1) {
        ssize_t n = read(fd, &ch, 1);

        if (n < 0) {
            if (errno == EINTR)
                continue;

            perror("Serial read");
            break;
        }

        if (n == 0)
            continue;

        if (ch == '\r')
            continue;

        if (ch == '\n') {
            buffer[index] = '\0';

            int left, right, pot;

            if (sscanf(buffer,
                       "L:%d,R:%d,P:%d",
                       &left, &right, &pot) == 3 &&
                (left == 0 || left == 1) &&
                (right == 0 || right == 1) &&
                pot >= 0 && pot <= 4095) {

                int buttons = (left << 1) | right;

                // Send button pulse only on change
                if (buttons != previous_buttons) {
                    if (MsgSendPulse(
                            button_coid,
                            -1,
                            PULSE_BUTTON_UPDATE,
                            buttons) == -1) {
                        perror("Button MsgSendPulse");
                        break;
                    }

                    previous_buttons = buttons;
                }

                // Send potentiometer pulse on change
                if (pot != previous_pot) {
                    if (MsgSendPulse(
                            pot_coid,
                            -1,
                            PULSE_POT_UPDATE,
                            pot) == -1) {
                        perror("Pot MsgSendPulse");
                        break;
                    }

                    previous_pot = pot;
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

    return EXIT_FAILURE;
}
