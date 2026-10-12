#ifndef SELECTION_H
#define SELECTION_H

#include "term.h"
#include <X11/Xlib.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SEL_NONE,
    SEL_CHAR,
    SEL_WORD,
    SEL_LINE
} SelectionType;

typedef struct {
    bool active;
    bool dragging;
    SelectionType type;

    int start_col;
    int start_row;
    int end_col;
    int end_row;

    /* Normalized bounding box (start <= end) */
    int norm_start_col;
    int norm_start_row;
    int norm_end_col;
    int norm_end_row;

    /* Multi-click tracking */
    Time last_click_time;
    int click_count;
    int last_click_col;
    int last_click_row;

    /* Extracted selected text */
    char *text;
    size_t text_len;
} Selection;

void selection_init(Selection *sel);
void selection_free(Selection *sel);

void selection_start(Selection *sel, Terminal *term, int col, int row, Time time);
void selection_drag(Selection *sel, Terminal *term, int col, int row);
void selection_end(Selection *sel, Terminal *term, Display *dpy, Window win, Time time);
void selection_extend(Selection *sel, Terminal *term, Display *dpy, Window win, int col, int row, Time time);
void selection_clear(Selection *sel, Terminal *term);

bool selection_is_cell_selected(const Selection *sel, int col, int row);

void selection_handle_request(Selection *sel, Display *dpy, XSelectionRequestEvent *req,
                              Atom utf8_atom, Atom targets_atom);
void selection_request_paste(Selection *sel, Terminal *term, Display *dpy, Window win,
                             Atom sel_atom, Atom utf8_atom, Atom prop_atom, Time time);
void selection_handle_notify(Terminal *term, Display *dpy, XSelectionEvent *notify, Atom prop_atom, Atom utf8_atom);

#endif /* SELECTION_H */
