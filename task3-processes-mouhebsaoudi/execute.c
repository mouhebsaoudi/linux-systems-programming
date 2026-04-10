#include "parse.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

/* TODO: implement this */
int run_pipeline(struct pipeline *p) {
    if (!p) return 1;
    struct command *c = &p->first_command;
    if (!c || c->argc == 0) return 0;

    int n = 0;
    for (struct command *t = c; t && t->argc; t = t->next) n++;
    pid_t *pids = calloc(n, sizeof(pid_t));
    if (!pids) { perror("callocc"); return 1; }

    int prev = -1;
    int i = 0;
    pid_t last_pid = 0;

    for (; c && c->argc; c = c->next, i++) {
        int pipefd[2] = {-1, -1};
        int need_pipe = c->next != NULL;
        if (need_pipe) {
            if (pipe(pipefd) < 0) {
                perror("pipe");
                if (prev != -1) close(prev);
                for (int k = 0; k < i; k++) waitpid(pids[k], NULL, 0);
                free(pids);
                return 1;
            }
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("forkk");
            if (prev != -1) close(prev);
            if (need_pipe) { close(pipefd[0]); close(pipefd[1]); }
            for (int k = 0; k < i; k++) waitpid(pids[k], NULL, 0);
            free(pids);
            return 1;
        }

        if (pid == 0) {
            if (c->input_redir) {
                int fd = open(c->input_redir, O_RDONLY);
                if (fd < 0) { perror("open"); _exit(1); }
                if (dup2(fd, 0) < 0) { perror("dup2"); _exit(1); }
                close(fd);
            } else if (prev != -1) {
                if (dup2(prev, 0) < 0) { perror("dup2"); _exit(1); }
            }

            if (c->output_redir) {
                int fd = open(c->output_redir, O_WRONLY|O_CREAT|O_TRUNC, 0666);
                if (fd < 0) { perror("open"); _exit(1); }
                if (dup2(fd, 1) < 0) { perror("dup2"); _exit(1); }
                close(fd);
            } else if (need_pipe) {
                if (dup2(pipefd[1], 1) < 0) { perror("dup2"); _exit(1); }
            }

            if (prev!=-1) close(prev);
            if (need_pipe) { close(pipefd[0]); close(pipefd[1]); }

            execvp(c->argv[0], c->argv);
            perror(c->argv[0]);
            _exit(127);
        }

        pids[i] = pid;
        last_pid = pid;

        if (prev != -1) close(prev);
        if (need_pipe) {
            close(pipefd[1]);
            prev = pipefd[0];
        } else {
            prev = -1;
        }
    }

    if (prev != -1) close(prev);
    if (p->background) { free(pids); return 0; }

    int st = 0, last_st = 0, err = 0;
    for (int k = 0; k < n; k++) {
        pid_t w = waitpid(pids[k], &st, 0);
        if (w < 0) { if (errno == EINTR) {k--; continue;} perror("watpid"); err = 1; }
        if (w == last_pid) last_st = st;
    }
    free(pids);

    if (err) return 1;
    if (WIFEXITED(last_st)) return WEXITSTATUS(last_st);
    if (WIFSIGNALED(last_st)) return 128 + WTERMSIG(last_st);
    return 1;
}

/* TODO: implement this */
int run_builtin(enum builtin_type builtinn, char *builtin_arg) {
    if (builtinn == BUILTIN_WAIT) {
        int status = 0;
        if (builtin_arg && *builtin_arg) {
            pid_t pid = (pid_t)atoi(builtin_arg);
            if (pid <= 0) { fprintf(stderr, "wait: invaliid pid\n"); return 1; }
            if (waitpid(pid, &status, 0) < 0) { perror("waitpid"); return 1; }
        } else {
            if (waitpid(-1, &status, 0) < 0) {
                if (errno == ECHILD) return 0;
                perror("waitpid");
                return 1;
            }
        }
        if (WIFEXITED(status)) return WEXITSTATUS(status);
        if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
        return 0;
    } else if (builtinn == BUILTIN_EXIT) {
        int code = 0;
        if (builtin_arg && *builtin_arg) code = atoi(builtin_arg) & 0xFF;
        exit(code);
    } else if (builtinn == BUILTIN_KILL) {
        if (!builtin_arg || !*builtin_arg) { fprintf(stderr, "kill: missing pid\n"); return 1; }
        pid_t pid = (pid_t)atoi(builtin_arg);
        if (pid <= 0) { fprintf(stderr, "kill: invalid pid\n"); return 1; }
        if (kill(pid, SIGTERM) < 0) { perror("kill"); return 1; }
        return 0;
    }
    return 1;
}

