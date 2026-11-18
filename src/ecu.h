#ifndef ECU_H
#define ECU_H

#include <pthread.h>
#include <stdint.h>
#include <sys/neutrino.h>

/* Positive codes are application pulses; never use reserved negative codes. */
#define PULSE_POT       (_PULSE_CODE_MINAVAIL + 1)
#define PULSE_MOTOR     (_PULSE_CODE_MINAVAIL + 2)
#define PULSE_BUTTON    (_PULSE_CODE_MINAVAIL + 3)
#define PULSE_LED       (_PULSE_CODE_MINAVAIL + 4)
#define PULSE_DISTANCE  (_PULSE_CODE_MINAVAIL + 5)
#define PULSE_SAFETY_TICK (_PULSE_CODE_MINAVAIL + 6)

#define OBSTACLE_CM       20
#define HOLD_MS         5000
#define SENSOR_STALE_MS  350

/* Desired FIFO priorities. Higher number means higher priority on QNX. */
#define PRIO_SAFETY      50
#define PRIO_POLL        45
#define PRIO_SERIAL      40
#define PRIO_POT         20
#define PRIO_MOTOR       18
#define PRIO_LED         12
#define PRIO_BUTTON      11
#define PRIO_CONSOLE      9

typedef struct {
    int fd;
    pthread_mutex_t state_lock;
    pthread_mutex_t tx_lock;
    int pot_adc;
    int requested_speed;   /* 0..100, set by motor simulation thread */
    int distance_cm;       /* -1 means no valid echo */
    int distance_seen;
    uint64_t last_distance_ms;
    uint64_t boot_ms;
    uint64_t emergency_start_ms;
    int emergency;
    int indicator_mode;    /* 0=off 1=left 2=right */
    int led_left;
    int led_right;

    int pot_chid, pot_coid;
    int motor_chid, motor_coid;
    int button_chid, button_coid;
    int led_chid, led_coid;
    int safety_chid, safety_coid;
} Ecu;

uint64_t ecu_now_ms(void);
void ecu_sleep_ms(int ms);
int ecu_write_line(Ecu *ecu, const char *msg);

#endif
