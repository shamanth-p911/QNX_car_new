
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

#define SERIAL_PORT "/dev/ser1"

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

static void set_priority(pthread_t thread,
                         int priority,
                         const char *name)
{
    struct sched_param param;
    memset(&param, 0, sizeof(param));

    param.sched_priority = priority;

    int result = pthread_setschedparam(
        thread,
        SCHED_FIFO,
        &param
    );

    if (result != 0) {
        fprintf(stderr,
                "%s scheduling failed: %s\n",
                name, strerror(result));
    }
    else {
        printf("%s: SCHED_FIFO priority %d\n",
               name, priority);
    }
}

int main(void)
{
    printf("QNX MINI ECU STARTING\n");

    // 1. Open ESP32 serial connection
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

    // 2. Create a QNX channel for LED events
    int chid = ChannelCreate(_NTO_CHF_PRIVATE);

    if (chid == -1) {
        perror("ChannelCreate");
        close(fd);
        return EXIT_FAILURE;
    }

    // 3. Attach a connection to the channel
    int coid = ConnectAttach(
        0,
        0,
        chid,
        _NTO_SIDE_CHANNEL,
        0
    );

    if (coid == -1) {
        perror("ConnectAttach");
        ChannelDestroy(chid);
        close(fd);
        return EXIT_FAILURE;
    }

    PushbuttonConfig button_config = {
        .serial_fd = fd,
        .pulse_coid = coid
    };

    LedConfig led_config = {
        .serial_fd = fd,
        .channel_id = chid
    };

    pthread_t button_tid;
    pthread_t led_tid;

    // 4. Create LED thread first
    int result = pthread_create(
        &led_tid,
        NULL,
        led_thread,
        &led_config
    );

    if (result != 0) {
        fprintf(stderr, "LED thread: %s\n",
                strerror(result));
        ConnectDetach(coid);
        ChannelDestroy(chid);
        close(fd);
        return EXIT_FAILURE;
    }

    // 5. Assign LED thread priority
    set_priority(led_tid, 12, "LED");

    // 6. Create pushbutton thread
    result = pthread_create(
        &button_tid,
        NULL,
        pushbutton_thread,
        &button_config
    );

    if (result != 0) {
        fprintf(stderr, "Button thread: %s\n",
                strerror(result));
        return EXIT_FAILURE;
    }

    // 7. Assign pushbutton thread priority
    set_priority(button_tid, 11, "BUTTON");

    printf("QNX RTOS THREADS CREATED\n");
    fflush(stdout);

    // 8. Wait for the threads
    pthread_join(button_tid, NULL);
    pthread_join(led_tid, NULL);

    ConnectDetach(coid);
    ChannelDestroy(chid);
    close(fd);

    return EXIT_SUCCESS;
}
