/* Host-side stand-in for the firmware's <gui/gui.h>.
 *
 * Only exists so src/app/ft_render.c can be compiled and rasterised on a
 * development machine, which is how the 128x64 layout gets checked without
 * flashing. Signatures match the real SDK exactly; see test/canvas_stub.c for
 * the implementation and test/preview.c for the harness. */
#ifndef FT_STUB_GUI_H
#define FT_STUB_GUI_H

#include <stddef.h>
#include <stdint.h>

typedef struct Canvas Canvas;

typedef enum {
    ColorWhite = 0x00,
    ColorBlack = 0x01,
    ColorXOR = 0x02,
} Color;

typedef enum {
    FontPrimary,
    FontSecondary,
    FontKeyboard,
    FontBigNumbers,
    FontTotalNumber,
} Font;

typedef enum {
    AlignLeft,
    AlignRight,
    AlignTop,
    AlignBottom,
    AlignCenter,
} Align;

void     canvas_clear(Canvas* canvas);
void     canvas_set_color(Canvas* canvas, Color color);
void     canvas_set_font(Canvas* canvas, Font font);
void     canvas_draw_str(Canvas* canvas, int32_t x, int32_t y, const char* str);
void     canvas_draw_str_aligned(
        Canvas* canvas,
        int32_t x,
        int32_t y,
        Align   horizontal,
        Align   vertical,
        const char* str);
void     canvas_draw_frame(Canvas* canvas, int32_t x, int32_t y, size_t width, size_t height);
void     canvas_draw_box(Canvas* canvas, int32_t x, int32_t y, size_t width, size_t height);
void     canvas_draw_rframe(
        Canvas* canvas,
        int32_t x,
        int32_t y,
        size_t  width,
        size_t  height,
        size_t  radius);
void     canvas_draw_dot(Canvas* canvas, int32_t x, int32_t y);
void     canvas_draw_line(Canvas* canvas, int32_t x1, int32_t y1, int32_t x2, int32_t y2);
void     canvas_draw_circle(Canvas* canvas, int32_t x, int32_t y, size_t radius);
void     canvas_draw_disc(Canvas* canvas, int32_t x, int32_t y, size_t radius);
uint16_t canvas_string_width(Canvas* canvas, const char* str);
size_t   canvas_current_font_height(const Canvas* canvas);

/* Harness-only extras. */
Canvas* ft_stub_canvas_alloc(void);
void    ft_stub_canvas_free(Canvas* canvas);
int     ft_stub_canvas_write_pbm(const Canvas* canvas, const char* path);
/* Number of pixels drawn outside the 128x64 panel since the last clear. */
int     ft_stub_canvas_clipped(const Canvas* canvas);

#endif /* FT_STUB_GUI_H */
