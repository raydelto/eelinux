/* GNU/Linux o32 TLS, futex and soft-float smoke test.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#define _GNU_SOURCE
#include <errno.h>
#include <gnu/libc-version.h>
#include <limits.h>
#include <linux/futex.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>

_Static_assert(sizeof(void *) == 4, "o32 ABI required");
_Static_assert(MB_LEN_MAX == 16, "final GNU libc compiler headers required");
static _Thread_local unsigned int tls_value = 17;

__attribute__((noinline)) static int product(int a, int b)
{
    return a * b;
}

static void *worker(void *unused)
{
    (void)unused;
    if (tls_value != 17)
        return (void *)(uintptr_t)1;
    tls_value = 23;
    return (void *)(uintptr_t)(tls_value != 23);
}

int main(void)
{
    volatile int a = -7, b = 37;
    volatile double x = 1.5, y = 2.25;
    unsigned int word = 0;
    pthread_t thread;
    void *thread_result;
    if (product(a, b) != -259 || x + y != 3.75 || x * y != 3.375)
        return 1;
    if (tls_value != 17)
        return 2;
    tls_value = 31;
    errno = 0;
    if (syscall(SYS_futex, &word, FUTEX_WAIT_PRIVATE, 1, NULL, NULL, 0) != -1 || errno != EAGAIN)
        return 3;
    if (syscall(SYS_futex, &word, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0) != 0)
        return 4;
    if (pthread_create(&thread, NULL, worker, NULL) != 0)
        return 5;
    if (pthread_join(thread, &thread_result) != 0 || thread_result != NULL || tls_value != 31)
        return 6;
    printf("glibc %s: R5900 multiply, soft float, TLS, pthread and futex passed\n",
           gnu_get_libc_version());
    return 0;
}
