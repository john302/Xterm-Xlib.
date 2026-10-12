#define _XOPEN_SOURCE 700
#include "term.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <wchar.h>

static void term_newline(Terminal *term);

static uint32_t dec_special_graphics(uint32_t c) {
    switch (c) {
        case '`': return 0x25C6; /* ◆ diamond */
        case 'a': return 0x2592; /* ▒ checkerboard */
        case 'b': return 0x2409; /* ␉ HT */
        case 'c': return 0x240C; /* ␌ FF */
        case 'd': return 0x240D; /* ␍ CR */
        case 'e': return 0x240A; /* ␊ LF */
        case 'f': return 0x00B0; /* ° degree */
        case 'g': return 0x00B1; /* ± plus-minus */
        case 'h': return 0x2424; /* ␤ NL */
        case 'i': return 0x240B; /* ␋ VT */
        case 'j': return 0x2518; /* ┘ lower right corner */
        case 'k': return 0x2510; /* ┐ upper right corner */
        case 'l': return 0x250C; /* ┌ upper left corner */
        case 'm': return 0x2514; /* └ lower left corner */
        case 'n': return 0x253C; /* ┼ crossing lines */
        case 'o': return 0x23BA; /* ⎺ scan 1 */
        case 'p': return 0x23BB; /* ⎻ scan 3 */
        case 'q': return 0x2500; /* ─ horizontal line */
        case 'r': return 0x23BC; /* ⎼ scan 7 */
        case 's': return 0x23BD; /* ⎽ scan 9 */
        case 't': return 0x251C; /* ├ left tee */
        case 'u': return 0x2524; /* ┤ right tee */
        case 'v': return 0x2534; /* ┴ bottom tee */
        case 'w': return 0x252C; /* ┬ top tee */
        case 'x': return 0x2502; /* │ vertical line */
        case 'y': return 0x2264; /* ≤ less than or equal */
        case 'z': return 0x2265; /* ≥ greater than or equal */
        case '{': return 0x03C0; /* π pi */
        case '|': return 0x2260; /* ≠ not equal */
        case '}': return 0x00A3; /* £ pound */
        case '~': return 0x00B7; /* · middle dot */
        default:  return c;
    }
}

static void term_put_char(Terminal *term, uint32_t codepoint) {
    if (codepoint < 32 && codepoint != 0) return;

    int width = 1;
    if (codepoint >= 0x2500 && codepoint <= 0x25FF) {
        width = 1;
    } else {
        int w = wcwidth((wchar_t)codepoint);
        if (w == 2) width = 2;
        else if (w == 0) return; /* ignore zero-width combining */
        else width = 1;
    }

    if (term->cursor_x + width > term->cols) {
        if (term->autowrap) {
            term->cursor_x = 0;
            term_newline(term);
        } else {
            term->cursor_x = term->cols - width;
        }
    }

    Cell **target_grid = term->alt_buffer_active ? term->alt_grid : term->grid;
    Cell *c = &target_grid[term->cursor_y][term->cursor_x];
    c->c = codepoint;
    c->fg = term->cur_fg;
    c->bg = term->cur_bg;
    c->attr = term->cur_attr;
    if (width == 2) {
        c->attr |= ATTR_WIDE_CHAR;
        if (term->cursor_x + 1 < term->cols) {
            Cell *c2 = &target_grid[term->cursor_y][term->cursor_x + 1];
            c2->c = ' ';
            c2->fg = term->cur_fg;
            c2->bg = term->cur_bg;
            c2->attr = term->cur_attr | ATTR_WIDE_CONT;
        }
    }
    term->cursor_x += width;
    term->dirty = true;
}

static void term_scroll_up(Terminal *term, int top, int bottom) {
    if (top < 0 || bottom >= term->rows || top >= bottom) return;

    /* Push top line to scrollback if scrolling whole screen on primary buffer */
    if (top == 0 && bottom == term->rows - 1 && !term->alt_buffer_active) {
        Line *sb = &term->scrollback[term->scrollback_head];
        if (sb->cells) {
            free(sb->cells);
        }
        sb->cells = malloc(sizeof(Cell) * term->cols);
        if (sb->cells) {
            memcpy(sb->cells, term->grid[0], sizeof(Cell) * term->cols);
            sb->len = term->cols;
            term->scrollback_head = (term->scrollback_head + 1) % SCROLLBACK_SIZE;
            if (term->scrollback_count < SCROLLBACK_SIZE) {
                term->scrollback_count++;
            }
        }
    }

    Cell **target_grid = term->alt_buffer_active ? term->alt_grid : term->grid;
    Cell *top_line = target_grid[top];

    for (int y = top; y < bottom; y++) {
        target_grid[y] = target_grid[y + 1];
    }
    target_grid[bottom] = top_line;

    /* Clear bottom line */
    for (int x = 0; x < term->cols; x++) {
        target_grid[bottom][x].c = ' ';
        target_grid[bottom][x].fg = term->def_fg;
        target_grid[bottom][x].bg = term->def_bg;
        target_grid[bottom][x].attr = 0;
    }
    term->dirty = true;
}

static void term_scroll_down(Terminal *term, int top, int bottom) {
    if (top < 0 || bottom >= term->rows || top >= bottom) return;

    Cell **target_grid = term->alt_buffer_active ? term->alt_grid : term->grid;
    Cell *bottom_line = target_grid[bottom];

    for (int y = bottom; y > top; y--) {
        target_grid[y] = target_grid[y - 1];
    }
    target_grid[top] = bottom_line;

    /* Clear top line */
    for (int x = 0; x < term->cols; x++) {
        target_grid[top][x].c = ' ';
        target_grid[top][x].fg = term->def_fg;
        target_grid[top][x].bg = term->def_bg;
        target_grid[top][x].attr = 0;
    }
    term->dirty = true;
}

static void term_clear_line(Terminal *term, int row, int start_col, int end_col) {
    if (row < 0 || row >= term->rows) return;
    if (start_col < 0) start_col = 0;
    if (end_col >= term->cols) end_col = term->cols - 1;

    Cell **target_grid = term->alt_buffer_active ? term->alt_grid : term->grid;
    for (int x = start_col; x <= end_col; x++) {
        target_grid[row][x].c = ' ';
        target_grid[row][x].fg = term->cur_fg;
        target_grid[row][x].bg = term->cur_bg;
        target_grid[row][x].attr = term->cur_attr;
    }
    term->dirty = true;
}

static void term_newline(Terminal *term) {
    if (term->cursor_y == term->bottom_margin) {
        term_scroll_up(term, term->top_margin, term->bottom_margin);
    } else if (term->cursor_y < term->rows - 1) {
        term->cursor_y++;
    }
    term->dirty = true;
}

static Cell **alloc_grid(int rows, int cols, uint32_t def_fg, uint32_t def_bg) {
    Cell **g = malloc(sizeof(Cell *) * rows);
    for (int y = 0; y < rows; y++) {
        g[y] = malloc(sizeof(Cell) * cols);
        for (int x = 0; x < cols; x++) {
            g[y][x].c = ' ';
            g[y][x].fg = def_fg;
            g[y][x].bg = def_bg;
            g[y][x].attr = 0;
        }
    }
    return g;
}

static void free_grid(Cell **g, int rows) {
    if (!g) return;
    for (int y = 0; y < rows; y++) {
        free(g[y]);
    }
    free(g);
}

Terminal *term_init(int cols, int rows, int pty_fd, int child_pid) {
    Terminal *term = calloc(1, sizeof(Terminal));
    if (!term) return NULL;

    term->cols = (cols > 0 && cols <= MAX_COLS) ? cols : DEFAULT_COLS;
    term->rows = (rows > 0 && rows <= MAX_ROWS) ? rows : DEFAULT_ROWS;

    term->pty_fd = pty_fd;
    term->child_pid = child_pid;

    /* Default colors: 7 (white/silver), 0 (black/translucent) */
    term->def_fg = 7;
    term->def_bg = 0;
    term->cur_fg = term->def_fg;
    term->cur_bg = term->def_bg;
    term->cur_attr = 0;

    term->cursor_x = 0;
    term->cursor_y = 0;
    term->saved_x = 0;
    term->saved_y = 0;

    term->top_margin = 0;
    term->bottom_margin = term->rows - 1;

    term->cursor_visible = true;
    term->autowrap = true;
    term->app_cursor_keys = false;
    term->alt_buffer_active = false;

    term->grid = alloc_grid(term->rows, term->cols, term->def_fg, term->def_bg);
    term->alt_grid = alloc_grid(term->rows, term->cols, term->def_fg, term->def_bg);

    for (int i = 0; i < MAX_COLS; i++) {
        term->tab_stops[i] = (i % 8 == 0);
    }

    term->scrollback_count = 0;
    term->scrollback_head = 0;
    term->scroll_offset = 0;

    term->state = STATE_NORMAL;
    term->dirty = true;
    strncpy(term->title, "Eterm", sizeof(term->title) - 1);
    term->title_changed = true;

    term->utf8_code = 0;
    term->utf8_left = 0;
    term->charset_g0 = 'B';
    term->charset_g1 = '0';
    term->active_charset = 0;
    term->charset_target = '(';

    return term;
}

void term_free(Terminal *term) {
    if (!term) return;
    free_grid(term->grid, term->rows);
    free_grid(term->alt_grid, term->rows);

    for (int i = 0; i < SCROLLBACK_SIZE; i++) {
        if (term->scrollback[i].cells) {
            free(term->scrollback[i].cells);
        }
    }
    free(term);
}

void term_resize(Terminal *term, int new_cols, int new_rows) {
    if (new_cols <= 0 || new_rows <= 0) return;
    if (new_cols > MAX_COLS) new_cols = MAX_COLS;
    if (new_rows > MAX_ROWS) new_rows = MAX_ROWS;

    if (new_cols == term->cols && new_rows == term->rows) return;

    Cell **new_grid = alloc_grid(new_rows, new_cols, term->def_fg, term->def_bg);
    Cell **new_alt_grid = alloc_grid(new_rows, new_cols, term->def_fg, term->def_bg);

    int copy_rows = (new_rows < term->rows) ? new_rows : term->rows;
    int copy_cols = (new_cols < term->cols) ? new_cols : term->cols;

    for (int y = 0; y < copy_rows; y++) {
        memcpy(new_grid[y], term->grid[y], sizeof(Cell) * copy_cols);
        memcpy(new_alt_grid[y], term->alt_grid[y], sizeof(Cell) * copy_cols);
    }

    free_grid(term->grid, term->rows);
    free_grid(term->alt_grid, term->rows);

    term->grid = new_grid;
    term->alt_grid = new_alt_grid;
    term->cols = new_cols;
    term->rows = new_rows;

    if (term->cursor_x >= new_cols) term->cursor_x = new_cols - 1;
    if (term->cursor_y >= new_rows) term->cursor_y = new_rows - 1;

    term->top_margin = 0;
    term->bottom_margin = new_rows - 1;

    for (int i = 0; i < MAX_COLS; i++) {
        term->tab_stops[i] = (i % 8 == 0);
    }

    term->scroll_offset = 0;
    term->dirty = true;
}

void term_write_child(Terminal *term, const char *buf, size_t len) {
    if (term->pty_fd >= 0 && buf && len > 0) {
        ssize_t ret = write(term->pty_fd, buf, len);
        (void)ret;
    }
}

void term_scroll_viewport(Terminal *term, int delta) {
    if (term->alt_buffer_active) return;
    int max_scroll = term->scrollback_count;
    term->scroll_offset += delta;
    if (term->scroll_offset < 0) term->scroll_offset = 0;
    if (term->scroll_offset > max_scroll) term->scroll_offset = max_scroll;
    term->dirty = true;
}

void term_reset_viewport(Terminal *term) {
    if (term->scroll_offset != 0) {
        term->scroll_offset = 0;
        term->dirty = true;
    }
}

Cell term_get_cell(Terminal *term, int col, int row) {
    Cell default_empty = { ' ', term->def_fg, term->def_bg, 0 };
    if (col < 0 || col >= term->cols || row < 0 || row >= term->rows) {
        return default_empty;
    }

    if (term->alt_buffer_active || term->scroll_offset == 0) {
        Cell **target_grid = term->alt_buffer_active ? term->alt_grid : term->grid;
        return target_grid[row][col];
    }

    /* Scrollback viewing logic */
    int effective_row = row - term->scroll_offset;
    if (effective_row >= 0) {
        return term->grid[effective_row][col];
    }

    /* Cell is in scrollback history */
    int sb_idx = term->scrollback_head - term->scroll_offset + row;
    while (sb_idx < 0) sb_idx += SCROLLBACK_SIZE;
    sb_idx %= SCROLLBACK_SIZE;

    Line *sb = &term->scrollback[sb_idx];
    if (sb->cells && col < sb->len) {
        return sb->cells[col];
    }

    return default_empty;
}

static void handle_sgr(Terminal *term) {
    if (term->csi_param_count == 0) {
        /* Reset */
        term->cur_attr = 0;
        term->cur_fg = term->def_fg;
        term->cur_bg = term->def_bg;
        return;
    }

    for (int i = 0; i < term->csi_param_count; i++) {
        int p = term->csi_params[i];
        switch (p) {
            case 0:
                term->cur_attr = 0;
                term->cur_fg = term->def_fg;
                term->cur_bg = term->def_bg;
                break;
            case 1:
                term->cur_attr |= ATTR_BOLD;
                break;
            case 2:
                term->cur_attr |= ATTR_DIM;
                break;
            case 3:
                term->cur_attr |= ATTR_ITALIC;
                break;
            case 4:
                term->cur_attr |= ATTR_UNDERLINE;
                break;
            case 5:
            case 6:
                term->cur_attr |= ATTR_BLINK;
                break;
            case 7:
                term->cur_attr |= ATTR_REVERSE;
                break;
            case 8:
                term->cur_attr |= ATTR_INVISIBLE;
                break;
            case 22:
                term->cur_attr &= ~(ATTR_BOLD | ATTR_DIM);
                break;
            case 23:
                term->cur_attr &= ~ATTR_ITALIC;
                break;
            case 24:
                term->cur_attr &= ~ATTR_UNDERLINE;
                break;
            case 25:
                term->cur_attr &= ~ATTR_BLINK;
                break;
            case 27:
                term->cur_attr &= ~ATTR_REVERSE;
                break;
            case 28:
                term->cur_attr &= ~ATTR_INVISIBLE;
                break;
            case 30: case 31: case 32: case 33:
            case 34: case 35: case 36: case 37:
                term->cur_attr &= ~ATTR_TRUECOLOR_FG;
                term->cur_fg = p - 30;
                break;
            case 38:
                /* 38;5;n or 38;2;r;g;b */
                if (i + 2 < term->csi_param_count && term->csi_params[i + 1] == 5) {
                    term->cur_attr &= ~ATTR_TRUECOLOR_FG;
                    term->cur_fg = term->csi_params[i + 2] & 0xFF;
                    i += 2;
                } else if (i + 4 < term->csi_param_count && term->csi_params[i + 1] == 2) {
                    term->cur_attr |= ATTR_TRUECOLOR_FG;
                    uint32_t r = term->csi_params[i + 2] & 0xFF;
                    uint32_t g = term->csi_params[i + 3] & 0xFF;
                    uint32_t b = term->csi_params[i + 4] & 0xFF;
                    term->cur_fg = (r << 16) | (g << 8) | b;
                    i += 4;
                }
                break;
            case 39:
                term->cur_attr &= ~ATTR_TRUECOLOR_FG;
                term->cur_fg = term->def_fg;
                break;
            case 40: case 41: case 42: case 43:
            case 44: case 45: case 46: case 47:
                term->cur_attr &= ~ATTR_TRUECOLOR_BG;
                term->cur_bg = p - 40;
                break;
            case 48:
                /* 48;5;n or 48;2;r;g;b */
                if (i + 2 < term->csi_param_count && term->csi_params[i + 1] == 5) {
                    term->cur_attr &= ~ATTR_TRUECOLOR_BG;
                    term->cur_bg = term->csi_params[i + 2] & 0xFF;
                    i += 2;
                } else if (i + 4 < term->csi_param_count && term->csi_params[i + 1] == 2) {
                    term->cur_attr |= ATTR_TRUECOLOR_BG;
                    uint32_t r = term->csi_params[i + 2] & 0xFF;
                    uint32_t g = term->csi_params[i + 3] & 0xFF;
                    uint32_t b = term->csi_params[i + 4] & 0xFF;
                    term->cur_bg = (r << 16) | (g << 8) | b;
                    i += 4;
                }
                break;
            case 49:
                term->cur_attr &= ~ATTR_TRUECOLOR_BG;
                term->cur_bg = term->def_bg;
                break;
            case 90: case 91: case 92: case 93:
            case 94: case 95: case 96: case 97:
                term->cur_attr &= ~ATTR_TRUECOLOR_FG;
                term->cur_fg = (p - 90) + 8;
                break;
            case 100: case 101: case 102: case 103:
            case 104: case 105: case 106: case 107:
                term->cur_attr &= ~ATTR_TRUECOLOR_BG;
                term->cur_bg = (p - 100) + 8;
                break;
        }
    }
}

static void handle_csi(Terminal *term, char cmd) {
    int p1 = (term->csi_param_count > 0 && term->csi_params[0] > 0) ? term->csi_params[0] : 1;
    int p2 = (term->csi_param_count > 1 && term->csi_params[1] > 0) ? term->csi_params[1] : 1;

    Cell **target_grid = term->alt_buffer_active ? term->alt_grid : term->grid;

    if (term->csi_private) {
        switch (cmd) {
            case 'h': /* DEC private mode set */
                for (int i = 0; i < term->csi_param_count; i++) {
                    switch (term->csi_params[i]) {
                        case 1: term->app_cursor_keys = true; break;
                        case 7: term->autowrap = true; break;
                        case 25: term->cursor_visible = true; break;
                        case 47:
                        case 1049:
                            if (!term->alt_buffer_active) {
                                term->alt_buffer_active = true;
                                term->saved_x = term->cursor_x;
                                term->saved_y = term->cursor_y;
                                /* Clear alt buffer */
                                for (int y = 0; y < term->rows; y++) {
                                    for (int x = 0; x < term->cols; x++) {
                                        term->alt_grid[y][x].c = ' ';
                                        term->alt_grid[y][x].fg = term->def_fg;
                                        term->alt_grid[y][x].bg = term->def_bg;
                                        term->alt_grid[y][x].attr = 0;
                                    }
                                }
                                term->cursor_x = 0;
                                term->cursor_y = 0;
                                term->dirty = true;
                            }
                            break;
                    }
                }
                break;
            case 'l': /* DEC private mode reset */
                for (int i = 0; i < term->csi_param_count; i++) {
                    switch (term->csi_params[i]) {
                        case 1: term->app_cursor_keys = false; break;
                        case 7: term->autowrap = false; break;
                        case 25: term->cursor_visible = false; break;
                        case 47:
                        case 1049:
                            if (term->alt_buffer_active) {
                                term->alt_buffer_active = false;
                                term->cursor_x = term->saved_x;
                                term->cursor_y = term->saved_y;
                                term->dirty = true;
                            }
                            break;
                    }
                }
                break;
        }
        return;
    }

    switch (cmd) {
        case 'A': /* Cursor Up */
            term->cursor_y -= p1;
            if (term->cursor_y < term->top_margin) term->cursor_y = term->top_margin;
            break;
        case 'B': /* Cursor Down */
            term->cursor_y += p1;
            if (term->cursor_y > term->bottom_margin) term->cursor_y = term->bottom_margin;
            break;
        case 'C': /* Cursor Forward */
            term->cursor_x += p1;
            if (term->cursor_x >= term->cols) term->cursor_x = term->cols - 1;
            break;
        case 'D': /* Cursor Backward */
            term->cursor_x -= p1;
            if (term->cursor_x < 0) term->cursor_x = 0;
            break;
        case 'E': /* Cursor Next Line */
            term->cursor_y += p1;
            if (term->cursor_y > term->bottom_margin) term->cursor_y = term->bottom_margin;
            term->cursor_x = 0;
            break;
        case 'F': /* Cursor Previous Line */
            term->cursor_y -= p1;
            if (term->cursor_y < term->top_margin) term->cursor_y = term->top_margin;
            term->cursor_x = 0;
            break;
        case 'G': /* Cursor Horizontal Absolute */
            term->cursor_x = p1 - 1;
            if (term->cursor_x < 0) term->cursor_x = 0;
            if (term->cursor_x >= term->cols) term->cursor_x = term->cols - 1;
            break;
        case 'H': /* Cursor Position */
        case 'f':
            term->cursor_y = p1 - 1;
            term->cursor_x = p2 - 1;
            if (term->cursor_y < 0) term->cursor_y = 0;
            if (term->cursor_y >= term->rows) term->cursor_y = term->rows - 1;
            if (term->cursor_x < 0) term->cursor_x = 0;
            if (term->cursor_x >= term->cols) term->cursor_x = term->cols - 1;
            break;
        case 'J': /* Erase in Display */
            {
                int mode = (term->csi_param_count > 0) ? term->csi_params[0] : 0;
                if (mode == 0) {
                    /* Cursor to end of screen */
                    term_clear_line(term, term->cursor_y, term->cursor_x, term->cols - 1);
                    for (int y = term->cursor_y + 1; y < term->rows; y++) {
                        term_clear_line(term, y, 0, term->cols - 1);
                    }
                } else if (mode == 1) {
                    /* Top of screen to cursor */
                    for (int y = 0; y < term->cursor_y; y++) {
                        term_clear_line(term, y, 0, term->cols - 1);
                    }
                    term_clear_line(term, term->cursor_y, 0, term->cursor_x);
                } else if (mode == 2) {
                    /* Entire screen */
                    for (int y = 0; y < term->rows; y++) {
                        term_clear_line(term, y, 0, term->cols - 1);
                    }
                } else if (mode == 3) {
                    /* Clear scrollback */
                    for (int i = 0; i < SCROLLBACK_SIZE; i++) {
                        if (term->scrollback[i].cells) {
                            free(term->scrollback[i].cells);
                            term->scrollback[i].cells = NULL;
                        }
                    }
                    term->scrollback_count = 0;
                    term->scrollback_head = 0;
                    term->scroll_offset = 0;
                }
            }
            break;
        case 'K': /* Erase in Line */
            {
                int mode = (term->csi_param_count > 0) ? term->csi_params[0] : 0;
                if (mode == 0) {
                    term_clear_line(term, term->cursor_y, term->cursor_x, term->cols - 1);
                } else if (mode == 1) {
                    term_clear_line(term, term->cursor_y, 0, term->cursor_x);
                } else if (mode == 2) {
                    term_clear_line(term, term->cursor_y, 0, term->cols - 1);
                }
            }
            break;
        case 'L': /* Insert Line */
            for (int i = 0; i < p1; i++) {
                term_scroll_down(term, term->cursor_y, term->bottom_margin);
            }
            break;
        case 'M': /* Delete Line */
            for (int i = 0; i < p1; i++) {
                term_scroll_up(term, term->cursor_y, term->bottom_margin);
            }
            break;
        case 'P': /* Delete Character */
            {
                int n = p1;
                if (n > term->cols - term->cursor_x) n = term->cols - term->cursor_x;
                for (int x = term->cursor_x; x < term->cols - n; x++) {
                    target_grid[term->cursor_y][x] = target_grid[term->cursor_y][x + n];
                }
                for (int x = term->cols - n; x < term->cols; x++) {
                    target_grid[term->cursor_y][x].c = ' ';
                    target_grid[term->cursor_y][x].fg = term->def_fg;
                    target_grid[term->cursor_y][x].bg = term->def_bg;
                    target_grid[term->cursor_y][x].attr = 0;
                }
                term->dirty = true;
            }
            break;
        case '@': /* Insert blank characters */
            {
                int n = p1;
                if (n > term->cols - term->cursor_x) n = term->cols - term->cursor_x;
                for (int x = term->cols - 1; x >= term->cursor_x + n; x--) {
                    target_grid[term->cursor_y][x] = target_grid[term->cursor_y][x - n];
                }
                for (int x = term->cursor_x; x < term->cursor_x + n; x++) {
                    target_grid[term->cursor_y][x].c = ' ';
                    target_grid[term->cursor_y][x].fg = term->def_fg;
                    target_grid[term->cursor_y][x].bg = term->def_bg;
                    target_grid[term->cursor_y][x].attr = 0;
                }
                term->dirty = true;
            }
            break;
        case 'X': /* Erase Character */
            {
                int n = p1;
                term_clear_line(term, term->cursor_y, term->cursor_x, term->cursor_x + n - 1);
            }
            break;
        case 'd': /* Line Position Absolute */
            term->cursor_y = p1 - 1;
            if (term->cursor_y < 0) term->cursor_y = 0;
            if (term->cursor_y >= term->rows) term->cursor_y = term->rows - 1;
            break;
        case 'm': /* SGR */
            handle_sgr(term);
            break;
        case 'r': /* Set Scrolling Region */
            {
                int top = (term->csi_param_count > 0 && term->csi_params[0] > 0) ? term->csi_params[0] - 1 : 0;
                int bot = (term->csi_param_count > 1 && term->csi_params[1] > 0) ? term->csi_params[1] - 1 : term->rows - 1;
                if (top < 0) top = 0;
                if (bot >= term->rows) bot = term->rows - 1;
                if (top < bot) {
                    term->top_margin = top;
                    term->bottom_margin = bot;
                }
                term->cursor_x = 0;
                term->cursor_y = 0;
            }
            break;
        case 's': /* Save Cursor */
            term->saved_x = term->cursor_x;
            term->saved_y = term->cursor_y;
            break;
        case 'u': /* Restore Cursor */
            term->cursor_x = term->saved_x;
            term->cursor_y = term->saved_y;
            break;
        case 'n': /* Device Status Report */
            if (p1 == 5) {
                term_write_child(term, "\033[0n", 4);
            } else if (p1 == 6) {
                char reply[32];
                int len = snprintf(reply, sizeof(reply), "\033[%d;%dR", term->cursor_y + 1, term->cursor_x + 1);
                term_write_child(term, reply, len);
            }
            break;
        case 'c': /* Device Attributes */
            term_write_child(term, "\033[?1;2c", 7);
            break;
    }
    term->dirty = true;
}

static void handle_osc(Terminal *term) {
    term->osc_buf[term->osc_len] = '\0';
    if (term->osc_len >= 2 && (term->osc_buf[0] == '0' || term->osc_buf[0] == '2') && term->osc_buf[1] == ';') {
        const char *title = term->osc_buf + 2;
        strncpy(term->title, title, sizeof(term->title) - 1);
        term->title[sizeof(term->title) - 1] = '\0';
        term->title_changed = true;
    }
}

void term_process_input(Terminal *term, const char *buf, size_t len) {
    if (!term || !buf || len == 0) return;

    /* If new output comes from child, automatically snap viewport to bottom */
    term_reset_viewport(term);

    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)buf[i];

        switch (term->state) {
            case STATE_NORMAL:
                if (term->utf8_left > 0) {
                    if ((ch & 0xC0) == 0x80) {
                        term->utf8_code = (term->utf8_code << 6) | (ch & 0x3F);
                        term->utf8_left--;
                        if (term->utf8_left == 0) {
                            term_put_char(term, term->utf8_code);
                        }
                    } else {
                        term->utf8_left = 0;
                        goto handle_single_byte;
                    }
                } else {
                handle_single_byte:
                    if (ch == '\033') {
                        term->state = STATE_ESC;
                    } else if (ch == '\r') {
                        term->cursor_x = 0;
                        term->dirty = true;
                    } else if (ch == '\n' || ch == '\013' || ch == '\014') {
                        term_newline(term);
                    } else if (ch == '\b') {
                        if (term->cursor_x > 0) {
                            term->cursor_x--;
                            term->dirty = true;
                        }
                    } else if (ch == '\t') {
                        for (int x = term->cursor_x + 1; x < term->cols; x++) {
                            if (term->tab_stops[x]) {
                                term->cursor_x = x;
                                break;
                            }
                        }
                        if (term->cursor_x >= term->cols) term->cursor_x = term->cols - 1;
                        term->dirty = true;
                    } else if (ch == '\a') {
                        /* Bell */
                    } else if (ch == 0x0E) {
                        term->active_charset = 1; /* Shift Out */
                    } else if (ch == 0x0F) {
                        term->active_charset = 0; /* Shift In */
                    } else if (ch >= 32 && ch < 127) {
                        uint32_t codepoint = ch;
                        char cs = (term->active_charset == 0) ? term->charset_g0 : term->charset_g1;
                        if (cs == '0') {
                            codepoint = dec_special_graphics(ch);
                        }
                        term_put_char(term, codepoint);
                    } else if ((ch & 0xE0) == 0xC0) {
                        term->utf8_code = ch & 0x1F;
                        term->utf8_left = 1;
                    } else if ((ch & 0xF0) == 0xE0) {
                        term->utf8_code = ch & 0x0F;
                        term->utf8_left = 2;
                    } else if ((ch & 0xF8) == 0xF0) {
                        term->utf8_code = ch & 0x07;
                        term->utf8_left = 3;
                    } else if (ch >= 160) {
                        term_put_char(term, (uint32_t)ch);
                    }
                }
                break;

            case STATE_ESC:
                if (ch == '[') {
                    term->state = STATE_CSI;
                    term->csi_param_count = 0;
                    term->csi_params[0] = 0;
                    term->csi_private = false;
                    term->csi_intermediate = 0;
                } else if (ch == ']') {
                    term->state = STATE_OSC;
                    term->osc_len = 0;
                } else if (ch == '(' || ch == ')') {
                    term->charset_target = (char)ch;
                    term->state = STATE_CHARSET;
                } else if (ch == '*' || ch == '+') {
                    term->state = STATE_CHARSET;
                } else if (ch == 'M') {
                    /* Reverse Index */
                    if (term->cursor_y == term->top_margin) {
                        term_scroll_down(term, term->top_margin, term->bottom_margin);
                    } else if (term->cursor_y > 0) {
                        term->cursor_y--;
                    }
                    term->state = STATE_NORMAL;
                } else if (ch == 'E') {
                    /* Next Line */
                    term->cursor_x = 0;
                    term_newline(term);
                    term->state = STATE_NORMAL;
                } else if (ch == 'D') {
                    /* Index */
                    term_newline(term);
                    term->state = STATE_NORMAL;
                } else if (ch == '7') {
                    /* Save Cursor */
                    term->saved_x = term->cursor_x;
                    term->saved_y = term->cursor_y;
                    term->state = STATE_NORMAL;
                } else if (ch == '8') {
                    /* Restore Cursor */
                    term->cursor_x = term->saved_x;
                    term->cursor_y = term->saved_y;
                    term->state = STATE_NORMAL;
                } else if (ch == 'c') {
                    /* Reset */
                    term->cursor_x = 0;
                    term->cursor_y = 0;
                    term->cur_attr = 0;
                    term->cur_fg = term->def_fg;
                    term->cur_bg = term->def_bg;
                    term->state = STATE_NORMAL;
                } else {
                    term->state = STATE_NORMAL;
                }
                break;

            case STATE_CHARSET:
                if (term->charset_target == '(') {
                    term->charset_g0 = (char)ch;
                } else if (term->charset_target == ')') {
                    term->charset_g1 = (char)ch;
                }
                term->state = STATE_NORMAL;
                break;

            case STATE_CSI:
                if (ch == '?') {
                    term->csi_private = true;
                } else if (isdigit(ch)) {
                    if (term->csi_param_count == 0) term->csi_param_count = 1;
                    term->csi_params[term->csi_param_count - 1] =
                        term->csi_params[term->csi_param_count - 1] * 10 + (ch - '0');
                } else if (ch == ';' || ch == ':') {
                    if (term->csi_param_count < 16) {
                        term->csi_param_count++;
                        term->csi_params[term->csi_param_count - 1] = 0;
                    }
                } else if (ch >= 0x40 && ch <= 0x7E) {
                    handle_csi(term, (char)ch);
                    term->state = STATE_NORMAL;
                } else {
                    /* Intermediate or unrecognized */
                    if (ch >= 0x20 && ch <= 0x2F) {
                        term->csi_intermediate = (char)ch;
                    }
                }
                break;

            case STATE_OSC:
                if (ch == '\a' || (ch == '\\' && term->osc_len > 0 && term->osc_buf[term->osc_len - 1] == '\033')) {
                    if (ch == '\\' && term->osc_len > 0) {
                        term->osc_len--; /* strip escape */
                    }
                    handle_osc(term);
                    term->state = STATE_NORMAL;
                } else {
                    if (term->osc_len < (int)sizeof(term->osc_buf) - 1) {
                        term->osc_buf[term->osc_len++] = (char)ch;
                    }
                }
                break;
        }
    }
}
