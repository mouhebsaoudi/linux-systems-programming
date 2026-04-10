/* Export functions here */
#define _GNU_SOURCE
#include <unistd.h>
#include <sys/syscall.h>
#include <errno.h>
#include <stddef.h>


ssize_t read(int f, void *buf, size_t count) {
    if (count == 0) return 0;
    return (ssize_t)syscall(SYS_read, f, buf, count);
}

ssize_t write(int f, const void *buf, size_t count) {
    if (count == 0) return 0;
    return (ssize_t)syscall(SYS_write, f, buf, count);
}

