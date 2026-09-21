/* Renders overworld screens to .pbm so the map sketches can be judged at the
 * size they will actually be played at. */
#include <gui/gui.h>

#include <stdio.h>

#include "ft_maps.h"
#include "ft_overworld.h"

typedef struct {
    const char*  name;
    const FtMap* map;
    int32_t      px, py;
    FtFacing     facing;
    bool         moving;
    uint32_t     area_ms; /* banner is only up briefly after entering */
} Shot;

int main(void) {
    static const Shot shots[] = {
        {"cold-start",   &FT_MAP_COLD_BOOT, 40,  40,  FT_FACE_DOWN,  false, 0},
        {"cold-term",    &FT_MAP_COLD_BOOT, 44,  20,  FT_FACE_UP,    true, 9000},
        {"cold-door",    &FT_MAP_COLD_BOOT, 76,  36,  FT_FACE_RIGHT, true, 9000},
        {"cold-grass",   &FT_MAP_COLD_BOOT, 40,  80,  FT_FACE_DOWN,  true, 9000},
        {"cold-locked",  &FT_MAP_COLD_BOOT, 180, 84,  FT_FACE_RIGHT, false, 9000},
        {"door-side",    &FT_MAP_COLD_BOOT, 72,  36,  FT_FACE_RIGHT, false, 9000},
        {"door-front",   &FT_MAP_COLD_BOOT, 24,  52,  FT_FACE_DOWN,  false, 9000},
        {"lock-front",   &FT_MAP_COLD_BOOT, 184, 76,  FT_FACE_UP,    false, 9000},
        {"scrap-open",   &FT_MAP_SCRAPLINE, 60,  36,  FT_FACE_DOWN,  false, 0},
        {"scrap-void",   &FT_MAP_SCRAPLINE, 56,  70,  FT_FACE_RIGHT, true, 9000},
        {"scrap-corner", &FT_MAP_SCRAPLINE, 240, 60,  FT_FACE_RIGHT, true, 9000},
        {"scrap-far",    &FT_MAP_SCRAPLINE, 264, 130, FT_FACE_UP,    false, 9000},
    };

    Canvas* canvas = ft_stub_canvas_alloc();
    int clipped_total = 0;

    for(size_t i = 0; i < sizeof(shots) / sizeof(shots[0]); i++) {
        const Shot* s = &shots[i];
        const FtPos p = {s->px, s->py};

        ft_overworld_render(canvas, s->map, p, s->facing, 200, s->moving, s->area_ms);

        char path[128];
        snprintf(path, sizeof(path), "preview/map_%02zu_%s.pbm", i, s->name);
        ft_stub_canvas_write_pbm(canvas, path);

        const int clipped = ft_stub_canvas_clipped(canvas);
        clipped_total += clipped;
        printf("  %-14s %s\n", s->name, clipped ? "CLIPPED" : "ok");
    }

    /* Camera must never show outside the map, however far the player pushes. */
    const FtPos corners[] = {{0, 0}, {10000, 10000}, {-500, -500}};
    for(size_t i = 0; i < sizeof(corners) / sizeof(corners[0]); i++) {
        const FtPos c = ft_map_camera(&FT_MAP_SCRAPLINE, corners[i]);
        const int32_t max_x = (int32_t)FT_MAP_SCRAPLINE.w * FT_TILE_PX - FT_VIEW_W * FT_TILE_PX;
        const int32_t max_y = (int32_t)FT_MAP_SCRAPLINE.h * FT_TILE_PX - FT_VIEW_H * FT_TILE_PX;

        if(c.x < 0 || c.y < 0 || c.x > max_x || c.y > max_y) {
            printf("  CAMERA ESCAPED at (%d,%d) -> (%d,%d)\n", corners[i].x, corners[i].y,
                   c.x, c.y);
            clipped_total++;
        }
    }

    ft_stub_canvas_free(canvas);

    printf("\n%s\n", clipped_total ? "MAP PREVIEW FAILED" : "map preview clean");
    return clipped_total ? 1 : 0;
}
