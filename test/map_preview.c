/* Renders overworld screens to .pbm so the map sketches can be judged at the
 * size they will actually be played at. */
#include <gui/gui.h>

#include <stdio.h>

#include "ft_maps.h"
#include "../core/ft_world.h"
#include "ft_overworld.h"
#include "ft_tiles.h"

typedef struct {
    const char*  name;
    uint8_t      room;
    const FtMap* map;
    int32_t      px, py;
    FtFacing     facing;
    bool         moving;
    uint32_t     area_ms; /* banner is only up briefly after entering */
} Shot;

/* Draw an entire map into one oversized canvas, so a level can be reviewed as
 * a whole rather than a screen at a time.
 *
 * This walks the tiles itself rather than calling ft_overworld_render, which
 * is bound to the 128x64 viewport — but it asks core for every decision
 * (orientation, scatter, shadow), so what it shows is what the game draws. */
static void draw_whole_map(Canvas* c, const FtMap* m) {
    canvas_clear(c);
    canvas_set_color(c, ColorBlack);

    for(int32_t ty = 0; ty < (int32_t)m->h; ty++) {
        for(int32_t tx = 0; tx < (int32_t)m->w; tx++) {
            const int32_t ox = tx * FT_TILE_PX;
            const int32_t oy = ty * FT_TILE_PX;

            const uint8_t layers[3] = {
                ft_map_art_index(m, tx, ty),
                ft_map_scatter(m, tx, ty) ? (uint8_t)FT_TILE_ART_TUFT : 0xFFu,
                ft_map_has_shadow(m, tx, ty) ? (uint8_t)FT_TILE_ART_SHADOW : 0xFFu,
            };

            for(int l = 0; l < 3; l++) {
                if(layers[l] == 0xFFu) continue;
                const uint8_t* rows = FT_TILE_ART[layers[l]];

                for(int32_t ry = 0; ry < FT_TILE_PX; ry++) {
                    for(int32_t rx = 0; rx < FT_TILE_PX; rx++) {
                        if(rows[ry] & (1u << rx)) canvas_draw_dot(c, ox + rx, oy + ry);
                    }
                }
            }
        }
    }
}

static void write_whole_map(const FtMap* m, const char* path) {
    Canvas* c = ft_stub_canvas_alloc_size(
        (int)m->w * FT_TILE_PX, (int)m->h * FT_TILE_PX);

    draw_whole_map(c, m);
    ft_stub_canvas_write_pbm(c, path);

    printf("  %-16s %ux%u tiles -> %s\n", m->name, m->w, m->h, path);
    ft_stub_canvas_free(c);
}

int main(void) {
    static const Shot shots[] = {
        {"cb1-wake",     0, &FT_MAP_CB1, 32,  28, FT_FACE_RIGHT, false, 0},
        {"cb1-terminal", 0, &FT_MAP_CB1, 36,  20, FT_FACE_UP,    false, 9000},
        {"cb1-exit",     0, &FT_MAP_CB1, 112, 28, FT_FACE_RIGHT, true,  9000},
        {"cb1-npc",      0, &FT_MAP_CB1, 48,  16, FT_FACE_RIGHT, false, 9000},
        {"cb1-tree",     0, &FT_MAP_CB1, 160, 48, FT_FACE_UP,    false, 9000},
        {"cb1-behind",   0, &FT_MAP_CB1, 176, 32, FT_FACE_UP,    false, 9000},
        {"cb4-cache",    3, &FT_MAP_CB4, 16,  64, FT_FACE_RIGHT, false, 9000},
        /* Chapter 1: the fork, the gate and the junction. */
        {"ap1-fork",     9, &FT_MAP_AP1, 176, 64, FT_FACE_DOWN,  false, 0},
        {"ap1-trees",    9, &FT_MAP_AP1, 48,  48, FT_FACE_LEFT,  false, 9000},
        {"wh1-gate",    10, &FT_MAP_WH1, 320, 96, FT_FACE_RIGHT, false, 0},
        {"wh1-huts",    10, &FT_MAP_WH1, 160, 48, FT_FACE_UP,    false, 9000},
        {"ej1-wren",    11, &FT_MAP_EJ1, 288, 112, FT_FACE_RIGHT, false, 0},
        {"cb2-foe",      1, &FT_MAP_CB2, 48,  28, FT_FACE_RIGHT, true,  0},
        {"cb2-mid",      1, &FT_MAP_CB2, 96,  36, FT_FACE_RIGHT, true,  9000},
        {"cb3-shelf",    2, &FT_MAP_CB3, 48,  28, FT_FACE_DOWN,  true,  0},
        {"cb3-ladder",   2, &FT_MAP_CB3, 40,  36, FT_FACE_DOWN,  true,  9000},
        {"cb3-lower",    2, &FT_MAP_CB3, 80,  68, FT_FACE_RIGHT, true,  9000},
        {"cb4-lock",     3, &FT_MAP_CB4, 40,  44, FT_FACE_RIGHT, false, 0},
        {"cb4-exit",     3, &FT_MAP_CB4, 120, 68, FT_FACE_RIGHT, true,  9000},
        /* The concept slices, each at the thing that makes it that place. */
        {"sl1-gap",      4, &FT_MAP_SL1, 80,  36, FT_FACE_RIGHT, true,  0},
        {"sl1-scrap",    4, &FT_MAP_SL1, 48,  28, FT_FACE_RIGHT, true,  9000},
        {"cs1-cell",     5, &FT_MAP_CS1, 72,  36, FT_FACE_DOWN,  true,  0},
        {"cs1-frost",    5, &FT_MAP_CS1, 140, 28, FT_FACE_LEFT,  true,  9000},
        {"ts1-gate",     6, &FT_MAP_TS1, 80,  36, FT_FACE_RIGHT, true,  0},
        {"sh1-pylons",   7, &FT_MAP_SH1, 72,  20, FT_FACE_RIGHT, true,  0},
        {"sh1-ladder",   7, &FT_MAP_SH1, 48,  36, FT_FACE_DOWN,  true,  9000},
        {"dz1-static",   8, &FT_MAP_DZ1, 80,  44, FT_FACE_RIGHT, true,  0},
        {"dz1-void",     8, &FT_MAP_DZ1, 24,  52, FT_FACE_RIGHT, true,  9000},
    };

    Canvas* canvas = ft_stub_canvas_alloc();

    /* Every line the overworld can toast, checked for width rather than
     * counted by hand. The toast box is text + 8 and is not clipped, so one
     * long string is one off-panel banner. */
    static const char* const TOASTS[] = {
        "Saved. Restored.", "Restored. No card.", "Find a terminal.",
        "Not in a fight.",  "No card.",           "New run.",
        "Cleared.",         "Rebooted.",          "Nothing here.",
    };

    int clipped_total = 0;

    for(size_t i = 0; i < sizeof(TOASTS) / sizeof(TOASTS[0]); i++) {
        FtWorld tw;
        ft_world_init(&tw);
        ft_overworld_render(canvas, &tw);
        ft_overworld_toast(canvas, TOASTS[i]);

        const int c = ft_stub_canvas_clipped(canvas);
        clipped_total += c;
        if(c) printf("  TOAST CLIPPED: \"%s\"\n", TOASTS[i]);
    }
    printf("  %zu toasts fit\n", sizeof(TOASTS) / sizeof(TOASTS[0]));


    for(size_t i = 0; i < sizeof(shots) / sizeof(shots[0]); i++) {
        const Shot* s = &shots[i];

        FtWorld w;
        ft_world_init(&w);
        ft_world_enter(&w, s->room, (uint8_t)(s->px / FT_TILE_PX),
                       (uint8_t)(s->py / FT_TILE_PX));
        w.facing = s->facing;
        w.walk_ms = 200;
        w.area_ms = s->area_ms;

        ft_overworld_render(canvas, &w);

        char path[128];
        snprintf(path, sizeof(path), "preview/map_%02zu_%s.pbm", i, s->name);
        ft_stub_canvas_write_pbm(canvas, path);

        const int clipped = ft_stub_canvas_clipped(canvas);
        clipped_total += clipped;
        printf("  %-14s %s\n", s->name, clipped ? "CLIPPED" : "ok");
    }

    /* Walking her home: the escort behind the avatar, in the room she was
     * taken from and in the one she is going to. */
    {
        FtWorld esc;
        ft_world_init(&esc);
        ft_quest_advance(&esc.quests, FT_QUEST_WREN, FT_QUEST_READY);
        ft_world_enter(&esc, 11, 8, 1);
        ft_world_escort_start(&esc);
        esc.escort_mv.tx = 9;
        esc.escort_mv.ty = 1;
        esc.facing = FT_FACE_LEFT;

        ft_overworld_render(canvas, &esc);
        ft_stub_canvas_write_pbm(canvas, "preview/map_24_escort.pbm");

        const int c = ft_stub_canvas_clipped(canvas);
        clipped_total += c;
        printf("  %-14s %s\n", "escort", c ? "CLIPPED" : "ok");
    }

    /* Spotted: the mark over a group that has seen you and has not started
     * moving yet. It is the only thing drawn above a sprite, so it is also
     * the only thing that can run off the top of the panel. */
    {
        FtWorld n;
        ft_world_init(&n);
        ft_world_enter(&n, 1, 11, 3);
        n.foes[0].alert = true;
        n.foes[0].notice_ms = FT_FOE_NOTICE_MS;

        ft_overworld_render(canvas, &n);
        ft_stub_canvas_write_pbm(canvas, "preview/map_19_spotted.pbm");

        const int c = ft_stub_canvas_clipped(canvas);
        clipped_total += c;
        printf("  %-14s %s\n", "spotted", c ? "CLIPPED" : "ok");

        /* And again with the group pressed against the top wall, where the
         * mark has the least room above it. */
        for(uint8_t m = 0; m < n.foes[0].count; m++) {
            n.foes[0].w[m].mv.ty = 1u;
            n.foes[0].w[m].mv.tx = (uint8_t)(10u + m);
        }
        n.mv.tx = 12;
        n.mv.ty = 1;

        ft_overworld_render(canvas, &n);
        ft_stub_canvas_write_pbm(canvas, "preview/map_20_spotted-top.pbm");

        const int c2 = ft_stub_canvas_clipped(canvas);
        clipped_total += c2;
        printf("  %-14s %s\n", "spotted-top", c2 ? "CLIPPED" : "ok");
    }

    /* Camera must never show outside the map, however far the player pushes. */
    const FtPos corners[] = {{0, 0}, {10000, 10000}, {-500, -500}};
    for(size_t i = 0; i < sizeof(corners) / sizeof(corners[0]); i++) {
        const FtPos c = ft_map_camera(&FT_MAP_CB3, corners[i]);
        const int32_t max_x = (int32_t)FT_MAP_CB3.w * FT_TILE_PX - FT_VIEW_W * FT_TILE_PX;
        const int32_t max_y = (int32_t)FT_MAP_CB3.h * FT_TILE_PX - FT_VIEW_H * FT_TILE_PX;

        if(c.x < 0 || c.y < 0 || c.x > max_x || c.y > max_y) {
            printf("  CAMERA ESCAPED at (%d,%d) -> (%d,%d)\n", corners[i].x, corners[i].y,
                   c.x, c.y);
            clipped_total++;
        }
    }

    ft_stub_canvas_free(canvas);

    /* Whole-level views, for judging layout rather than presentation. */
    write_whole_map(&FT_MAP_CB1, "preview/whole_1_cb1.pbm");
    write_whole_map(&FT_MAP_CB2, "preview/whole_2_cb2.pbm");
    write_whole_map(&FT_MAP_CB3, "preview/whole_3_cb3.pbm");
    write_whole_map(&FT_MAP_CB4, "preview/whole_4_cb4.pbm");
    write_whole_map(&FT_MAP_AP1, "preview/whole_a_ap1.pbm");
    write_whole_map(&FT_MAP_WH1, "preview/whole_b_wh1.pbm");
    write_whole_map(&FT_MAP_EJ1, "preview/whole_c_ej1.pbm");
    write_whole_map(&FT_MAP_SL1, "preview/whole_5_sl1.pbm");
    write_whole_map(&FT_MAP_CS1, "preview/whole_6_cs1.pbm");
    write_whole_map(&FT_MAP_TS1, "preview/whole_7_ts1.pbm");
    write_whole_map(&FT_MAP_SH1, "preview/whole_8_sh1.pbm");
    write_whole_map(&FT_MAP_DZ1, "preview/whole_9_dz1.pbm");

    printf("\n%s\n", clipped_total ? "MAP PREVIEW FAILED" : "map preview clean");
    return clipped_total ? 1 : 0;
}
