/* Host rasteriser standing in for the firmware canvas.
 *
 * Renders into a 128x64 1-bit buffer so the battle layout can be inspected as
 * an image during development, and counts any pixel drawn off-panel so
 * clipping is caught automatically rather than spotted on a device. */
#include <gui/gui.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ft_font5x8.h"

#define STUB_W 128
#define STUB_H 64

/* Cap top sits on row 0 of the glyph cell, baseline six rows below. */
#define FONT_BASELINE 6

struct Canvas {
    uint8_t px[STUB_H][STUB_W]; /* 1 = ink */
    Color   color;
    Font    font;
    int     clipped;
};

Canvas* ft_stub_canvas_alloc(void) {
    Canvas* c = calloc(1, sizeof(Canvas));
    c->color = ColorBlack;
    c->font = FontSecondary;
    return c;
}

void ft_stub_canvas_free(Canvas* c) {
    free(c);
}

int ft_stub_canvas_clipped(const Canvas* c) {
    return c->clipped;
}

/* Advances are deliberately at or above the firmware's, so the preview errs
 * toward reporting overflow that the device would not actually show. */
static int font_advance(Font f) {
    switch(f) {
    case FontPrimary: return 7;
    case FontKeyboard: return 5;
    default: return 6;
    }
}

static void put(Canvas* c, int32_t x, int32_t y) {
    if(x < 0 || x >= STUB_W || y < 0 || y >= STUB_H) {
        c->clipped++;
        return;
    }
    switch(c->color) {
    case ColorWhite: c->px[y][x] = 0; break;
    case ColorXOR:   c->px[y][x] = (uint8_t)!c->px[y][x]; break;
    case ColorBlack:
    default:         c->px[y][x] = 1; break;
    }
}

void canvas_clear(Canvas* c) {
    memset(c->px, 0, sizeof(c->px));
    c->clipped = 0;
}

void canvas_set_color(Canvas* c, Color color) {
    c->color = color;
}

void canvas_set_font(Canvas* c, Font font) {
    c->font = font;
}

size_t canvas_current_font_height(const Canvas* c) {
    (void)c;
    return FT_FONT_H;
}

uint16_t canvas_string_width(Canvas* c, const char* str) {
    if(!str) return 0;
    return (uint16_t)(strlen(str) * (size_t)font_advance(c->font));
}

static void draw_glyph(Canvas* c, int32_t x, int32_t top, char ch) {
    if(ch < FT_FONT_FIRST || ch > FT_FONT_LAST) ch = '?';
    const uint8_t* cols = FT_FONT5X8[ch - FT_FONT_FIRST];

    for(int gx = 0; gx < FT_FONT_W; gx++) {
        for(int gy = 0; gy < FT_FONT_H; gy++) {
            if(cols[gx] & (1u << gy)) put(c, x + gx, top + gy);
        }
    }
}

void canvas_draw_str(Canvas* c, int32_t x, int32_t y, const char* str) {
    if(!str) return;
    const int adv = font_advance(c->font);
    const int32_t top = y - FONT_BASELINE;

    for(size_t i = 0; str[i]; i++) {
        draw_glyph(c, x + (int32_t)i * adv, top, str[i]);
    }
}

void canvas_draw_str_aligned(
    Canvas* c,
    int32_t x,
    int32_t y,
    Align   h,
    Align   v,
    const char* str) {
    if(!str) return;

    const int32_t w = (int32_t)canvas_string_width(c, str);

    if(h == AlignCenter) x -= w / 2;
    else if(h == AlignRight) x -= w;

    /* The firmware treats AlignTop as "y is the top of the cell". */
    if(v == AlignTop) y += FONT_BASELINE;
    else if(v == AlignCenter) y += FONT_BASELINE / 2;

    canvas_draw_str(c, x, y, str);
}

void canvas_draw_box(Canvas* c, int32_t x, int32_t y, size_t w, size_t h) {
    for(size_t j = 0; j < h; j++)
        for(size_t i = 0; i < w; i++) put(c, x + (int32_t)i, y + (int32_t)j);
}

void canvas_draw_frame(Canvas* c, int32_t x, int32_t y, size_t w, size_t h) {
    if(w == 0 || h == 0) return;
    for(size_t i = 0; i < w; i++) {
        put(c, x + (int32_t)i, y);
        put(c, x + (int32_t)i, y + (int32_t)h - 1);
    }
    for(size_t j = 0; j < h; j++) {
        put(c, x, y + (int32_t)j);
        put(c, x + (int32_t)w - 1, y + (int32_t)j);
    }
}

void canvas_draw_rframe(Canvas* c, int32_t x, int32_t y, size_t w, size_t h, size_t r) {
    if(w == 0 || h == 0) return;
    const int32_t ri = (int32_t)r;

    for(int32_t i = ri; i < (int32_t)w - ri; i++) {
        put(c, x + i, y);
        put(c, x + i, y + (int32_t)h - 1);
    }
    for(int32_t j = ri; j < (int32_t)h - ri; j++) {
        put(c, x, y + j);
        put(c, x + (int32_t)w - 1, y + j);
    }
    /* Corner pixels, approximated the same way the firmware does. */
    for(int32_t i = 0; i < ri; i++) {
        put(c, x + ri - i, y + i);
        put(c, x + (int32_t)w - 1 - ri + i, y + i);
        put(c, x + ri - i, y + (int32_t)h - 1 - i);
        put(c, x + (int32_t)w - 1 - ri + i, y + (int32_t)h - 1 - i);
    }
}

void canvas_draw_dot(Canvas* c, int32_t x, int32_t y) {
    put(c, x, y);
}

void canvas_draw_line(Canvas* c, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    int32_t dx = (x2 > x1) ? x2 - x1 : x1 - x2;
    int32_t dy = (y2 > y1) ? y2 - y1 : y1 - y2;
    const int32_t sx = (x1 < x2) ? 1 : -1;
    const int32_t sy = (y1 < y2) ? 1 : -1;
    dy = -dy;

    int32_t err = dx + dy;
    for(;;) {
        put(c, x1, y1);
        if(x1 == x2 && y1 == y2) break;
        const int32_t e2 = 2 * err;
        if(e2 >= dy) { err += dy; x1 += sx; }
        if(e2 <= dx) { err += dx; y1 += sy; }
    }
}

void canvas_draw_circle(Canvas* c, int32_t cx, int32_t cy, size_t radius) {
    int32_t x = (int32_t)radius, y = 0, err = 1 - x;
    while(x >= y) {
        put(c, cx + x, cy + y); put(c, cx + y, cy + x);
        put(c, cx - y, cy + x); put(c, cx - x, cy + y);
        put(c, cx - x, cy - y); put(c, cx - y, cy - x);
        put(c, cx + y, cy - x); put(c, cx + x, cy - y);
        y++;
        if(err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

void canvas_draw_disc(Canvas* c, int32_t cx, int32_t cy, size_t radius) {
    const int32_t r = (int32_t)radius;
    for(int32_t j = -r; j <= r; j++)
        for(int32_t i = -r; i <= r; i++)
            if(i * i + j * j <= r * r) put(c, cx + i, cy + j);
}

int ft_stub_canvas_write_pbm(const Canvas* c, const char* path) {
    FILE* f = fopen(path, "wb");
    if(!f) return -1;

    fprintf(f, "P1\n%d %d\n", STUB_W, STUB_H);
    for(int y = 0; y < STUB_H; y++) {
        for(int x = 0; x < STUB_W; x++) fputc(c->px[y][x] ? '1' : '0', f);
        fputc('\n', f);
    }
    fclose(f);
    return 0;
}
