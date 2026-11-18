// QNX SDP 8 x86_64 Mini ECU simulation. No physical motor control.
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/neutrino.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include "ecu.h"
#include "potentiometer.h"
#include "motor.h"
#include "pushbutton.h"
#include "led.h"
#include "ultrasonic.h"

uint64_t ecu_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

void ecu_sleep_ms(int ms) {
    struct timespec d = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L };
    while (nanosleep(&d, &d) == -1 && errno == EINTR) { }
}

int ecu_write_line(Ecu *ecu, const char *message) {
    const char *p = message;
    size_t remaining = strlen(message);
    pthread_mutex_lock(&ecu->tx_lock);
    while (remaining > 0) {
        ssize_t n = write(ecu->fd, p, remaining);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            pthread_mutex_unlock(&ecu->tx_lock);
            return -1;
        }
        p += n;
        remaining -= (size_t)n;
    }
    pthread_mutex_unlock(&ecu->tx_lock);
    return 0;
}

static int serial_open(const char *path) {
    int fd = open(path, O_RDWR | O_NOCTTY);
    if (fd == -1) { perror("Open /dev/ser1"); exit(1); }
    struct termios t;
    if (tcgetattr(fd, &t) == -1) { perror("tcgetattr"); exit(1); }
    if (cfsetispeed(&t, B115200) == -1 || cfsetospeed(&t, B115200) == -1) {
        perror("Baud rate"); exit(1);
    }
    /* Works on QNX without relying on cfmakeraw() availability. */
    t.c_iflag = 0; t.c_oflag = 0; t.c_lflag = 0;
    t.c_cflag &= ~(CSIZE | PARENB | CSTOPB);
    t.c_cflag |= CS8 | CLOCAL | CREAD;
    t.c_cc[VMIN] = 1; t.c_cc[VTIME] = 0;
    if (tcsetattr(fd, TCSANOW, &t) == -1) { perror("tcsetattr"); exit(1); }
    return fd;
}

static void create_pulse_channel(int *ch, int *co) {
    *ch = ChannelCreate(_NTO_CHF_FIXED_PRIORITY | _NTO_CHF_PRIVATE);
    if (*ch == -1) { perror("ChannelCreate"); exit(1); }
    *co = ConnectAttach(0, 0, *ch, _NTO_SIDE_CHANNEL, 0);
    if (*co == -1) { perror("ConnectAttach"); exit(1); }
}

static void start_thread(pthread_t *t, void *(*run)(void *), Ecu *ecu,
                         int priority, const char *name) {
    pthread_attr_t attr;
    struct sched_param sp = {0};
    sp.sched_priority = priority;
    int rc = pthread_attr_init(&attr);
    if (rc != 0) goto fail;
    if ((rc = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED)) != 0)
        goto fail_destroy;
    if ((rc = pthread_attr_setschedpolicy(&attr, SCHED_FIFO)) != 0)
        goto fail_destroy;
    if ((rc = pthread_attr_setschedparam(&attr, &sp)) != 0)
        goto fail_destroy;
    rc = pthread_create(t, &attr, run, ecu);
    pthread_attr_destroy(&attr);
    if (rc == 0) return;
    goto fail;
fail_destroy:
    pthread_attr_destroy(&attr);
fail:
    fprintf(stderr, "SCHED_FIFO thread '%s' priority %d failed: %s\n",
            name, priority, strerror(rc));
    fprintf(stderr, "Do not claim priority scheduling until this is resolved.\n");
    exit(1);
}

/* Console lines always contain the effective virtual vehicle speed. */
static void *console_thread(void *arg) {
    Ecu *ecu = (Ecu *)arg;
    for (;;) {
        uint64_t now = ecu_now_ms();
        int requested, distance, seen, mode, left, right, emergency;
        uint64_t last;
        pthread_mutex_lock(&ecu->state_lock);
        requested = ecu->requested_speed;
        distance = ecu->distance_cm;
        seen = ecu->distance_seen;
        last = ecu->last_distance_ms;
        emergency = ecu->emergency;
        mode = ecu->indicator_mode;
        left = ecu->led_left;
        right = ecu->led_right;
        pthread_mutex_unlock(&ecu->state_lock);

        int clear = seen && now - last <= SENSOR_STALE_MS &&
                    distance >= OBSTACLE_CM;
        int speed = (clear && !emergency) ? requested : 0;
        const char *condition = emergency ? "EMERGENCY" :
                                clear ? "NORMAL" : "WAIT_SENSOR";
        printf("SPEED=%3d%% REQUEST=%3d%% DIST=%4dcm STATE=%-11s TURN=%-5s LEDs=%d,%d\n",
               speed, requested, distance, condition,
               mode == 1 ? "LEFT" : mode == 2 ? "RIGHT" : "OFF",
               left, right);
        ecu_sleep_ms(100);
    }
    return NULL;
}

static void process_line(Ecu *ecu, const char *line, int *last_buttons) {
    int left, right, pot, distance;
    char extra;
    if (sscanf(line, "L:%d,R:%d,P:%d%c", &left, &right, &pot, &extra) == 3 &&
        (left == 0 || left == 1) && (right == 0 || right == 1) &&
        pot >= 0 && pot <= 4095) {
        int buttons = (left << 1) | right;
        if (buttons != *last_buttons) {
            if (MsgSendPulse(ecu->button_coid, -1, PULSE_BUTTON, buttons) == -1)
                perror("Button send pulse");
            *last_buttons = buttons;
        }
        if (MsgSendPulse(ecu->pot_coid, -1, PULSE_POT, pot) == -1)
            perror("Potentiometer send pulse");
    } else if (sscanf(line, "D:%d%c", &distance, &extra) == 1 &&
               distance >= -1 && distance <= 500) {
        if (MsgSendPulse(ecu->safety_coid, -1, PULSE_DISTANCE, distance) == -1)
            perror("Distance send pulse");
    }
}

int main(void) {
    Ecu ecu;
    memset(&ecu, 0, sizeof(ecu));
    pthread_mutex_init(&ecu.state_lock, NULL);
    pthread_mutex_init(&ecu.tx_lock, NULL);
    ecu.distance_cm = -1;
    ecu.boot_ms = ecu_now_ms();
    ecu.fd = serial_open("/dev/ser1");

    create_pulse_channel(&ecu.pot_chid, &ecu.pot_coid);
    create_pulse_channel(&ecu.motor_chid, &ecu.motor_coid);
    create_pulse_channel(&ecu.button_chid, &ecu.button_coid);
    create_pulse_channel(&ecu.led_chid, &ecu.led_coid);
    create_pulse_channel(&ecu.safety_chid, &ecu.safety_coid);

    /* Set reader priority before starting workers: all workers explicit. */
    struct sched_param sp = {0};
    sp.sched_priority = PRIO_SERIAL;
    int rc = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
    if (rc != 0) { fprintf(stderr, "Serial priority: %s\n", strerror(rc)); return 1; }

    pthread_t pot_t, motor_t, button_t, led_t, safety_t, poll_t, console_t;
    start_thread(&safety_t, ultrasonic_safety_thread, &ecu, PRIO_SAFETY, "SAFETY");
    start_thread(&poll_t, ultrasonic_poll_thread, &ecu, PRIO_POLL, "POLL");
    start_thread(&pot_t, potentiometer_thread, &ecu, PRIO_POT, "POT");
    start_thread(&motor_t, motor_thread, &ecu, PRIO_MOTOR, "VIRTUAL MOTOR");
    start_thread(&led_t, led_thread, &ecu, PRIO_LED, "LED");
    start_thread(&button_t, pushbutton_thread, &ecu, PRIO_BUTTON, "BUTTON");
    start_thread(&console_t, console_thread, &ecu, PRIO_CONSOLE, "CONSOLE");

    char line[96], c;
    size_t len = 0;
    int previous_buttons = -1;
    for (;;) {
        ssize_t n = read(ecu.fd, &c, 1);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { perror("Serial read failed"); break; }
        if (c == '\r') continue;
        if (c == '\n') {
            line[len] = '\0';
            if (len) process_line(&ecu, line, &previous_buttons);
            len = 0;
        } else if (len < sizeof(line) - 1) {
            line[len++] = c;
        } else {
            len = 0;
        }
    }
    /* ESP32 has no motor output, so dropping serial never powers a motor. */
    return 1;
}
