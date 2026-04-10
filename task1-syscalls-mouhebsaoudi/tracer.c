#define _GNU_SOURCE
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

static void oops(const char *msg){ perror(msg); exit(1); }

int main(int argc,char *argv[]){
    if(argc<2){ fprintf(stderr,"Usage: tracer <program> [args...]\n"); return 1; }

    pid_t kid=fork();
    if(kid<0) oops("fork failed");

    if(kid==0){
        if(ptrace(PTRACE_TRACEME,0,0,0)==-1) oops("cannot enable tracing");
        raise(SIGSTOP);
        execvp(argv[1],&argv[1]);
        oops("cannot run program");
    }

    int st=0;
    if(waitpid(kid,&st,0)==-1) oops("cannot wait for child (start)");

    if(ptrace(PTRACE_SETOPTIONS,kid,0,PTRACE_O_TRACESYSGOOD)==-1) oops("cannot set tracing options");

    int in_sys=0;
    long nr=-1, fd=-1;
    unsigned long long buf=0, cnt=0;

    if(ptrace(PTRACE_SYSCALL,kid,0,0)==-1) oops("cannot start syscall tracing");

    while(1){
        if(waitpid(kid,&st,0)==-1) oops("cannot wait for child");
        if(WIFEXITED(st)||WIFSIGNALED(st)) break;
        if(!WIFSTOPPED(st)) continue;

        int sig=WSTOPSIG(st);

        if((sig&0x80)==0x80){
            struct user_regs_struct r;
            if(ptrace(PTRACE_GETREGS,kid,0,&r)==-1) oops("cannot read registers");

            if(!in_sys){
                in_sys=1;
                nr=(long)r.orig_rax;
                if(nr==SYS_read||nr==SYS_write){
                    fd=(long)r.rdi;
                    buf=(unsigned long long)r.rsi;
                    cnt=(unsigned long long)r.rdx;
                } else {
                    nr=-1;
                }
            } else {
                if(nr==SYS_read||nr==SYS_write){
                    const char *name=(nr==SYS_read)?"read":"write";
                    long ret=(long)r.rax;
                    fprintf(stderr,"%s(%ld, 0x%llx, %llu) = %ld\n",name,fd,buf,cnt,ret);
                }
                in_sys=0;
                nr=-1;
            }

            if(ptrace(PTRACE_SYSCALL,kid,0,0)==-1) oops("cannot continue (syscall)");
        } else {
            int deliver = (sig==SIGTRAP || sig==SIGSTOP) ? 0 : sig;
            if(ptrace(PTRACE_SYSCALL,kid,0,deliver)==-1) oops("cannot continue (signal)");
        }
    }

    return 0;
}
