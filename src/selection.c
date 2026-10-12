#include "selection.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <X11/Xatom.h>

#define MULTI_CLICK_TIME 400 /* milliseconds */

static bool is_word_char(uint32_t c) {
    if (c > 127) return true; /* treat multibyte characters as word characters */
    return isalnum((int)c) || c == '_' || c == '-' || c == '.' || c == '/' || c == ':' || c == '~' || c == '$';
}

static void normalize_selection(Selection *sel) {
    if (!sel->active) {
        sel->norm_start_col = sel->norm_start_row = 0;
        sel->norm_end_col = sel->norm_end_row = 0;
        return;
    }

    if (sel->start_row < sel->end_row ||
        (sel->start_row == sel->end_row && sel->start_col <= sel->end_col)) {
        sel->norm_start_col = sel->start_col;
        sel->norm_start_row = sel->start_row;
        sel->norm_end_col = sel->end_col;
        sel->norm_end_row = sel->end_row;
    } else {
        sel->norm_start_col = sel->end_col;
        sel->norm_start_row = sel->end_row;
        sel->norm_end_col = sel->start_col;
        sel->norm_end_row = sel->start_row;
    }
}

static void find_word_bounds(Terminal *term, int row, int col, int *out_start, int *out_end) {
    if (col < 0) col = 0;
    if (col >= term->cols) col = term->cols - 1;

    Cell c = term_get_cell(term, col, row);
    bool word_mode = is_word_char(c.c);

    int start = col;
    while (start > 0) {
        Cell prev = term_get_cell(term, start - 1, row);
        if (is_word_char(prev.c) != word_mode) break;
        if (!word_mode && prev.c != ' ' && prev.c != 0) break;
        start--;
    }

    int end = col;
    while (end < term->cols - 1) {
        Cell next = term_get_cell(term, end + 1, row);
        if (is_word_char(next.c) != word_mode) break;
        if (!word_mode && next.c != ' ' && next.c != 0) break;
        end++;
    }

    *out_start = start;
    *out_end = end;
}

void selection_init(Selection *sel) {
    memset(sel, 0, sizeof(Selection));
    sel->type = SEL_NONE;
}

void selection_free(Selection *sel) {
    if (sel->text) {
        free(sel->text);
        sel->text = NULL;
    }
    sel->text_len = 0;
    sel->active = false;
    sel->dragging = false;
}

void selection_clear(Selection *sel, Terminal *term) {
    if (sel->active || sel->dragging) {
        sel->active = false;
        sel->dragging = false;
        sel->type = SEL_NONE;
        normalize_selection(sel);
        if (term) term->dirty = true;
    }
}

void selection_start(Selection *sel, Terminal *term, int col, int row, Time time) {
    if (!term) return;

    if (col < 0) col = 0;
    if (col >= term->cols) col = term->cols - 1;
    if (row < 0) row = 0;
    if (row >= term->rows) row = term->rows - 1;

    /* Check multi-click (double click for word, triple click for line) */
    if (time - sel->last_click_time < MULTI_CLICK_TIME &&
        abs(col - sel->last_click_col) <= 1 &&
        row == sel->last_click_row) {
        sel->click_count++;
    } else {
        sel->click_count = 1;
    }

    sel->last_click_time = time;
    sel->last_click_col = col;
    sel->last_click_row = row;

    if (sel->click_count == 2) {
        /* Word selection */
        sel->type = SEL_WORD;
        int w_start, w_end;
        find_word_bounds(term, row, col, &w_start, &w_end);
        sel->start_col = w_start;
        sel->end_col = w_end;
        sel->start_row = sel->end_row = row;
        sel->active = true;
        sel->dragging = true;
    } else if (sel->click_count >= 3) {
        /* Line selection */
        sel->type = SEL_LINE;
        sel->start_col = 0;
        sel->end_col = term->cols - 1;
        sel->start_row = sel->end_row = row;
        sel->active = true;
        sel->dragging = true;
    } else {
        /* Character selection */
        sel->type = SEL_CHAR;
        sel->start_col = col;
        sel->start_row = row;
        sel->end_col = col;
        sel->end_row = row;
        sel->active = false; /* becomes active once dragged or finished */
        sel->dragging = true;
    }

    normalize_selection(sel);
    term->dirty = true;
}

void selection_drag(Selection *sel, Terminal *term, int col, int row) {
    if (!sel->dragging || !term) return;

    if (col < 0) col = 0;
    if (col >= term->cols) col = term->cols - 1;
    if (row < 0) row = 0;
    if (row >= term->rows) row = term->rows - 1;

    if (sel->type == SEL_CHAR) {
        sel->end_col = col;
        sel->end_row = row;
        sel->active = (sel->start_col != sel->end_col || sel->start_row != sel->end_row);
    } else if (sel->type == SEL_WORD) {
        int w_start, w_end;
        find_word_bounds(term, row, col, &w_start, &w_end);
        if (row < sel->start_row || (row == sel->start_row && col <= sel->start_col)) {
            sel->end_col = w_start;
        } else {
            sel->end_col = w_end;
        }
        sel->end_row = row;
        sel->active = true;
    } else if (sel->type == SEL_LINE) {
        if (row < sel->start_row) {
            sel->end_col = 0;
        } else {
            sel->end_col = term->cols - 1;
        }
        sel->end_row = row;
        sel->active = true;
    }

    normalize_selection(sel);
    term->dirty = true;
}

static void extract_selection_text(Selection *sel, Terminal *term) {
    if (!sel->active || !term) return;

    if (sel->text) {
        free(sel->text);
        sel->text = NULL;
        sel->text_len = 0;
    }

    size_t capacity = (sel->norm_end_row - sel->norm_start_row + 1) * (term->cols * 4 + 2) + 16;
    sel->text = malloc(capacity);
    if (!sel->text) return;

    size_t pos = 0;
    for (int y = sel->norm_start_row; y <= sel->norm_end_row; y++) {
        int c_start = (y == sel->norm_start_row) ? sel->norm_start_col : 0;
        int c_end = (y == sel->norm_end_row) ? sel->norm_end_col : term->cols - 1;

        /* Find rightmost non-space up to c_end (for trailing trim, classic Xterm style) */
        int last_non_space = c_start - 1;
        for (int x = c_start; x <= c_end; x++) {
            Cell cell = term_get_cell(term, x, y);
            if (cell.c != ' ' && cell.c != 0) {
                last_non_space = x;
            }
        }

        int actual_end = (y == sel->norm_end_row) ? c_end : last_non_space;
        for (int x = c_start; x <= actual_end; x++) {
            Cell cell = term_get_cell(term, x, y);
            if (cell.attr & ATTR_WIDE_CONT) continue;
            uint32_t c = (cell.c == 0) ? ' ' : cell.c;

            /* Encode UTF-8 */
            if (c < 0x80) {
                sel->text[pos++] = (char)c;
            } else if (c < 0x800) {
                sel->text[pos++] = (char)(0xC0 | (c >> 6));
                sel->text[pos++] = (char)(0x80 | (c & 0x3F));
            } else if (c < 0x10000) {
                sel->text[pos++] = (char)(0xE0 | (c >> 12));
                sel->text[pos++] = (char)(0x80 | ((c >> 6) & 0x3F));
                sel->text[pos++] = (char)(0x80 | (c & 0x3F));
            } else {
                sel->text[pos++] = (char)(0xF0 | (c >> 18));
                sel->text[pos++] = (char)(0x80 | ((c >> 12) & 0x3F));
                sel->text[pos++] = (char)(0x80 | ((c >> 6) & 0x3F));
                sel->text[pos++] = (char)(0x80 | (c & 0x3F));
            }
        }

        if (y < sel->norm_end_row) {
            sel->text[pos++] = '\n';
        }
    }

    sel->text[pos] = '\0';
    sel->text_len = pos;
}

void selection_end(Selection *sel, Terminal *term, Display *dpy, Window win, Time time) {
    if (!sel->dragging) return;
    sel->dragging = false;

    if (sel->norm_start_row == sel->norm_end_row && sel->norm_start_col == sel->norm_end_col && sel->type == SEL_CHAR) {
        sel->active = false;
        term->dirty = true;
        return;
    }

    sel->active = true;
    extract_selection_text(sel, term);

    if (sel->text_len > 0) {
        /* Acquire PRIMARY selection ownership */
        XSetSelectionOwner(dpy, XA_PRIMARY, win, time);

        /* Also acquire CLIPBOARD ownership for interoperability with modern GUI apps */
        Atom clipboard_atom = XInternAtom(dpy, "CLIPBOARD", False);
        XSetSelectionOwner(dpy, clipboard_atom, win, time);
    }

    term->dirty = true;
}

void selection_extend(Selection *sel, Terminal *term, Display *dpy, Window win, int col, int row, Time time) {
    if (!term) return;

    if (col < 0) col = 0;
    if (col >= term->cols) col = term->cols - 1;
    if (row < 0) row = 0;
    if (row >= term->rows) row = term->rows - 1;

    if (!sel->active) {
        /* Start from current cursor position */
        sel->start_col = term->cursor_x;
        sel->start_row = term->cursor_y;
        sel->end_col = col;
        sel->end_row = row;
        sel->type = SEL_CHAR;
        sel->active = true;
    } else {
        /* Extend whichever end is closer */
        long dist_start = labs(row - sel->norm_start_row) * term->cols + labs(col - sel->norm_start_col);
        long dist_end = labs(row - sel->norm_end_row) * term->cols + labs(col - sel->norm_end_col);

        if (dist_start < dist_end) {
            sel->norm_start_col = col;
            sel->norm_start_row = row;
            sel->start_col = sel->norm_end_col;
            sel->start_row = sel->norm_end_row;
            sel->end_col = col;
            sel->end_row = row;
        } else {
            sel->norm_end_col = col;
            sel->norm_end_row = row;
            sel->end_col = col;
            sel->end_row = row;
        }
    }

    normalize_selection(sel);
    extract_selection_text(sel, term);

    if (sel->text_len > 0) {
        XSetSelectionOwner(dpy, XA_PRIMARY, win, time);
        Atom clipboard_atom = XInternAtom(dpy, "CLIPBOARD", False);
        XSetSelectionOwner(dpy, clipboard_atom, win, time);
    }

    term->dirty = true;
}

bool selection_is_cell_selected(const Selection *sel, int col, int row) {
    if (!sel->active) return false;

    if (row < sel->norm_start_row || row > sel->norm_end_row) return false;

    if (sel->norm_start_row == sel->norm_end_row) {
        return (col >= sel->norm_start_col && col <= sel->norm_end_col);
    }

    if (row == sel->norm_start_row) {
        return (col >= sel->norm_start_col);
    }

    if (row == sel->norm_end_row) {
        return (col <= sel->norm_end_col);
    }

    return true;
}

void selection_handle_request(Selection *sel, Display *dpy, XSelectionRequestEvent *req,
                              Atom utf8_atom, Atom targets_atom) {
    XSelectionEvent resp;
    resp.type = SelectionNotify;
    resp.display = req->display;
    resp.requestor = req->requestor;
    resp.selection = req->selection;
    resp.target = req->target;
    resp.time = req->time;
    resp.property = None;

    if (!sel->active || !sel->text) {
        XSendEvent(dpy, req->requestor, False, 0, (XEvent *)&resp);
        return;
    }

    Atom prop = (req->property == None) ? req->target : req->property;

    if (req->target == targets_atom) {
        Atom targets[4];
        targets[0] = targets_atom;
        targets[1] = utf8_atom;
        targets[2] = XA_STRING;
        targets[3] = XInternAtom(dpy, "TEXT", False);

        XChangeProperty(dpy, req->requestor, prop, XA_ATOM, 32,
                        PropModeReplace, (unsigned char *)targets, 4);
        resp.property = prop;
    } else if (req->target == utf8_atom || req->target == XA_STRING ||
               req->target == XInternAtom(dpy, "TEXT", False)) {
        XChangeProperty(dpy, req->requestor, prop, req->target, 8,
                        PropModeReplace, (unsigned char *)sel->text, sel->text_len);
        resp.property = prop;
    }

    XSendEvent(dpy, req->requestor, False, 0, (XEvent *)&resp);
}

void selection_request_paste(Selection *sel, Terminal *term, Display *dpy, Window win,
                             Atom sel_atom, Atom utf8_atom, Atom prop_atom, Time time) {
    (void)time;
    Window owner = XGetSelectionOwner(dpy, sel_atom);
    if (owner == win && sel && sel->active && sel->text && sel->text_len > 0) {
        term_write_child(term, sel->text, sel->text_len);
        return;
    }
    if (owner != None) {
        XConvertSelection(dpy, sel_atom, utf8_atom, prop_atom, win, CurrentTime);
    }
}

void selection_handle_notify(Terminal *term, Display *dpy, XSelectionEvent *notify, Atom prop_atom, Atom utf8_atom) {
    if (notify->property == None) {
        /* Fallback: if UTF8_STRING failed, try standard XA_STRING */
        if (notify->target == utf8_atom) {
            XConvertSelection(dpy, notify->selection, XA_STRING, prop_atom, notify->requestor, notify->time);
        }
        return;
    }

    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;

    int status = XGetWindowProperty(dpy, notify->requestor, notify->property, 0, 65536 / 4,
                                   False, AnyPropertyType, &actual_type, &actual_format,
                                   &nitems, &bytes_after, &data);

    if (status == Success && data && nitems > 0) {
        term_write_child(term, (const char *)data, nitems);
        XFree(data);
    }

    XDeleteProperty(dpy, notify->requestor, notify->property);
}
