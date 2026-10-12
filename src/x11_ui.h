#ifndef X11_UI_H
#define X11_UI_H

#include "term.h"
#include "selection.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>
#include <stdbool.h>

#define PADDING_LEFT 4
#define PADDING_TOP 4
#define PADDING_RIGHT 4
#define PADDING_BOTTOM 4

typedef struct {
    const char *name;
    uint32_t bg;
    uint32_t fg;
    uint32_t ansi[16];
} Theme;

typedef struct {
    Display *dpy;
    int screen;
    Visual *visual;
    int depth;
    Colormap colormap;
    Window win;
    Pixmap pixmap;
    GC gc;

    /* Xft font and drawing */
    XftDraw *xft_draw;
    XftFont *xft_font;
    XftFont *fallback_fonts[32];
    int fallback_font_count;
    double current_pixelsize;
    char current_font_name[128];
    int font_idx;

    int char_width;
    int char_height;
    int font_ascent;

    int win_width;
    int win_height;

    bool has_32bit_visual;
    uint8_t opacity; /* 0 to 255 */

    bool focused;
    bool cursor_state; /* for blinking */

    /* Precomputed palette */
    uint32_t color_table[256];
    XftColor xft_color_table[256];
    XftColor xft_fg;
    XftColor xft_bg;
    bool xft_colors_allocated;
    uint32_t bg_color;
    uint32_t fg_color;

    /* Eterm bevel & styling */
    bool draw_bevel;
    uint32_t bevel_light;
    uint32_t bevel_dark;

    /* Atoms */
    Atom wm_delete_window;
    Atom net_wm_name;
    Atom net_wm_window_opacity;
    Atom utf8_string;
    Atom targets;
    Atom clipboard;
    Atom xsel_data;

    Selection selection;
} X11UI;

X11UI *x11_ui_create(int cols, int rows, const char *font_name, uint8_t opacity,
                     const char *theme_name, const char *title, bool bevel);
void x11_ui_free(X11UI *ui);

void x11_ui_render(X11UI *ui, Terminal *term);
void x11_ui_handle_event(X11UI *ui, Terminal *term, XEvent *ev);
void x11_ui_set_title(X11UI *ui, const char *title);
void x11_ui_set_opacity(X11UI *ui, uint8_t opacity);
bool x11_ui_cycle_font(X11UI *ui, Terminal *term, int direction);

#endif /* X11_UI_H */
