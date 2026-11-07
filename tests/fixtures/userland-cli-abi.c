#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/syscall.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

struct kernel_time64 { int64_t sec, nsec; };
struct termios2 {
    uint32_t c_iflag, c_oflag, c_cflag, c_lflag;
    unsigned char c_line, c_cc[23];
    uint32_t c_ispeed, c_ospeed;
};
_Static_assert(sizeof(struct termios2) == 48, "MIPS termios2 ABI");
static unsigned checks;
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "failed line %d: %s (errno=%d)\n", __LINE__, #expr, errno); \
    return 1; } ++checks; } while (0)

int main(void)
{
    struct kernel_time64 clock, timeout = { 0, 1000000 };
    int pipefd[2];
    fd_set readers;
    sigset_t mask;
    struct { const sigset_t *set; size_t size; } packed;
    struct termios initial, changed, observed;
    struct termios2 raw_initial, raw;

    CHECK(syscall(SYS_clock_gettime64, CLOCK_MONOTONIC, &clock) == 0);
    CHECK(clock.sec >= 0 && clock.nsec >= 0 && clock.nsec < 1000000000);
    CHECK(syscall(SYS_pselect6_time64, 0, NULL, NULL, NULL, &timeout, NULL) == 0);
    timeout.sec = 0; timeout.nsec = INT64_C(0x12345678) << 32;
    CHECK(syscall(SYS_pselect6_time64, 0, NULL, NULL, NULL, &timeout, NULL) == 0);
    timeout.sec = 0; timeout.nsec = 1000000000;
    CHECK(syscall(SYS_pselect6_time64, 0, NULL, NULL, NULL, &timeout, NULL) == -1 && errno == EINVAL);
    CHECK(syscall(SYS_pselect6_time64, 0, NULL, NULL, NULL, (void *)1, NULL) == -1 && errno == EFAULT);
    CHECK(pipe(pipefd) == 0);
    CHECK(write(pipefd[1], "x", 1) == 1);
    FD_ZERO(&readers); FD_SET(pipefd[0], &readers);
    sigemptyset(&mask); sigaddset(&mask, SIGUSR1);
    packed.set = &mask; packed.size = _NSIG / 8;
    timeout.sec = (INT64_C(1) << 33) + 3; timeout.nsec = 0;
    CHECK(syscall(SYS_pselect6_time64, pipefd[0] + 1, &readers, NULL, NULL, &timeout, &packed) == 1);
    CHECK(FD_ISSET(pipefd[0], &readers));
    CHECK(timeout.sec > INT32_MAX);
    packed.size = 1;
    CHECK(syscall(SYS_pselect6_time64, 0, NULL, NULL, NULL, NULL, &packed) == -1 && errno == EINVAL);
    close(pipefd[0]); close(pipefd[1]);

    CHECK(isatty(STDIN_FILENO));
    CHECK(tcgetattr(STDIN_FILENO, &initial) == 0);
    changed = initial; changed.c_lflag &= ~(ECHO | ICANON);
    CHECK(tcsetattr(STDIN_FILENO, TCSANOW, &changed) == 0);
    CHECK(tcgetattr(STDIN_FILENO, &observed) == 0);
    CHECK((observed.c_lflag & (ECHO | ICANON)) == 0);
    CHECK(tcsetattr(STDIN_FILENO, TCSADRAIN, &initial) == 0);
    CHECK(tcsetattr(STDIN_FILENO, TCSAFLUSH, &initial) == 0);
    CHECK(ioctl(STDIN_FILENO, TCGETS2, &raw_initial) == 0);
    raw = raw_initial;
    raw.c_cflag = (raw.c_cflag & ~(0010017U | 002003600000U)) | 0010000U | (0010000U << 16);
    raw.c_ispeed = 24689; raw.c_ospeed = 13457;
    CHECK(ioctl(STDIN_FILENO, TCSETS2, &raw) == 0);
    memset(&raw, 0, sizeof(raw));
    CHECK(ioctl(STDIN_FILENO, TCGETS2, &raw) == 0);
    CHECK(raw.c_ispeed == 24689 && raw.c_ospeed == 13457);
    CHECK(ioctl(STDIN_FILENO, TCSETSW2, &raw_initial) == 0);
    CHECK(ioctl(STDIN_FILENO, TCSETSF2, &raw_initial) == 0);
    printf("CLI ABI: %u checks passed; clock_gettime64, pselect6_time64, MIPS termios2\n", checks);
    return 0;
}
