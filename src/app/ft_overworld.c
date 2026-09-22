#include "ft_overworld.h"

#include "ft_enemy_art.h"
#include "ft_sprites.h"
#include "ft_tiles.h"

/* The overworld draws at 2x. At 1:1 a whole room fitted on the panel at once
 * and everything in it was 8px of a 128px screen: legible, but it read as a
 * diagram rather than a place. Doubling halves the visible area to 8x4 tiles
 * and doubles how much of the screen the player occupies.
 *
 * The world itself is untouched — tiles, collision, stepping and the camera
 * all still work in 8px tiles. Only this file scales, on the way out. */
#define FT_ZOOM 2

/* Panel size in *world* pixels, which is what the tilemap clips against
 * before being scaled. */
#define FT_SCREEN_PX_W (FT_VIEW_W * FT_TILE_PX)
#define FT_SCREEN_PX_H (FT_VIEW_H * FT_TILE_PX)

/* A horizontal run, clipped to the panel. The tilemap deliberately draws one
 * row and column past the viewport so a half-scrolled tile still appears, so
 * some of what it emits is off-panel by design — clipping here keeps that from
 * becoming wasted draw calls every single frame. */
static void run_clipped(Canvas* c, int32_t x, int32_t y, int32_t w) {
    if(y < 0 || y >= FT_SCREEN_PX_H) return;

    if(x < 0) {
        w += x;
        x = 0;
    }
    if(x + w > FT_SCREEN_PX_W) w = FT_SCREEN_PX_W - x;
    if(w <= 0) return;

    /* Clipped in world pixels, drawn in screen pixels: one box per run, so
     * zooming costs nothing at draw time. */
    canvas_draw_box(c, x * FT_ZOOM, y * FT_ZOOM, (size_t)(w * FT_ZOOM), FT_ZOOM);
}

/* Runs as boxes, not dots: the tilemap draws well over a hundred tiles a frame
 * and the GUI thread is shared with input dispatch. */
static void blit_rows(
    Canvas* c, const uint8_t* rows, int32_t n, int32_t x, int32_t y, int32_t w) {
    for(int32_t ry = 0; ry < n; ry++) {
        const uint32_t bits = rows[ry];
        if(!bits) continue;

        int32_t rx = 0;
        while(rx < w) {
            if(!(bits & (1u << rx))) {
                rx++;
                continue;
            }
            int32_t run = 0;
            while(rx + run < w && (bits & (1u << (rx + run)))) run++;

            run_clipped(c, x + rx, y + ry, run);
            rx += run;
        }
    }
}


/* ---- Actors ----------------------------------------------------------- */

/* The player and the foes are drawn at the panel's own resolution, not at the
 * world's. The tiles are 8px art doubled and are meant to be chunky; keeping
 * the two things you actually look at crisp is what makes the zoom read as
 * "closer" instead of "bigger pixels". So everything below is in screen
 * pixels, and clips against the real panel. */
#define FT_PANEL_W (FT_SCREEN_PX_W * FT_ZOOM)
#define FT_PANEL_H (FT_SCREEN_PX_H * FT_ZOOM)

static void screen_run(Canvas* c, int32_t x, int32_t y, int32_t w) {
    if(y < 0 || y >= FT_PANEL_H) return;

    if(x < 0) {
        w += x;
        x = 0;
    }
    if(x + w > FT_PANEL_W) w = FT_PANEL_W - x;
    if(w <= 0) return;

    canvas_draw_box(c, x, y, (size_t)w, 1);
}

/* Runs, not dots: this is the GUI thread and it also dispatches input. */
static void blit_screen(
    Canvas* c, const uint32_t* rows, int32_t n, int32_t x, int32_t y, int32_t w) {
    for(int32_t ry = 0; ry < n; ry++) {
        const uint32_t bits = rows[ry];
        if(!bits) continue;

        int32_t rx = 0;
        while(rx < w) {
            if(!(bits & (1u << rx))) {
                rx++;
                continue;
            }
            int32_t run = 0;
            while(rx + run < w && (bits & (1u << (rx + run)))) run++;

            screen_run(c, x + rx, y + ry, run);
            rx += run;
        }
    }
}

/* A keyline, not a box and not an inversion.
 *
 * The white box swallowed whatever the sprite stood next to. XOR fixed that
 * but broke the silhouette: over a dithered tile the body came out checkered,
 * and standing half on a dark tile split it down the middle into two colours.
 * Outlining the shape itself — one white pixel around the sprite's own edge,
 * then the sprite in black on top — keeps it black and whole on every
 * background and erases nothing beyond its own outline. */
static void blit_keyed(
    Canvas* c, const uint32_t* rows, int32_t n, int32_t x, int32_t y, int32_t w) {
    uint32_t halo[(FT_SPRITE_H > FT_HERO_H ? FT_SPRITE_H : FT_HERO_H) + 2];

    for(int32_t i = 0; i < n + 2; i++) halo[i] = 0;

    for(int32_t i = 0; i < n; i++) {
        /* Widen this row a pixel each way, in the 1-pixel-shifted frame. */
        const uint32_t r = rows[i] << 1;
        const uint32_t wide = r | (r << 1) | (r >> 1);

        halo[i]     |= wide;
        halo[i + 1] |= wide;
        halo[i + 2] |= wide;
    }

    canvas_set_color(c, ColorWhite);
    blit_screen(c, halo, n + 2, x - 1, y - 1, w + 2);
    canvas_set_color(c, ColorBlack);

    blit_screen(c, rows, n, x, y, w);
}

/* The hero. x/y are his tile position in world pixels.
 *
 * This is the same FT_SPRITE_HERO the battle screen draws. There used to be a
 * second, separate avatar here — the same character described twice, at a
 * different size, and they did not match. Which one is "the player" is the
 * first thing anyone reads, so there is now only one answer. */
static void draw_avatar(Canvas* c, int32_t x, int32_t y, FtFacing facing, int32_t bob) {
    uint32_t rows[FT_HERO_H];
    for(int32_t i = 0; i < FT_HERO_H; i++) rows[i] = FT_SPRITE_HERO[i];

    /* Two-frame walk: the feet close and open. Anything more elaborate is
     * invisible even at this size. */
    if(bob) {
        rows[FT_HERO_H - 2] = 0x0660;
        rows[FT_HERO_H - 3] = 0x0660;
    }

    /* Feet on the tile he occupies. The sprite is taller than a tile, so he
     * is bottom-aligned and stands two pixels proud of it. */
    const int32_t sx = x * FT_ZOOM;
    const int32_t sy = y * FT_ZOOM + (FT_TILE_PX * FT_ZOOM - FT_HERO_H);

    blit_keyed(c, rows, FT_HERO_H, sx, sy, FT_HERO_W);

    /* Facing lives in the screen rather than in four sprite sets: a turned
     * body is unreadable at this size, shifted pupils are not. Facing away
     * leaves the screen blank, which is the back of his head. */
    const int32_t ex = sx + FT_HERO_SCREEN_X;
    const int32_t ey = sy + FT_HERO_EYE_Y;

    if(ex < 0 || ex + FT_HERO_SCREEN_W > FT_PANEL_W) return;
    if(ey < 0 || ey + FT_HERO_EYE_H > FT_PANEL_H) return;

    /* Wipe the band the bitmap's own eyes sit in, then put them back where
     * this facing wants them. */
    canvas_set_color(c, ColorWhite);
    canvas_draw_box(c, ex, ey, FT_HERO_SCREEN_W, FT_HERO_EYE_H);
    canvas_set_color(c, ColorBlack);

    if(facing == FT_FACE_UP) return;

    int32_t left, right;
    switch(facing) {
    case FT_FACE_LEFT:
        left = ex;
        right = ex + 4;
        break;
    case FT_FACE_RIGHT:
        left = ex + FT_HERO_SCREEN_W - 6;
        right = ex + FT_HERO_SCREEN_W - 2;
        break;
    case FT_FACE_DOWN:
    default:
        left = ex + 1;
        right = ex + FT_HERO_SCREEN_W - 3;
        break;
    }

    canvas_draw_box(c, left, ey, 2, FT_HERO_EYE_H);
    canvas_draw_box(c, right, ey, 2, FT_HERO_EYE_H);
}

/* Foes, at their full battle resolution. They used to be sampled down to 8x8
 * to sit beside an 8x12 player; at 2x there is room for all sixteen rows, so
 * the thing in the corridor is the thing you are about to fight. */
static void draw_foe(Canvas* c, const uint16_t* rows, int32_t x, int32_t y) {
    const int32_t sx = x * FT_ZOOM;
    const int32_t sy = y * FT_ZOOM + (FT_TILE_PX * FT_ZOOM - FT_SPRITE_H);

    /* Off-panel is both wasted work and, in the preview harness, a clipping
     * failure — the tilemap already draws a row past the edge by design. */
    if(sx + FT_SPRITE_W < 0 || sx >= FT_PANEL_W) return;
    if(sy + FT_SPRITE_H < 0 || sy >= FT_PANEL_H) return;

    uint32_t wide[FT_SPRITE_H];
    for(int32_t i = 0; i < FT_SPRITE_H; i++) wide[i] = rows[i];

    blit_keyed(c, wide, FT_SPRITE_H, sx, sy, FT_SPRITE_W);
}

void ft_overworld_toast(Canvas* canvas, const char* text) {
    if(!text) return;

    canvas_set_font(canvas, FontSecondary);
    const int32_t w = (int32_t)canvas_string_width(canvas, text) + 8;
    const int32_t x = (FT_PANEL_W - w) / 2;
    const int32_t y = FT_PANEL_H - 14;

    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, x, y, (size_t)w, 12);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_frame(canvas, x, y, (size_t)w, 12);
    canvas_draw_str(canvas, x + 4, y + 9, text);
}

void ft_overworld_render(Canvas* canvas, const FtWorld* w) {
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    const FtMap*  map = ft_world_map(w);
    const FtPos   player = ft_stepper_pos(&w->mv, FT_STEP_MS);
    const FtPos   cam = ft_map_camera(map, player);
    const FtRoom* room = ft_room(w->room);

    /* One extra column and row so a half-scrolled tile still draws. */
    const int32_t first_tx = cam.x / FT_TILE_PX;
    const int32_t first_ty = cam.y / FT_TILE_PX;
    const int32_t off_x = cam.x % FT_TILE_PX;
    const int32_t off_y = cam.y % FT_TILE_PX;

    for(int32_t ty = 0; ty <= FT_VIEW_H; ty++) {
        for(int32_t tx = 0; tx <= FT_VIEW_W; tx++) {
            const int32_t mx = first_tx + tx;
            const int32_t my = first_ty + ty;
            const int32_t sx = tx * FT_TILE_PX - off_x;
            const int32_t sy = ty * FT_TILE_PX - off_y;

            /* Art index, not the raw tile: walls, doors, locked ports and
             * conduit all pick their art from their neighbours (see
             * ft_map_art_index). */
            blit_rows(
                canvas, FT_TILE_ART[ft_map_art_index(map, mx, my)], FT_TILE_PX, sx, sy,
                FT_TILE_PX);

            /* Loose greenery over open floor, placed procedurally. */
            if(ft_map_scatter(map, mx, my)) {
                blit_rows(
                    canvas, FT_TILE_ART[FT_TILE_ART_TUFT], FT_TILE_PX, sx, sy,
                    FT_TILE_PX);
            }

            /* Then the wall's shadow, over the top of whatever is below it. */
            if(ft_map_has_shadow(map, mx, my)) {
                blit_rows(
                    canvas, FT_TILE_ART[FT_TILE_ART_SHADOW], FT_TILE_PX, sx, sy,
                    FT_TILE_PX);
            }
        }
    }

    /* Foes before the avatar, so the player is never hidden behind one.
     *
     * Every member of a roster is its own walker on its own tile. Pinning two
     * sprites at fixed offsets from a leader made a group of three read as
     * one object being dragged about. */
    for(uint8_t i = 0; i < room->ent_count && i < FT_MAX_ROOM_ENTS; i++) {
        if(room->ents[i].kind != FT_ENT_FOE) continue;
        if(!w->foes[i].alive) continue;

        const FtRoster* roster = ft_roster(room->ents[i].roster);

        for(uint8_t m = 0; m < w->foes[i].count && m < FT_MAX_ENEMIES; m++) {
            const FtPos fp = ft_stepper_pos(&w->foes[i].w[m].mv, FT_FOE_STEP_MS);

            /* Both actors are bottom-aligned on their tile by draw_foe and
             * draw_avatar, so no fudge is needed here. */
            draw_foe(canvas, ft_enemy_art(roster->foes[m]), fp.x - cam.x, fp.y - cam.y);
        }
    }

    const bool moving = ft_world_moving(w);
    const int32_t bob = moving ? (int32_t)((w->walk_ms / 150u) % 2u) : 0;
    const int32_t ax = player.x - cam.x;
    const int32_t ay = player.y - cam.y;

    draw_avatar(canvas, ax, ay, w->facing, bob);

    /* Area name, in a cleared strip so it stays legible over any tile. It
     * retires after a couple of seconds rather than occupying the corner for
     * the whole visit. */
    if(map->name && w->area_ms < FT_AREA_BANNER_MS) {
        canvas_set_font(canvas, FontSecondary);
        const int32_t w = (int32_t)canvas_string_width(canvas, map->name) + 6;

        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, 0, 0, (size_t)w, 10);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_frame(canvas, 0, 0, (size_t)w, 10);
        canvas_draw_str(canvas, 3, 7, map->name);
    }
}
