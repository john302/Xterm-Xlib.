#ifndef TERM_H
#define TERM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MAX_COLS 512
#define MAX_ROWS 256
#define DEFAULT_COLS 80
#define DEFAULT_ROWS 24
#define SCROLLBACK_SIZE 2048

#define ATTR_BOLD        (1 << 0)
#define ATTR_DIM         (1 << 1)
#define ATTR_ITALIC      (1 << 2)
#define ATTR_UNDERLINE   (1 << 3)
#define ATTR_BLINK       (1 << 4)
#define ATTR_REVERSE     (1 << 5)
#define ATTR_INVISIBLE   (1 << 6)
#define ATTR_TRUECOLOR_FG (1 << 7)
#define ATTR_TRUECOLOR_BG (1 << 8)
#define ATTR_WIDE_CHAR    (1 << 9)
#define ATTR_WIDE_CONT    (1 << 10)

typedef struct {
    uint32_t c;        /* Unicode codepoint / ASCII character */
    uint32_t fg;       /* Color index (0-255) or RGB if ATTR_TRUECOLOR_FG */
    uint32_t bg;       /* Color index (0-255) or RGB if ATTR_TRUECOLOR_BG */
    uint16_t attr;     /* Attributes bitmask */
} Cell;

typedef struct {
    Cell *cells;
    int len;
} Line;

typedef enum {
    STATE_NORMAL,
    STATE_ESC,
    STATE_CSI,
    STATE_OSC,
    STATE_CHARSET
} ParserState;

typedef struct {
    int cols;
    int rows;

    int cursor_x;
    int cursor_y;
    int saved_x;
    int saved_y;

    int top_margin;
    int bottom_margin;

    uint32_t cur_fg;
    uint32_t cur_bg;
    uint16_t cur_attr;

    uint32_t def_fg;
    uint32_t def_bg;

    bool cursor_visible;
    bool autowrap;
    bool app_cursor_keys;
    bool alt_buffer_active;

    /* Primary and alternate screen buffers */
    Cell **grid;
    Cell **alt_grid;

    /* Circular scrollback buffer */
    Line scrollback[SCROLLBACK_SIZE];
    int scrollback_count;
    int scrollback_head;
    int scroll_offset; /* 0 = at bottom, >0 = viewing history */

    /* Tab stops */
    bool tab_stops[MAX_COLS];

    /* Parser state */
    ParserState state;
    int csi_params[16];
    int csi_param_count;
    bool csi_private;
    char csi_intermediate;

    char osc_buf[1024];
    int osc_len;

    char title[256];

    /* Streaming UTF-8 decoder */
    uint32_t utf8_code;
    int utf8_left;

    /* Character set translation (DEC Special Graphics vs ASCII) */
    char charset_g0;
    char charset_g1;
    int active_charset; /* 0 for G0, 1 for G1 */
    char charset_target; /* '(' or ')' */

    /* PTY and process info */
    int pty_fd;
    int child_pid;
    bool dirty;
    bool title_changed;
} Terminal;

Terminal *term_init(int cols, int rows, int pty_fd, int child_pid);
void term_free(Terminal *term);
void term_resize(Terminal *term, int new_cols, int new_rows);
void term_write_child(Terminal *term, const char *buf, size_t len);
void term_process_input(Terminal *term, const char *buf, size_t len);
void term_scroll_viewport(Terminal *term, int delta);
void term_reset_viewport(Terminal *term);
Cell term_get_cell(Terminal *term, int col, int row);

#endif /* TERM_H */
