/* Export functions here */
#define _GNU_SOURCE
#include <unistd.h>
#include <sys/syscall.h>
#include <errno.h>
#include <stddef.h>


static inline long sys_read_asm(long fd, void *buf, size_t count) {
    register long rax asm("rax") = SYS_read;
    register long rdi asm("rdi") = fd;
    register long rsi asm("rsi") = (long)buf;
    register long rdx asm("rdx") = (long)count;
    asm volatile ("syscall"
                  : "+a"(rax)
                  : "D"(rdi), "S"(rsi), "d"(rdx)
                  : "rcx", "r11", "memory");
    return rax;
}

static inline long sys_write_asm(long fd, const void *buf, size_t count) {
    register long rax asm("rax") = SYS_write;
    register long rdi asm("rdi") = fd;
    register long rsi asm("rsi") = (long)buf;
    register long rdx asm("rdx") = (long)count;
    asm volatile ("syscall"
                  : "+a"(rax)
                  : "D"(rdi), "S"(rsi), "d"(rdx)
                  : "rcx", "r11", "memory");
    return rax;
}

ssize_t read(int n, void *buf, size_t count) {
    if (count == 0) return 0;
    long ret = sys_read_asm(n, buf, count);
    if (ret >= 0) return (ssize_t)ret;
    errno = (int)-ret;
    return -1;
}

ssize_t write(int n, const void *buf, size_t count) {
    if (count == 0) return 0;
    long ret = sys_write_asm(n, buf, count);
    if (ret >= 0) return (ssize_t)ret;
    errno = (int)-ret;
    return -1;
}

