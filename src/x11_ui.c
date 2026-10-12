#include "x11_ui.h"
#include "pty_mgr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>

static const char *default_font_list[] = {
    "monospace:pixelsize=11:antialias=true",
    "monospace:pixelsize=13:antialias=true",
    "monospace:pixelsize=15:antialias=true",
    "monospace:pixelsize=17:antialias=true",
    "monospace:pixelsize=20:antialias=true",
    "monospace:pixelsize=24:antialias=true",
    "DejaVu Sans Mono:pixelsize=13:antialias=true",
    "Liberation Mono:pixelsize=13:antialias=true",
    "fixed"
};
#define NUM_FONTS (int)(sizeof(default_font_list) / sizeof(default_font_list[0]))

static const Theme built_in_themes[] = {
    {
        .name = "eterm-classic",
        .bg = 0x0E141D, /* translucent dark slate blue glass */
        .fg = 0xCCE8F5, /* ice cyan / crisp white */
        .ansi = {
            0x151D28, 0xEE5D5D, 0x00E699, 0xFFD166,
            0x26C6DA, 0xC77DFF, 0x48CAE4, 0xD0E1EC,
            0x415A77, 0xFF758F, 0x52B788, 0xFFE066,
            0x4CC9F0, 0xE0AAFF, 0x90E0EF, 0xFFFFFF
        }
    },
    {
        .name = "cyberpunk",
        .bg = 0x0A0B12,
        .fg = 0x00F0FF,
        .ansi = {
            0x121420, 0xFF0055, 0x00FF9F, 0xFFE600,
            0x00B8FF, 0xD600FF, 0x00F0FF, 0xE0E6ED,
            0x323754, 0xFF3377, 0x33FFAF, 0xFFEB33,
            0x33C6FF, 0xDE33FF, 0x33F3FF, 0xFFFFFF
        }
    },
    {
        .name = "green-phosphor",
        .bg = 0x061208,
        .fg = 0x33FF66,
        .ansi = {
            0x0A1C0E, 0x22AA44, 0x33FF66, 0x55FF88,
            0x228844, 0x44DD66, 0x33EE77, 0xA8FFB8,
            0x1B4423, 0x2ECC5A, 0x44FF77, 0x66FFAA,
            0x33AA55, 0x55EE77, 0x44FF88, 0xCCFFD8
        }
    },
    {
        .name = "amber",
        .bg = 0x120B04,
        .fg = 0xFFAA00,
        .ansi = {
            0x1C1306, 0xDD7700, 0xFFAA00, 0xFFCC00,
            0xCC6600, 0xEE8800, 0xFF9900, 0xFFE0A0,
            0x402C10, 0xEE8800, 0xFFBB22, 0xFFDD33,
            0xDD7711, 0xFF9922, 0xFFAA22, 0xFFF0D0
        }
    },
    {
        .name = "dracula",
        .bg = 0x1E1F29,
        .fg = 0xF8F8F2,
        .ansi = {
            0x21222C, 0xFF5555, 0x50FA7B, 0xF1FA8C,
            0xBD93F9, 0xFF79C6, 0x8BE9FD, 0xF8F8F2,
            0x6272A4, 0xFF6E6E, 0x69FF94, 0xFFFFA5,
            0xD6ACFF, 0xFF92DF, 0xA4FFFF, 0xFFFFFF
        }
    }
};
#define NUM_THEMES (int)(sizeof(built_in_themes) / sizeof(built_in_themes[0]))

static void init_color_table(X11UI *ui, const Theme *th) {
    ui->bg_color = th->bg;
    ui->fg_color = th->fg;

    /* 0-15: Standard ANSI */
    for (int i = 0; i < 16; i++) {
        ui->color_table[i] = th->ansi[i];
    }

    /* 16-231: 6x6x6 color cube */
    static const uint8_t cube_steps[6] = { 0, 95, 135, 175, 215, 255 };
    for (int r = 0; r < 6; r++) {
        for (int g = 0; g < 6; g++) {
            for (int b = 0; b < 6; b++) {
                int idx = 16 + r * 36 + g * 6 + b;
                ui->color_table[idx] = (cube_steps[r] << 16) | (cube_steps[g] << 8) | cube_steps[b];
            }
        }
    }

    /* 232-255: Grayscale ramp */
    for (int i = 0; i < 24; i++) {
        uint8_t v = 8 + i * 10;
        ui->color_table[232 + i] = (v << 16) | (v << 8) | v;
    }

    /* Free prior XftColors if already allocated */
    if (ui->xft_colors_allocated) {
        for (int i = 0; i < 256; i++) {
            XftColorFree(ui->dpy, ui->visual, ui->colormap, &ui->xft_color_table[i]);
        }
        XftColorFree(ui->dpy, ui->visual, ui->colormap, &ui->xft_fg);
        XftColorFree(ui->dpy, ui->visual, ui->colormap, &ui->xft_bg);
        ui->xft_colors_allocated = false;
    }

    for (int i = 0; i < 256; i++) {
        uint32_t rgb = ui->color_table[i];
        XRenderColor rcolor;
        rcolor.red   = ((rgb >> 16) & 0xFF) * 0x101;
        rcolor.green = ((rgb >> 8) & 0xFF) * 0x101;
        rcolor.blue  = (rgb & 0xFF) * 0x101;
        rcolor.alpha = 0xFFFF;
        XftColorAllocValue(ui->dpy, ui->visual, ui->colormap, &rcolor, &ui->xft_color_table[i]);
    }

    XRenderColor rfg = {
        ((th->fg >> 16) & 0xFF) * 0x101,
        ((th->fg >> 8) & 0xFF) * 0x101,
        (th->fg & 0xFF) * 0x101,
        0xFFFF
    };
    XftColorAllocValue(ui->dpy, ui->visual, ui->colormap, &rfg, &ui->xft_fg);

    XRenderColor rbg = {
        ((th->bg >> 16) & 0xFF) * 0x101,
        ((th->bg >> 8) & 0xFF) * 0x101,
        (th->bg & 0xFF) * 0x101,
        0xFFFF
    };
    XftColorAllocValue(ui->dpy, ui->visual, ui->colormap, &rbg, &ui->xft_bg);

    ui->xft_colors_allocated = true;
}

static unsigned long make_pixel(const X11UI *ui, uint32_t rgb, uint8_t alpha) {
    if (ui->has_32bit_visual) {
        return ((unsigned long)alpha << 24) | (rgb & 0x00FFFFFF);
    } else {
        return rgb & 0x00FFFFFF;
    }
}

static bool load_font(X11UI *ui, const char *font_name) {
    for (int i = 0; i < ui->fallback_font_count; i++) {
        if (ui->fallback_fonts[i]) {
            XftFontClose(ui->dpy, ui->fallback_fonts[i]);
            ui->fallback_fonts[i] = NULL;
        }
    }
    ui->fallback_font_count = 0;

    if (ui->xft_font) {
        XftFontClose(ui->dpy, ui->xft_font);
        ui->xft_font = NULL;
    }

    char resolved[128];
    if (font_name && font_name[0] != '\0') {
        if (strcmp(font_name, "6x13") == 0 || strcmp(font_name, "7x13") == 0 ||
            strcmp(font_name, "8x13") == 0 || strcmp(font_name, "fixed") == 0) {
            snprintf(resolved, sizeof(resolved), "monospace:pixelsize=13:antialias=true");
            ui->font_idx = 1;
        } else if (strcmp(font_name, "9x15") == 0) {
            snprintf(resolved, sizeof(resolved), "monospace:pixelsize=15:antialias=true");
            ui->font_idx = 2;
        } else if (strcmp(font_name, "10x20") == 0) {
            snprintf(resolved, sizeof(resolved), "monospace:pixelsize=20:antialias=true");
            ui->font_idx = 4;
        } else {
            strncpy(resolved, font_name, sizeof(resolved) - 1);
            resolved[sizeof(resolved) - 1] = '\0';
        }
    } else {
        int idx = (ui->font_idx >= 0 && ui->font_idx < NUM_FONTS) ? ui->font_idx : 1;
        snprintf(resolved, sizeof(resolved), "%s", default_font_list[idx]);
    }

    XftFont *font = NULL;
    if (resolved[0] == '-') {
        font = XftFontOpenXlfd(ui->dpy, ui->screen, resolved);
    }
    if (!font) {
        font = XftFontOpenName(ui->dpy, ui->screen, resolved);
    }

    /* Verify monospace: check advance of 'M' vs 'i'. If proportional, fall back to clean monospace */
    if (font) {
        XGlyphInfo ext_m, ext_i;
        XftTextExtents8(ui->dpy, font, (const FcChar8 *)"M", 1, &ext_m);
        XftTextExtents8(ui->dpy, font, (const FcChar8 *)"i", 1, &ext_i);
        if (ext_m.xOff != ext_i.xOff) {
            /* Matched a proportional font! Fall back to explicit monospace pattern */
            XftFontClose(ui->dpy, font);
            font = XftFontOpenName(ui->dpy, ui->screen, "monospace:pixelsize=13:antialias=true");
            snprintf(resolved, sizeof(resolved), "monospace:pixelsize=13:antialias=true");
            ui->font_idx = 1;
        }
    }

    if (!font) {
        for (int i = 0; i < NUM_FONTS; i++) {
            font = XftFontOpenName(ui->dpy, ui->screen, default_font_list[i]);
            if (font) {
                strncpy(resolved, default_font_list[i], sizeof(resolved) - 1);
                ui->font_idx = i;
                break;
            }
        }
    }

    if (!font) return false;

    ui->xft_font = font;
    snprintf(ui->current_font_name, sizeof(ui->current_font_name), "%s", resolved);

    double psize = 13.0;
    if (FcPatternGetDouble(font->pattern, FC_PIXEL_SIZE, 0, &psize) != FcResultMatch) {
        psize = (double)(font->ascent + font->descent);
    }
    ui->current_pixelsize = psize;

    XGlyphInfo ext;
    XftTextExtents8(ui->dpy, font, (const FcChar8 *)"M", 1, &ext);
    ui->char_width = (ext.xOff > 0) ? ext.xOff : font->max_advance_width;
    if (ui->char_width <= 0) ui->char_width = 8;
    ui->char_height = font->ascent + font->descent;
    if (ui->char_height <= 0) ui->char_height = 14;
    ui->font_ascent = font->ascent;

    return true;
}

void x11_ui_set_opacity(X11UI *ui, uint8_t opacity) {
    ui->opacity = opacity;
    uint32_t net_opacity = (uint32_t)(((double)opacity / 255.0) * 0xFFFFFFFF);
    XChangeProperty(ui->dpy, ui->win, ui->net_wm_window_opacity, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)&net_opacity, 1);
    XFlush(ui->dpy);
}

void x11_ui_set_title(X11UI *ui, const char *title) {
    if (!title || *title == '\0') return;
    XStoreName(ui->dpy, ui->win, title);
    XChangeProperty(ui->dpy, ui->win, ui->net_wm_name, ui->utf8_string, 8,
                    PropModeReplace, (const unsigned char *)title, strlen(title));
}

bool x11_ui_cycle_font(X11UI *ui, Terminal *term, int direction) {
    int next_idx = ui->font_idx + direction;
    if (next_idx < 0) next_idx = 0;
    if (next_idx >= NUM_FONTS) next_idx = NUM_FONTS - 1;
    if (next_idx == ui->font_idx) return false;

    if (!load_font(ui, default_font_list[next_idx])) return false;
    ui->font_idx = next_idx;

    /* Recalculate terminal size for current window dimensions */
    int new_cols = (ui->win_width - PADDING_LEFT - PADDING_RIGHT) / ui->char_width;
    int new_rows = (ui->win_height - PADDING_TOP - PADDING_BOTTOM) / ui->char_height;
    if (new_cols < 10) new_cols = 10;
    if (new_rows < 4) new_rows = 4;

    term_resize(term, new_cols, new_rows);
    pty_set_size(term->pty_fd, new_cols, new_rows);

    term->dirty = true;
    return true;
}

static bool draw_box_char(Display *dpy, Pixmap pix, GC gc, uint32_t c, int px, int py, int w, int h, unsigned long fg) {
    if (c < 0x2500 || c > 0x25FF) return false;

    XSetForeground(dpy, gc, fg);
    int mx = px + w / 2;
    int my = py + h / 2;
    int th = (w >= 10 || h >= 18) ? 3 : 2; /* heavy thickness */
    int tl = 1;                           /* light thickness */

    switch (c) {
        /* Horizontal lines */
        case 0x2500: /* ─ light horizontal */
            XFillRectangle(dpy, pix, gc, px, my - tl/2, w, tl);
            return true;
        case 0x2501: /* ━ heavy horizontal */
            XFillRectangle(dpy, pix, gc, px, my - th/2, w, th);
            return true;

        /* Vertical lines */
        case 0x2502: /* │ light vertical */
            XFillRectangle(dpy, pix, gc, mx - tl/2, py, tl, h);
            return true;
        case 0x2503: /* ┃ heavy vertical */
            XFillRectangle(dpy, pix, gc, mx - th/2, py, th, h);
            return true;

        /* Top-left corners */
        case 0x250C: /* ┌ light down & right */
            XFillRectangle(dpy, pix, gc, mx - tl/2, my, tl, h - (my - py));
            XFillRectangle(dpy, pix, gc, mx, my - tl/2, w - (mx - px), tl);
            return true;
        case 0x250D: /* ┍ down light, right heavy */
            XFillRectangle(dpy, pix, gc, mx - tl/2, my, tl, h - (my - py));
            XFillRectangle(dpy, pix, gc, mx, my - th/2, w - (mx - px), th);
            return true;
        case 0x250E: /* ┎ down heavy, right light */
            XFillRectangle(dpy, pix, gc, mx - th/2, my, th, h - (my - py));
            XFillRectangle(dpy, pix, gc, mx, my - tl/2, w - (mx - px), tl);
            return true;
        case 0x250F: /* ┏ heavy down & right */
            XFillRectangle(dpy, pix, gc, mx - th/2, my - th/2, th, h - (my - py) + th/2);
            XFillRectangle(dpy, pix, gc, mx - th/2, my - th/2, w - (mx - px) + th/2, th);
            return true;

        /* Top-right corners */
        case 0x2510: /* ┐ light down & left */
            XFillRectangle(dpy, pix, gc, mx - tl/2, my, tl, h - (my - py));
            XFillRectangle(dpy, pix, gc, px, my - tl/2, mx - px + tl, tl);
            return true;
        case 0x2511: /* ┑ down light, left heavy */
            XFillRectangle(dpy, pix, gc, mx - tl/2, my, tl, h - (my - py));
            XFillRectangle(dpy, pix, gc, px, my - th/2, mx - px + th/2, th);
            return true;
        case 0x2512: /* ┒ down heavy, left light */
            XFillRectangle(dpy, pix, gc, mx - th/2, my, th, h - (my - py));
            XFillRectangle(dpy, pix, gc, px, my - tl/2, mx - px + tl, tl);
            return true;
        case 0x2513: /* ┓ heavy down & left */
            XFillRectangle(dpy, pix, gc, mx - th/2, my - th/2, th, h - (my - py) + th/2);
            XFillRectangle(dpy, pix, gc, px, my - th/2, mx - px + th/2, th);
            return true;

        /* Bottom-left corners */
        case 0x2514: /* └ light up & right */
            XFillRectangle(dpy, pix, gc, mx - tl/2, py, tl, my - py + tl);
            XFillRectangle(dpy, pix, gc, mx, my - tl/2, w - (mx - px), tl);
            return true;
        case 0x2515: /* ┕ up light, right heavy */
            XFillRectangle(dpy, pix, gc, mx - tl/2, py, tl, my - py + tl);
            XFillRectangle(dpy, pix, gc, mx, my - th/2, w - (mx - px), th);
            return true;
        case 0x2516: /* ┖ up heavy, right light */
            XFillRectangle(dpy, pix, gc, mx - th/2, py, th, my - py + th/2);
            XFillRectangle(dpy, pix, gc, mx, my - tl/2, w - (mx - px), tl);
            return true;
        case 0x2517: /* ┗ heavy up & right */
            XFillRectangle(dpy, pix, gc, mx - th/2, py, th, my - py + th/2);
            XFillRectangle(dpy, pix, gc, mx - th/2, my - th/2, w - (mx - px) + th/2, th);
            return true;

        /* Bottom-right corners */
        case 0x2518: /* ┘ light up & left */
            XFillRectangle(dpy, pix, gc, mx - tl/2, py, tl, my - py + tl);
            XFillRectangle(dpy, pix, gc, px, my - tl/2, mx - px + tl, tl);
            return true;
        case 0x2519: /* ┙ up light, left heavy */
            XFillRectangle(dpy, pix, gc, mx - tl/2, py, tl, my - py + tl);
            XFillRectangle(dpy, pix, gc, px, my - th/2, mx - px + th/2, th);
            return true;
        case 0x251A: /* ┚ up heavy, left light */
            XFillRectangle(dpy, pix, gc, mx - th/2, py, th, my - py + th/2);
            XFillRectangle(dpy, pix, gc, px, my - tl/2, mx - px + tl, tl);
            return true;
        case 0x251B: /* ┛ heavy up & left */
            XFillRectangle(dpy, pix, gc, mx - th/2, py, th, my - py + th/2);
            XFillRectangle(dpy, pix, gc, px, my - th/2, mx - px + th/2, th);
            return true;

        /* Tees */
        case 0x251C: /* ├ light vertical and right */
            XFillRectangle(dpy, pix, gc, mx - tl/2, py, tl, h);
            XFillRectangle(dpy, pix, gc, mx, my - tl/2, w - (mx - px), tl);
            return true;
        case 0x2523: /* ┣ heavy vertical and right */
            XFillRectangle(dpy, pix, gc, mx - th/2, py, th, h);
            XFillRectangle(dpy, pix, gc, mx, my - th/2, w - (mx - px), th);
            return true;
        case 0x2524: /* ┤ light vertical and left */
            XFillRectangle(dpy, pix, gc, mx - tl/2, py, tl, h);
            XFillRectangle(dpy, pix, gc, px, my - tl/2, mx - px + tl, tl);
            return true;
        case 0x252B: /* ┫ heavy vertical and left */
            XFillRectangle(dpy, pix, gc, mx - th/2, py, th, h);
            XFillRectangle(dpy, pix, gc, px, my - th/2, mx - px + th/2, th);
            return true;
        case 0x252C: /* ┬ light horizontal and down */
            XFillRectangle(dpy, pix, gc, px, my - tl/2, w, tl);
            XFillRectangle(dpy, pix, gc, mx - tl/2, my, tl, h - (my - py));
            return true;
        case 0x2533: /* ┳ heavy horizontal and down */
            XFillRectangle(dpy, pix, gc, px, my - th/2, w, th);
            XFillRectangle(dpy, pix, gc, mx - th/2, my, th, h - (my - py));
            return true;
        case 0x2534: /* ┴ light horizontal and up */
            XFillRectangle(dpy, pix, gc, px, my - tl/2, w, tl);
            XFillRectangle(dpy, pix, gc, mx - tl/2, py, tl, my - py + tl);
            return true;
        case 0x253B: /* ┻ heavy horizontal and up */
            XFillRectangle(dpy, pix, gc, px, my - th/2, w, th);
            XFillRectangle(dpy, pix, gc, mx - th/2, py, th, my - py + th/2);
            return true;
        case 0x253C: /* ┼ light cross */
            XFillRectangle(dpy, pix, gc, px, my - tl/2, w, tl);
            XFillRectangle(dpy, pix, gc, mx - tl/2, py, tl, h);
            return true;
        case 0x254B: /* ╋ heavy cross */
            XFillRectangle(dpy, pix, gc, px, my - th/2, w, th);
            XFillRectangle(dpy, pix, gc, mx - th/2, py, th, h);
            return true;

        /* Half-lines & hybrids */
        case 0x2574: /* ╴ light left */
            XFillRectangle(dpy, pix, gc, px, my - tl/2, mx - px + tl, tl);
            return true;
        case 0x2575: /* ╵ light up */
            XFillRectangle(dpy, pix, gc, mx - tl/2, py, tl, my - py + tl);
            return true;
        case 0x2576: /* ╶ light right */
            XFillRectangle(dpy, pix, gc, mx, my - tl/2, w - (mx - px), tl);
            return true;
        case 0x2577: /* ╷ light down */
            XFillRectangle(dpy, pix, gc, mx - tl/2, my, tl, h - (my - py));
            return true;
        case 0x2578: /* ╸ heavy left */
            XFillRectangle(dpy, pix, gc, px, my - th/2, mx - px + th/2, th);
            return true;
        case 0x2579: /* ╹ heavy up */
            XFillRectangle(dpy, pix, gc, mx - th/2, py, th, my - py + th/2);
            return true;
        case 0x257A: /* ╺ heavy right */
            XFillRectangle(dpy, pix, gc, mx - th/2, my - th/2, w - (mx - px) + th/2, th);
            return true;
        case 0x257B: /* ╻ heavy down */
            XFillRectangle(dpy, pix, gc, mx - th/2, my, th, h - (my - py));
            return true;
        case 0x257C: /* ╾ light left and heavy right */
            XFillRectangle(dpy, pix, gc, px, my - tl/2, mx - px, tl);
            XFillRectangle(dpy, pix, gc, mx - th/2, my - th/2, w - (mx - px) + th/2, th);
            return true;
        case 0x257D: /* ╽ light up and heavy down */
            XFillRectangle(dpy, pix, gc, mx - tl/2, py, tl, my - py);
            XFillRectangle(dpy, pix, gc, mx - th/2, my - th/2, th, h - (my - py) + th/2);
            return true;
        case 0x257E: /* ╼ heavy left and light right */
            XFillRectangle(dpy, pix, gc, px, my - th/2, mx - px + th/2, th);
            XFillRectangle(dpy, pix, gc, mx, my - tl/2, w - (mx - px), tl);
            return true;
        case 0x257F: /* ╿ heavy up and light down */
            XFillRectangle(dpy, pix, gc, mx - th/2, py, th, my - py + th/2);
            XFillRectangle(dpy, pix, gc, mx - tl/2, my, tl, h - (my - py));
            return true;

        /* Block elements */
        case 0x2588: /* █ full block */
            XFillRectangle(dpy, pix, gc, px, py, w, h);
            return true;
        case 0x2580: /* ▀ top half */
            XFillRectangle(dpy, pix, gc, px, py, w, h/2);
            return true;
        case 0x2584: /* ▄ bottom half */
            XFillRectangle(dpy, pix, gc, px, py + h/2, w, h - h/2);
            return true;
        case 0x258C: /* ▌ left half */
            XFillRectangle(dpy, pix, gc, px, py, w/2, h);
            return true;
        case 0x2590: /* ▐ right half */
            XFillRectangle(dpy, pix, gc, px + w/2, py, w - w/2, h);
            return true;
        case 0x2591: /* ░ light shade */
        case 0x2592: /* ▒ medium shade */
        case 0x2593: /* ▓ dark shade */
            {
                int step = (c == 0x2591) ? 3 : (c == 0x2592 ? 2 : 1);
                for (int sy = 0; sy < h; sy += step) {
                    for (int sx = (sy % 2); sx < w; sx += step) {
                        XDrawPoint(dpy, pix, gc, px + sx, py + sy);
                    }
                }
            }
            return true;

        /* Circles and geometric symbols */
        case 0x25C9: /* ◉ fisheye */
            {
                int r = (w < h ? w : h) / 2 - 1;
                if (r < 2) r = 2;
                XDrawArc(dpy, pix, gc, mx - r, my - r, r * 2, r * 2, 0, 360 * 64);
                int ir = r / 2;
                if (ir < 1) ir = 1;
                XFillArc(dpy, pix, gc, mx - ir, my - ir, ir * 2, ir * 2, 0, 360 * 64);
            }
            return true;
        case 0x25CF: /* ● black circle */
            {
                int r = (w < h ? w : h) / 2 - 1;
                if (r < 2) r = 2;
                XFillArc(dpy, pix, gc, mx - r, my - r, r * 2, r * 2, 0, 360 * 64);
            }
            return true;
        case 0x25CB: /* ○ white circle */
            {
                int r = (w < h ? w : h) / 2 - 1;
                if (r < 2) r = 2;
                XDrawArc(dpy, pix, gc, mx - r, my - r, r * 2, r * 2, 0, 360 * 64);
            }
            return true;
        case 0x25A0: /* ■ black square */
            {
                int s = (w < h ? w : h) - 2;
                XFillRectangle(dpy, pix, gc, mx - s/2, my - s/2, s, s);
            }
            return true;
        case 0x25A1: /* □ white square */
            {
                int s = (w < h ? w : h) - 2;
                XDrawRectangle(dpy, pix, gc, mx - s/2, my - s/2, s, s);
            }
            return true;
        case 0x25C6: /* ◆ diamond */
            {
                XPoint pts[4] = {
                    { mx, py + 1 },
                    { px + w - 1, my },
                    { mx, py + h - 1 },
                    { px + 1, my }
                };
                XFillPolygon(dpy, pix, gc, pts, 4, Convex, CoordModeOrigin);
            }
            return true;
        default:
            return false;
    }
}

X11UI *x11_ui_create(int cols, int rows, const char *font_name, uint8_t opacity,
                     const char *theme_name, const char *title, bool bevel) {
    X11UI *ui = calloc(1, sizeof(X11UI));
    if (!ui) return NULL;

    ui->dpy = XOpenDisplay(NULL);
    if (!ui->dpy) {
        fprintf(stderr, "Error: Unable to open X display %s\n", XDisplayName(NULL));
        free(ui);
        return NULL;
    }

    ui->screen = DefaultScreen(ui->dpy);
    ui->opacity = opacity;
    ui->focused = true;
    ui->cursor_state = true;
    ui->draw_bevel = bevel;

    /* Eterm bevel colors */
    ui->bevel_light = 0x66FFFFFF; /* subtle translucent highlight */
    ui->bevel_dark  = 0x66000000; /* subtle translucent shadow */

    /* Find 32-bit visual for TrueColor alpha translucency */
    XVisualInfo vinfo;
    if (XMatchVisualInfo(ui->dpy, ui->screen, 32, TrueColor, &vinfo)) {
        ui->has_32bit_visual = true;
        ui->visual = vinfo.visual;
        ui->depth = 32;
        ui->colormap = XCreateColormap(ui->dpy, RootWindow(ui->dpy, ui->screen), ui->visual, AllocNone);
    } else {
        ui->has_32bit_visual = false;
        ui->visual = DefaultVisual(ui->dpy, ui->screen);
        ui->depth = DefaultDepth(ui->dpy, ui->screen);
        ui->colormap = DefaultColormap(ui->dpy, ui->screen);
    }

    /* Theme selection */
    const Theme *sel_theme = &built_in_themes[0];
    if (theme_name) {
        for (int i = 0; i < NUM_THEMES; i++) {
            if (strcasecmp(theme_name, built_in_themes[i].name) == 0) {
                sel_theme = &built_in_themes[i];
                break;
            }
        }
    }
    init_color_table(ui, sel_theme);

    /* Load font */
    ui->font_idx = 1; /* default to monospace:size=13 */
    if (!load_font(ui, font_name ? font_name : "monospace:pixelsize=13:antialias=true")) {
        fprintf(stderr, "Error: Failed to load any terminal font\n");
        XCloseDisplay(ui->dpy);
        free(ui);
        return NULL;
    }

    ui->win_width = PADDING_LEFT + cols * ui->char_width + PADDING_RIGHT;
    ui->win_height = PADDING_TOP + rows * ui->char_height + PADDING_BOTTOM;

    XSetWindowAttributes swa;
    swa.colormap = ui->colormap;
    swa.background_pixel = 0;
    swa.border_pixel = 0;
    swa.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask |
                     ButtonPressMask | ButtonReleaseMask | ButtonMotionMask |
                     StructureNotifyMask | FocusChangeMask | PropertyChangeMask;

    unsigned long mask = CWColormap | CWBorderPixel | CWBackPixel | CWEventMask;

    ui->win = XCreateWindow(ui->dpy, RootWindow(ui->dpy, ui->screen),
                            100, 100, ui->win_width, ui->win_height, 0,
                            ui->depth, InputOutput, ui->visual, mask, &swa);

    /* Double buffering pixmap */
    ui->pixmap = XCreatePixmap(ui->dpy, ui->win, ui->win_width, ui->win_height, ui->depth);

    /* XftDraw on pixmap */
    ui->xft_draw = XftDrawCreate(ui->dpy, ui->pixmap, ui->visual, ui->colormap);

    /* Graphics Context */
    XGCValues gcv;
    memset(&gcv, 0, sizeof(gcv));
    ui->gc = XCreateGC(ui->dpy, ui->pixmap, 0, &gcv);

    /* Intern Atoms */
    ui->wm_delete_window = XInternAtom(ui->dpy, "WM_DELETE_WINDOW", False);
    ui->net_wm_name = XInternAtom(ui->dpy, "_NET_WM_NAME", False);
    ui->net_wm_window_opacity = XInternAtom(ui->dpy, "_NET_WM_WINDOW_OPACITY", False);
    ui->utf8_string = XInternAtom(ui->dpy, "UTF8_STRING", False);
    ui->targets = XInternAtom(ui->dpy, "TARGETS", False);
    ui->clipboard = XInternAtom(ui->dpy, "CLIPBOARD", False);
    ui->xsel_data = XInternAtom(ui->dpy, "XSEL_DATA", False);

    XSetWMProtocols(ui->dpy, ui->win, &ui->wm_delete_window, 1);

    /* Set WM_CLASS */
    XClassHint *class_hint = XAllocClassHint();
    if (class_hint) {
        class_hint->res_name = (char *)"eterm";
        class_hint->res_class = (char *)"Eterm";
        XSetClassHint(ui->dpy, ui->win, class_hint);
        XFree(class_hint);
    }

    /* Set _NET_WM_PID */
    Atom net_wm_pid = XInternAtom(ui->dpy, "_NET_WM_PID", False);
    unsigned long pid_val = (unsigned long)getpid();
    XChangeProperty(ui->dpy, ui->win, net_wm_pid, XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)&pid_val, 1);

    /* Set window title and opacity */
    x11_ui_set_title(ui, title ? title : "Eterm");
    x11_ui_set_opacity(ui, ui->opacity);

    /* Set size hints */
    XSizeHints *hints = XAllocSizeHints();
    if (hints) {
        hints->flags = PResizeInc | PBaseSize | PMinSize;
        hints->width_inc = ui->char_width;
        hints->height_inc = ui->char_height;
        hints->base_width = PADDING_LEFT + PADDING_RIGHT;
        hints->base_height = PADDING_TOP + PADDING_BOTTOM;
        hints->min_width = hints->base_width + ui->char_width * 10;
        hints->min_height = hints->base_height + ui->char_height * 4;
        XSetWMNormalHints(ui->dpy, ui->win, hints);
        XFree(hints);
    }

    selection_init(&ui->selection);

    XMapWindow(ui->dpy, ui->win);
    XFlush(ui->dpy);

    return ui;
}

void x11_ui_free(X11UI *ui) {
    if (!ui) return;
    selection_free(&ui->selection);

    for (int i = 0; i < ui->fallback_font_count; i++) {
        if (ui->fallback_fonts[i]) {
            XftFontClose(ui->dpy, ui->fallback_fonts[i]);
            ui->fallback_fonts[i] = NULL;
        }
    }
    ui->fallback_font_count = 0;

    if (ui->xft_colors_allocated) {
        for (int i = 0; i < 256; i++) {
            XftColorFree(ui->dpy, ui->visual, ui->colormap, &ui->xft_color_table[i]);
        }
        XftColorFree(ui->dpy, ui->visual, ui->colormap, &ui->xft_fg);
        XftColorFree(ui->dpy, ui->visual, ui->colormap, &ui->xft_bg);
        ui->xft_colors_allocated = false;
    }

    if (ui->xft_draw) XftDrawDestroy(ui->xft_draw);
    if (ui->xft_font) XftFontClose(ui->dpy, ui->xft_font);
    if (ui->gc) XFreeGC(ui->dpy, ui->gc);
    if (ui->pixmap) XFreePixmap(ui->dpy, ui->pixmap);
    if (ui->win) XDestroyWindow(ui->dpy, ui->win);
    if (ui->has_32bit_visual && ui->colormap) XFreeColormap(ui->dpy, ui->colormap);
    if (ui->dpy) XCloseDisplay(ui->dpy);
    free(ui);
}

static XftFont *x11_ui_get_font_for_char(X11UI *ui, FcChar32 c) {
    if (c < 128 || XftCharExists(ui->dpy, ui->xft_font, c)) {
        return ui->xft_font;
    }
    for (int i = 0; i < ui->fallback_font_count; i++) {
        if (XftCharExists(ui->dpy, ui->fallback_fonts[i], c)) {
            return ui->fallback_fonts[i];
        }
    }
    if (ui->fallback_font_count < 32) {
        FcPattern *pat = FcPatternCreate();
        FcPatternAddString(pat, FC_FAMILY, (const FcChar8 *)"monospace");
        FcPatternAddDouble(pat, FC_PIXEL_SIZE, ui->current_pixelsize);
        FcCharSet *cs = FcCharSetCreate();
        FcCharSetAddChar(cs, c);
        FcPatternAddCharSet(pat, FC_CHARSET, cs);
        FcCharSetDestroy(cs);

        FcConfigSubstitute(NULL, pat, FcMatchPattern);
        FcDefaultSubstitute(pat);

        FcResult res;
        FcPattern *match = FcFontMatch(NULL, pat, &res);
        FcPatternDestroy(pat);

        if (match) {
            XftFont *fb = XftFontOpenPattern(ui->dpy, match);
            if (fb) {
                ui->fallback_fonts[ui->fallback_font_count++] = fb;
                return fb;
            }
        }
    }
    return ui->xft_font;
}

void x11_ui_render(X11UI *ui, Terminal *term) {
    if (!ui || !term) return;

    /* 1. Clear window with translucent background color */
    unsigned long bg_pix = make_pixel(ui, ui->bg_color, ui->opacity);
    XSetForeground(ui->dpy, ui->gc, bg_pix);
    XFillRectangle(ui->dpy, ui->pixmap, ui->gc, 0, 0, ui->win_width, ui->win_height);

    /* 2. Render terminal text and cells */
    char utf8_buf[8];

    for (int y = 0; y < term->rows; y++) {
        int x = 0;
        while (x < term->cols) {
            Cell cell = term_get_cell(term, x, y);
            bool is_selected = selection_is_cell_selected(&ui->selection, x, y);

            /* Gather consecutive cells with identical background & selection for efficient rectangle fill */
            int run_len = 0;
            while (x + run_len < term->cols) {
                Cell next = term_get_cell(term, x + run_len, y);
                bool next_sel = selection_is_cell_selected(&ui->selection, x + run_len, y);
                if (next.bg != cell.bg || (next.attr & (ATTR_REVERSE | ATTR_TRUECOLOR_BG)) !=
                    (cell.attr & (ATTR_REVERSE | ATTR_TRUECOLOR_BG)) || next_sel != is_selected) {
                    break;
                }
                run_len++;
            }
            if (run_len == 0) run_len = 1;

            int px = PADDING_LEFT + x * ui->char_width;
            int py = PADDING_TOP + y * ui->char_height;
            int pw = run_len * ui->char_width;
            int ph = ui->char_height;

            uint32_t bg_rgb;
            uint8_t cell_alpha = ui->opacity;

            /* Resolve background color */
            if (is_selected) {
                bg_rgb = 0x20558A; /* electric blue selection */
                cell_alpha = 0xE0;
            } else if (cell.attr & ATTR_REVERSE) {
                if (cell.attr & ATTR_TRUECOLOR_FG) bg_rgb = cell.fg;
                else bg_rgb = ui->color_table[cell.fg & 0xFF];
                cell_alpha = 0xFF;
            } else if (cell.attr & ATTR_TRUECOLOR_BG) {
                bg_rgb = cell.bg;
                cell_alpha = 0xFF;
            } else if (cell.bg == term->def_bg) {
                bg_rgb = ui->bg_color;
                cell_alpha = ui->opacity;
            } else {
                bg_rgb = ui->color_table[cell.bg & 0xFF];
                cell_alpha = 0xF0;
            }

            if (cell_alpha != ui->opacity || bg_rgb != ui->bg_color || is_selected) {
                XSetForeground(ui->dpy, ui->gc, make_pixel(ui, bg_rgb, cell_alpha));
                XFillRectangle(ui->dpy, ui->pixmap, ui->gc, px, py, pw, ph);
            }

            /* Draw individual cells in this run */
            for (int r = 0; r < run_len; r++) {
                int col_idx = x + r;
                Cell c = term_get_cell(term, col_idx, y);
                if (c.attr & ATTR_WIDE_CONT) continue; /* wide continuation, already rendered */

                int cell_px = PADDING_LEFT + col_idx * ui->char_width;
                int cell_pw = (c.attr & ATTR_WIDE_CHAR) ? (ui->char_width * 2) : ui->char_width;

                uint32_t fg_rgb;
                if (is_selected) {
                    fg_rgb = 0xFFFFFF;
                } else if (c.attr & ATTR_REVERSE) {
                    if (c.attr & ATTR_TRUECOLOR_BG) fg_rgb = c.bg;
                    else if (c.bg == term->def_bg) fg_rgb = ui->bg_color;
                    else fg_rgb = ui->color_table[c.bg & 0xFF];
                } else if (c.attr & ATTR_TRUECOLOR_FG) {
                    fg_rgb = c.fg;
                } else {
                    fg_rgb = ui->color_table[c.fg & 0xFF];
                }

                if (c.attr & ATTR_DIM) {
                    fg_rgb = ((fg_rgb & 0xFEFEFE) >> 1);
                }

                if (!(c.attr & ATTR_INVISIBLE)) {
                    /* Check block elements (0x2580-0x2593) or font fallback for box drawing */
                    bool use_geom = (c.c >= 0x2580 && c.c <= 0x2593) ||
                                    !XftCharExists(ui->dpy, ui->xft_font, c.c);
                    if (use_geom && draw_box_char(ui->dpy, ui->pixmap, ui->gc, c.c,
                                                  cell_px, py, cell_pw, ph, make_pixel(ui, fg_rgb, 0xFF))) {
                        continue;
                    }

                    /* General Unicode & ASCII glyph drawing via Xft */
                    uint32_t code = c.c;
                    if (code == 0 || code == ' ') continue;

                    int ulen = 0;
                    if (code < 0x80) {
                        utf8_buf[ulen++] = (char)code;
                    } else if (code < 0x800) {
                        utf8_buf[ulen++] = (char)(0xC0 | (code >> 6));
                        utf8_buf[ulen++] = (char)(0x80 | (code & 0x3F));
                    } else if (code < 0x10000) {
                        utf8_buf[ulen++] = (char)(0xE0 | (code >> 12));
                        utf8_buf[ulen++] = (char)(0x80 | ((code >> 6) & 0x3F));
                        utf8_buf[ulen++] = (char)(0x80 | (code & 0x3F));
                    } else {
                        utf8_buf[ulen++] = (char)(0xF0 | (code >> 18));
                        utf8_buf[ulen++] = (char)(0x80 | ((code >> 12) & 0x3F));
                        utf8_buf[ulen++] = (char)(0x80 | ((code >> 6) & 0x3F));
                        utf8_buf[ulen++] = (char)(0x80 | (code & 0x3F));
                    }
                    utf8_buf[ulen] = '\0';

                    XftColor *xft_col = NULL;
                    XftColor tmp_col;
                    bool need_free = false;

                    if (!is_selected && !(c.attr & (ATTR_TRUECOLOR_FG | ATTR_REVERSE)) && c.fg < 256) {
                        xft_col = &ui->xft_color_table[c.fg & 0xFF];
                    } else {
                        XRenderColor rcolor = {
                            ((fg_rgb >> 16) & 0xFF) * 0x101,
                            ((fg_rgb >> 8) & 0xFF) * 0x101,
                            (fg_rgb & 0xFF) * 0x101,
                            0xFFFF
                        };
                        XftColorAllocValue(ui->dpy, ui->visual, ui->colormap, &rcolor, &tmp_col);
                        xft_col = &tmp_col;
                        need_free = true;
                    }

                    XftFont *char_font = x11_ui_get_font_for_char(ui, code);

                    XftDrawStringUtf8(ui->xft_draw, xft_col, char_font,
                                      cell_px, py + ui->font_ascent,
                                      (const FcChar8 *)utf8_buf, ulen);

                    if (c.attr & ATTR_BOLD) {
                        XftDrawStringUtf8(ui->xft_draw, xft_col, char_font,
                                          cell_px + 1, py + ui->font_ascent,
                                          (const FcChar8 *)utf8_buf, ulen);
                    }

                    if (c.attr & ATTR_UNDERLINE) {
                        XSetForeground(ui->dpy, ui->gc, make_pixel(ui, fg_rgb, 0xFF));
                        XDrawLine(ui->dpy, ui->pixmap, ui->gc,
                                  cell_px, py + ui->font_ascent + 1,
                                  cell_px + cell_pw - 1, py + ui->font_ascent + 1);
                    }

                    if (need_free) {
                        XftColorFree(ui->dpy, ui->visual, ui->colormap, &tmp_col);
                    }
                }
            }

            x += run_len;
        }
    }

    /* 3. Render Cursor */
    if (term->cursor_visible && term->scroll_offset == 0 &&
        term->cursor_x >= 0 && term->cursor_x < term->cols &&
        term->cursor_y >= 0 && term->cursor_y < term->rows) {

        Cell cur_cell = term_get_cell(term, term->cursor_x, term->cursor_y);
        int cur_w = (cur_cell.attr & ATTR_WIDE_CHAR) ? (ui->char_width * 2) : ui->char_width;
        int cx = PADDING_LEFT + term->cursor_x * ui->char_width;
        int cy = PADDING_TOP + term->cursor_y * ui->char_height;

        if (ui->focused) {
            if (ui->cursor_state) {
                /* Solid block cursor */
                XSetForeground(ui->dpy, ui->gc, make_pixel(ui, ui->fg_color, 0xEE));
                XFillRectangle(ui->dpy, ui->pixmap, ui->gc, cx, cy, cur_w, ui->char_height);

                /* Redraw cursor character in background color */
                if (cur_cell.c >= 32 && cur_cell.c != 0) {
                    bool cur_geom = (cur_cell.c >= 0x2580 && cur_cell.c <= 0x2593) ||
                                    !XftCharExists(ui->dpy, ui->xft_font, cur_cell.c);
                    if (cur_geom && draw_box_char(ui->dpy, ui->pixmap, ui->gc, cur_cell.c, cx, cy, cur_w, ui->char_height, make_pixel(ui, ui->bg_color, 0xFF))) {
                        /* Drawn by geometry engine */
                    } else {
                        char cbuf[8];
                        int clen = 0;
                        uint32_t code = cur_cell.c;
                        if (code < 0x80) cbuf[clen++] = (char)code;
                        else if (code < 0x800) {
                            cbuf[clen++] = (char)(0xC0 | (code >> 6));
                            cbuf[clen++] = (char)(0x80 | (code & 0x3F));
                        } else if (code < 0x10000) {
                            cbuf[clen++] = (char)(0xE0 | (code >> 12));
                            cbuf[clen++] = (char)(0x80 | ((code >> 6) & 0x3F));
                            cbuf[clen++] = (char)(0x80 | (code & 0x3F));
                        } else {
                            cbuf[clen++] = (char)(0xF0 | (code >> 18));
                            cbuf[clen++] = (char)(0x80 | ((code >> 12) & 0x3F));
                            cbuf[clen++] = (char)(0x80 | ((code >> 6) & 0x3F));
                            cbuf[clen++] = (char)(0x80 | (code & 0x3F));
                        }
                        cbuf[clen] = '\0';
                        XftFont *char_font = x11_ui_get_font_for_char(ui, code);
                        XftDrawStringUtf8(ui->xft_draw, &ui->xft_bg, char_font,
                                          cx, cy + ui->font_ascent, (const FcChar8 *)cbuf, clen);
                    }
                }
            }
        } else {
            /* Unfocused: hollow rectangular outline */
            XSetForeground(ui->dpy, ui->gc, make_pixel(ui, ui->fg_color, 0xBB));
            XDrawRectangle(ui->dpy, ui->pixmap, ui->gc, cx, cy, cur_w - 1, ui->char_height - 1);
        }
    }

    /* 4. Eterm Beveled Border Frame */
    if (ui->draw_bevel) {
        int x2 = ui->win_width - 1;
        int y2 = ui->win_height - 1;

        /* Light highlight border (top and left) */
        XSetForeground(ui->dpy, ui->gc, make_pixel(ui, 0xFFFFFF, 0x55));
        XDrawLine(ui->dpy, ui->pixmap, ui->gc, 0, 0, x2, 0);
        XDrawLine(ui->dpy, ui->pixmap, ui->gc, 0, 0, 0, y2);

        /* Dark shadow border (bottom and right) */
        XSetForeground(ui->dpy, ui->gc, make_pixel(ui, 0x000000, 0x88));
        XDrawLine(ui->dpy, ui->pixmap, ui->gc, 0, y2, x2, y2);
        XDrawLine(ui->dpy, ui->pixmap, ui->gc, x2, 0, x2, y2);
    }

    /* 5. Flip double buffer pixmap onto window */
    XCopyArea(ui->dpy, ui->pixmap, ui->win, ui->gc, 0, 0, ui->win_width, ui->win_height, 0, 0);
    XFlush(ui->dpy);
    term->dirty = false;
}

void x11_ui_handle_event(X11UI *ui, Terminal *term, XEvent *ev) {
    if (!ui || !term || !ev) return;

    switch (ev->type) {
        case Expose:
            if (ev->xexpose.count == 0) {
                x11_ui_render(ui, term);
            }
            break;

        case FocusIn:
            ui->focused = true;
            ui->cursor_state = true;
            term->dirty = true;
            break;

        case FocusOut:
            ui->focused = false;
            term->dirty = true;
            break;

        case ConfigureNotify:
            if (ev->xconfigure.width != ui->win_width || ev->xconfigure.height != ui->win_height) {
                ui->win_width = ev->xconfigure.width;
                ui->win_height = ev->xconfigure.height;

                if (ui->xft_draw) {
                    XftDrawDestroy(ui->xft_draw);
                    ui->xft_draw = NULL;
                }
                if (ui->pixmap) {
                    XFreePixmap(ui->dpy, ui->pixmap);
                }
                ui->pixmap = XCreatePixmap(ui->dpy, ui->win, ui->win_width, ui->win_height, ui->depth);
                ui->xft_draw = XftDrawCreate(ui->dpy, ui->pixmap, ui->visual, ui->colormap);

                int new_cols = (ui->win_width - PADDING_LEFT - PADDING_RIGHT) / ui->char_width;
                int new_rows = (ui->win_height - PADDING_TOP - PADDING_BOTTOM) / ui->char_height;
                if (new_cols < 10) new_cols = 10;
                if (new_rows < 4) new_rows = 4;

                term_resize(term, new_cols, new_rows);
                pty_set_size(term->pty_fd, new_cols, new_rows);
                term->dirty = true;
            }
            break;

        case ButtonPress:
            {
                int col = (ev->xbutton.x - PADDING_LEFT) / ui->char_width;
                int row = (ev->xbutton.y - PADDING_TOP) / ui->char_height;

                if (ev->xbutton.button == Button1) {
                    /* Left Click: Start Selection (character/word/line) */
                    selection_start(&ui->selection, term, col, row, ev->xbutton.time);
                } else if (ev->xbutton.button == Button2) {
                    /* Middle Click: Paste Primary Selection */
                    selection_request_paste(&ui->selection, term, ui->dpy, ui->win,
                                            XA_PRIMARY, ui->utf8_string, ui->xsel_data, ev->xbutton.time);
                } else if (ev->xbutton.button == Button3) {
                    /* Right Click: Extend Selection */
                    selection_extend(&ui->selection, term, ui->dpy, ui->win, col, row, ev->xbutton.time);
                } else if (ev->xbutton.button == Button4) {
                    /* Scroll Wheel Up */
                    term_scroll_viewport(term, 3);
                } else if (ev->xbutton.button == Button5) {
                    /* Scroll Wheel Down */
                    term_scroll_viewport(term, -3);
                }
            }
            break;

        case MotionNotify:
            {
                if (ev->xmotion.state & Button1Mask) {
                    int col = (ev->xmotion.x - PADDING_LEFT) / ui->char_width;
                    int row = (ev->xmotion.y - PADDING_TOP) / ui->char_height;
                    selection_drag(&ui->selection, term, col, row);
                }
            }
            break;

        case ButtonRelease:
            {
                if (ev->xbutton.button == Button1 || ev->xbutton.button == Button3) {
                    selection_end(&ui->selection, term, ui->dpy, ui->win, ev->xbutton.time);
                }
            }
            break;

        case SelectionRequest:
            selection_handle_request(&ui->selection, ui->dpy, &ev->xselectionrequest,
                                     ui->utf8_string, ui->targets);
            break;

        case SelectionNotify:
            selection_handle_notify(term, ui->dpy, &ev->xselection, ui->xsel_data, ui->utf8_string);
            break;

        case SelectionClear:
            selection_clear(&ui->selection, term);
            break;

        case KeyPress:
            {
                KeySym ksym;
                char buf[64];
                int len = XLookupString(&ev->xkey, buf, sizeof(buf) - 1, &ksym, NULL);
                bool ctrl = (ev->xkey.state & ControlMask) != 0;
                bool shift = (ev->xkey.state & ShiftMask) != 0;
                bool alt = (ev->xkey.state & Mod1Mask) != 0;

                /* Eterm Interactive Shortcuts */
                if (ctrl && shift) {
                    if (ksym == XK_Up) {
                        /* Increase opacity (+5%) */
                        uint8_t new_op = (ui->opacity > 242) ? 255 : (ui->opacity + 13);
                        x11_ui_set_opacity(ui, new_op);
                        term->dirty = true;
                        return;
                    } else if (ksym == XK_Down) {
                        /* Decrease opacity (-5%) */
                        uint8_t new_op = (ui->opacity < 30) ? 17 : (ui->opacity - 13);
                        x11_ui_set_opacity(ui, new_op);
                        term->dirty = true;
                        return;
                    } else if (ksym == XK_plus || ksym == XK_equal || ksym == XK_KP_Add) {
                        /* Increase font size */
                        x11_ui_cycle_font(ui, term, 1);
                        return;
                    } else if (ksym == XK_minus || ksym == XK_underscore || ksym == XK_KP_Subtract) {
                        /* Decrease font size */
                        x11_ui_cycle_font(ui, term, -1);
                        return;
                    }
                }

                if (shift) {
                    if (ksym == XK_Prior) {
                        term_scroll_viewport(term, term->rows / 2);
                        return;
                    } else if (ksym == XK_Next) {
                        term_scroll_viewport(term, -(term->rows / 2));
                        return;
                    } else if (ksym == XK_Insert || ksym == XK_KP_Insert) {
                        /* Shift+Insert: Paste primary selection */
                        selection_request_paste(&ui->selection, term, ui->dpy, ui->win,
                                                XA_PRIMARY, ui->utf8_string, ui->xsel_data, ev->xkey.time);
                        return;
                    }
                }

                /* Navigation and Special Keys */
                const char *seq = NULL;
                switch (ksym) {
                    case XK_Up:
                        seq = term->app_cursor_keys ? "\033OA" : "\033[A";
                        break;
                    case XK_Down:
                        seq = term->app_cursor_keys ? "\033OB" : "\033[B";
                        break;
                    case XK_Right:
                        seq = term->app_cursor_keys ? "\033OC" : "\033[C";
                        break;
                    case XK_Left:
                        seq = term->app_cursor_keys ? "\033OD" : "\033[D";
                        break;
                    case XK_Home:
                        seq = "\033[H";
                        break;
                    case XK_End:
                        seq = "\033[F";
                        break;
                    case XK_Prior:
                        seq = "\033[5~";
                        break;
                    case XK_Next:
                        seq = "\033[6~";
                        break;
                    case XK_Insert:
                        seq = "\033[2~";
                        break;
                    case XK_Delete:
                        seq = "\033[3~";
                        break;
                    case XK_BackSpace:
                        seq = "\177";
                        break;
                    case XK_Return:
                    case XK_KP_Enter:
                        seq = "\r";
                        break;
                    case XK_Tab:
                        seq = "\t";
                        break;
                    case XK_F1: seq = "\033OP"; break;
                    case XK_F2: seq = "\033OQ"; break;
                    case XK_F3: seq = "\033OR"; break;
                    case XK_F4: seq = "\033OS"; break;
                    case XK_F5: seq = "\033[15~"; break;
                    case XK_F6: seq = "\033[17~"; break;
                    case XK_F7: seq = "\033[18~"; break;
                    case XK_F8: seq = "\033[19~"; break;
                    case XK_F9: seq = "\033[20~"; break;
                    case XK_F10: seq = "\033[21~"; break;
                    case XK_F11: seq = "\033[23~"; break;
                    case XK_F12: seq = "\033[24~"; break;
                }

                if (seq) {
                    term_write_child(term, seq, strlen(seq));
                    term_reset_viewport(term);
                } else if (len > 0) {
                    if (alt) {
                        /* Alt/Meta key sends ESC prefix */
                        char alt_buf[66];
                        alt_buf[0] = '\033';
                        memcpy(alt_buf + 1, buf, len);
                        term_write_child(term, alt_buf, len + 1);
                    } else {
                        term_write_child(term, buf, len);
                    }
                    term_reset_viewport(term);
                }
            }
            break;
    }
}
