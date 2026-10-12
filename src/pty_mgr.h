#ifndef PTY_MGR_H
#define PTY_MGR_H

#include <sys/types.h>
#include <stdbool.h>

pid_t pty_spawn(int *out_pty_fd, int cols, int rows, const char *cmd, char *const argv[]);
void pty_set_size(int pty_fd, int cols, int rows);
int pty_read(int pty_fd, char *buf, size_t max_len);

#endif /* PTY_MGR_H */
