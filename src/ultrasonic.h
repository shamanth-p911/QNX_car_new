#ifndef ULTRASONIC_H
#define ULTRASONIC_H
void *ultrasonic_poll_thread(void *arg);
void *ultrasonic_safety_thread(void *arg);
#endif
