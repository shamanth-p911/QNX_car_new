
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>

#define SERIAL_PORT "/dev/ser1"

int main(void)
{
    int fd = open(SERIAL_PORT, O_RDONLY | O_NOCTTY);

    if (fd == -1) {
        perror("Serial open failed");
        return 1;
    }

    struct termios tty;

    if (tcgetattr(fd, &tty) == -1) {
        perror("tcgetattr");
        close(fd);
        return 1;
    }

    cfmakeraw(&tty);
    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);

    tty.c_cflag |= CLOCAL | CREAD;

    if (tcsetattr(fd, TCSANOW, &tty) == -1) {
        perror("tcsetattr");
        close(fd);
        return 1;
    }

    int left_state = 0;
    int right_state = 0;

    int previous_left = 1;
    int previous_right = 1;

    char buffer[64];
    int index = 0;
    char ch;

    printf("QNX INDICATOR CONTROLLER STARTED\n");
    printf("LEFT = 0 | RIGHT = 0\n");
    fflush(stdout);

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

            int left_raw, right_raw;

            if (sscanf(buffer, "L:%d,R:%d",
                       &left_raw, &right_raw) == 2 &&
                (left_raw == 0 || left_raw == 1) &&
                (right_raw == 0 || right_raw == 1)) {

                // Left button: falling edge
                if (previous_left == 1 && left_raw == 0) {

                    left_state = !left_state;

                    if (left_state == 1)
                        right_state = 0;

                    printf("LEFT = %d | RIGHT = %d\n",
                           left_state, right_state);
                    fflush(stdout);
                }

                // Right button: falling edge
                if (previous_right == 1 && right_raw == 0) {

                    right_state = !right_state;

                    if (right_state == 1)
                        left_state = 0;

                    printf("LEFT = %d | RIGHT = %d\n",
                           left_state, right_state);
                    fflush(stdout);
                }

                previous_left = left_raw;
                previous_right = right_raw;
            }

            index = 0;
        }
        else if (index < (int)sizeof(buffer) - 1) {
            buffer[index++] = ch;
        }
        else {
            // Discard an oversized line
            index = 0;
        }
    }

    close(fd);
    return 0;
}
