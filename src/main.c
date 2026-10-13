/*
*  This program is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation, either version 3 of the License, or
*  (at your option) any later version.
*
*  This program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/********************************************************************
* Description: A simple translucent terminal emulator.
* Author: jcartwright <jcartwright-System-Version>
* Created at: Wed Oct 14 09:17:07 AEDT 2026
* Computer: jcartwright-System-Version
* System: Linux 6.8.0-138-generic on x86_64
*
* Copyright (c) 2026 jcartwright.  All rights reserved.
*
********************************************************************/

#include "term.h"
#include "pty_mgr.h"
#include "x11_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <locale.h>

static volatile bool g_running = true;

static void sig_handler(int sig) {
    if (sig == SIGCHLD) {
        int status;
        while (waitpid(-1, &status, WNOHANG) > 0) {
            g_running = false;
        }
    } else if (sig == SIGINT || sig == SIGTERM) {
        g_running = false;
    }
}

static void print_help(const char *progname) {
    printf("Eterm-Xlib - Translucent X11 Terminal Emulator\n");
    printf("Usage: %s [options] [-e command [args...]]\n\n", progname);
    printf("Options:\n");
    printf("  -fn, -font <name>        Fixed font name (e.g. 7x13, 6x13, 9x15, 10x20, fixed)\n");
    printf("  -geometry <cols>x<rows>  Terminal grid dimensions (default: 80x24)\n");
    printf("  -trans <0-100>           Background opacity percentage (default: 80)\n");
    printf("  -theme <name>            Color theme (eterm-classic, cyberpunk, green-phosphor, amber, dracula)\n");
    printf("  -title <string>          Initial window title (default: Eterm)\n");
    printf("  --no-bevel               Disable Eterm beveled inner border\n");
    printf("  -h, --help               Display this help message\n\n");
    printf("Mouse Bindings (in the style of Xterm):\n");
    printf("  Left Click & Drag:       Select text (character-by-character)\n");
    printf("  Left Double-Click:       Select word\n");
    printf("  Left Triple-Click:       Select entire line\n");
    printf("  Left Release:            Copy selection to PRIMARY and CLIPBOARD\n");
    printf("  Right Click (Button 3):  Extend / adjust selection range\n");
    printf("  Middle Click (Button 2): Paste PRIMARY selection into terminal\n");
    printf("  Mouse Wheel:             Scroll history up / down\n\n");
    printf("Keyboard Shortcuts:\n");
    printf("  Shift+Insert:            Paste PRIMARY selection\n");
    printf("  Shift+PageUp / Down:     Scroll history buffer up / down\n");
    printf("  Ctrl+Shift+Up / Down:    Increase / decrease opacity (+-5%%)\n");
    printf("  Ctrl+Shift+(+) / (-):    Cycle to larger / smaller fixed font\n");
}

int main(int argc, char *argv[]) {
    setlocale(LC_ALL, "");

    int cols = DEFAULT_COLS;
    int rows = DEFAULT_ROWS;
    const char *font_name = NULL;
    uint8_t opacity = 244; /* ~80% opacity */
    const char *theme_name = "eterm-classic";
    const char *title = "Eterm";
    bool bevel = true;

    char *exec_cmd = NULL;
    char **exec_argv = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_help(argv[0]);
            return 0;
        } else if ((strcmp(argv[i], "-fn") == 0 || strcmp(argv[i], "-font") == 0) && i + 1 < argc) {
            font_name = argv[++i];
        } else if (strcmp(argv[i], "-geometry") == 0 && i + 1 < argc) {
            int c = 0, r = 0;
            if (sscanf(argv[++i], "%dx%d", &c, &r) == 2 && c > 0 && r > 0) {
                cols = c;
                rows = r;
            }
        } else if (strcmp(argv[i], "-trans") == 0 && i + 1 < argc) {
            int pct = atoi(argv[++i]);
            if (pct < 0) pct = 0;
            if (pct > 100) pct = 100;
            opacity = (uint8_t)((pct * 255) / 100);
        } else if (strcmp(argv[i], "-theme") == 0 && i + 1 < argc) {
            theme_name = argv[++i];
        } else if (strcmp(argv[i], "-title") == 0 && i + 1 < argc) {
            title = argv[++i];
        } else if (strcmp(argv[i], "--no-bevel") == 0 || strcmp(argv[i], "-nb") == 0) {
            bevel = false;
        } else if (strcmp(argv[i], "-e") == 0 && i + 1 < argc) {
            exec_cmd = argv[i + 1];
            exec_argv = &argv[i + 1];
            break;
        }
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_handler;
    sigaction(SIGCHLD, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    int pty_fd = -1;
    pid_t child_pid = pty_spawn(&pty_fd, cols, rows, exec_cmd, exec_argv);
    if (child_pid < 0) {
        fprintf(stderr, "Failed to spawn PTY child process\n");
        return 1;
    }

    Terminal *term = term_init(cols, rows, pty_fd, child_pid);
    if (!term) {
        fprintf(stderr, "Failed to initialize terminal state\n");
        close(pty_fd);
        return 1;
    }

    X11UI *ui = x11_ui_create(cols, rows, font_name, opacity, theme_name, title, bevel);
    if (!ui) {
        fprintf(stderr, "Failed to initialize X11 interface\n");
        term_free(term);
        close(pty_fd);
        return 1;
    }

    int x11_fd = ConnectionNumber(ui->dpy);
    char read_buf[4096];
    struct timeval last_blink_time;
    gettimeofday(&last_blink_time, NULL);

    /* Initial render */
    x11_ui_render(ui, term);

    while (g_running) {
        /* Process all available X11 events first */
        while (XPending(ui->dpy)) {
            XEvent ev;
            XNextEvent(ui->dpy, &ev);
            if (ev.type == ClientMessage &&
                (Atom)ev.xclient.data.l[0] == ui->wm_delete_window) {
                g_running = false;
                break;
            }
            x11_ui_handle_event(ui, term, &ev);
        }

        if (!g_running) break;

        /* Prepare select() multiplexing */
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(x11_fd, &read_fds);
        FD_SET(pty_fd, &read_fds);

        int max_fd = (x11_fd > pty_fd) ? x11_fd : pty_fd;

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 25000; /* 25 ms timeout for responsive I/O and cursor blink */

        int ret = select(max_fd + 1, &read_fds, NULL, NULL, &tv);
        if (ret > 0) {
            if (FD_ISSET(pty_fd, &read_fds)) {
                int n = pty_read(pty_fd, read_buf, sizeof(read_buf));
                if (n > 0) {
                    term_process_input(term, read_buf, (size_t)n);
                } else if (n < 0) {
                    /* EOF or error on PTY */
                    g_running = false;
                }
            }
        }

        /* Check window title update from OSC */
        if (term->title_changed) {
            x11_ui_set_title(ui, term->title);
            term->title_changed = false;
        }

        /* Cursor blink management */
        struct timeval now;
        gettimeofday(&now, NULL);
        long elapsed_ms = (now.tv_sec - last_blink_time.tv_sec) * 1000 +
                          (now.tv_usec - last_blink_time.tv_usec) / 1000;
        if (elapsed_ms >= 500) {
            ui->cursor_state = !ui->cursor_state;
            term->dirty = true;
            last_blink_time = now;
        }

        /* Redraw if terminal state changed */
        if (term->dirty) {
            x11_ui_render(ui, term);
        }
    }

    /* Cleanup */
    if (child_pid > 0) {
        kill(child_pid, SIGHUP);
        waitpid(child_pid, NULL, WNOHANG);
    }
    if (pty_fd >= 0) {
        close(pty_fd);
    }

    x11_ui_free(ui);
    term_free(term);

    return 0;
}
