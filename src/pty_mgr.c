#include "pty_mgr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pty.h>
#include <utmp.h>
#include <sys/ioctl.h>
#include <termios.h>

pid_t pty_spawn(int *out_pty_fd, int cols, int rows, const char *cmd, char *const argv[]) {
    int master_fd = -1;
    struct winsize ws;
    ws.ws_col = cols;
    ws.ws_row = rows;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    struct termios tio;
    memset(&tio, 0, sizeof(tio));
    tio.c_iflag = ICRNL | IXON;
    tio.c_oflag = OPOST | ONLCR;
    tio.c_cflag = CS8 | CREAD;
    tio.c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHOCTL | ECHOKE;
    tio.c_cc[VINTR]  = 003;  /* Ctrl-C */
    tio.c_cc[VQUIT]  = 034;  /* Ctrl-\ */
    tio.c_cc[VERASE] = 0177; /* Del/Backspace */
    tio.c_cc[VKILL]  = 025;  /* Ctrl-U */
    tio.c_cc[VEOF]   = 004;  /* Ctrl-D */
    tio.c_cc[VSTART] = 021;  /* Ctrl-Q */
    tio.c_cc[VSTOP]  = 023;  /* Ctrl-S */
    tio.c_cc[VSUSP]  = 032;  /* Ctrl-Z */
    tio.c_cc[VREPRINT] = 022;/* Ctrl-R */
    tio.c_cc[VWERASE] = 027; /* Ctrl-W */

    pid_t pid = forkpty(&master_fd, NULL, &tio, &ws);
    if (pid < 0) {
        perror("forkpty failed");
        return -1;
    }

    if (pid == 0) {
        /* Child process */
        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);
        setenv("ETERM_VERSION", "0.9.6", 1);
        if (!getenv("LANG") || strstr(getenv("LANG"), "UTF-8") == NULL) {
            setenv("LANG", "en_US.UTF-8", 1);
        }

        if (cmd) {
            if (argv) {
                execvp(cmd, argv);
            } else {
                execlp(cmd, cmd, NULL);
            }
        } else {
            const char *shell = getenv("SHELL");
            if (!shell || *shell == '\0') {
                shell = "/bin/bash";
            }
            execlp(shell, shell, NULL);
            /* Fallback to /bin/sh if bash failed */
            execlp("/bin/sh", "/bin/sh", NULL);
        }

        perror("exec failed");
        _exit(127);
    }

    /* Parent process */
    int flags = fcntl(master_fd, F_GETFL, 0);
    if (flags != -1) {
        fcntl(master_fd, F_SETFL, flags | O_NONBLOCK);
    }

    *out_pty_fd = master_fd;
    return pid;
}

void pty_set_size(int pty_fd, int cols, int rows) {
    if (pty_fd < 0) return;
    struct winsize ws;
    ws.ws_col = cols;
    ws.ws_row = rows;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;
    ioctl(pty_fd, TIOCSWINSZ, &ws);
}

int pty_read(int pty_fd, char *buf, size_t max_len) {
    if (pty_fd < 0 || !buf || max_len == 0) return -1;
    ssize_t n = read(pty_fd, buf, max_len);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return 0;
        }
        return -1;
    }
    return (int)n;
}
