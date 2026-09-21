#include "ft_overworld.h"

#include "ft_tiles.h"

/* The overworld avatar, 8x12: the same handheld device as the battle sprite,
 * shrunk. It is the player character, so it must be recognisably the thing you
 * are in combat — antenna, screen, stubby legs — not a generic little figure.
 *
 * Smaller than the 16x16 battle sprite on purpose: that would eat a quarter of
 * the viewport's height. */
#define AV_SCREEN_ROW 5

static const uint8_t FT_AVATAR[FT_AVATAR_H] = {
    0x18, /* ...##...  antenna */
    0x18, /* ...##... */
    0x7E, /* .######.  case */
    0x7E, /* .######. */
    0x42, /* .#....#.  screen */
    0x66, /* .##..##.  eyes      <- AV_SCREEN_ROW */
    0x42, /* .#....#. */
    0x7E, /* .######. */
    0x7E, /* .######. */
    0x24, /* ..#..#..  legs */
    0x24, /* ..#..#.. */
    0x66, /* .##..##.  feet */
};

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

    canvas_draw_box(c, x, y, (size_t)w, 1);
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

/* Knock a one-pixel white gap around the avatar before drawing it. Without
 * this it has the same visual weight as a crate and disappears into the floor
 * stipple; with it, the eye finds the player instantly. */
static void draw_avatar_halo(Canvas* c, int32_t x, int32_t y) {
    canvas_set_color(c, ColorWhite);
    for(int32_t ry = -1; ry <= FT_AVATAR_H; ry++) {
        run_clipped(c, x - 1, y + ry, FT_AVATAR_W + 2);
    }
    canvas_set_color(c, ColorBlack);
}

static void draw_avatar(Canvas* c, int32_t x, int32_t y, FtFacing facing, int32_t bob) {
    uint8_t rows[FT_AVATAR_H];
    for(int32_t i = 0; i < FT_AVATAR_H; i++) rows[i] = FT_AVATAR[i];

    /* Facing lives in the screen rather than in four sprite sets: at 8px wide
     * a turned body is unreadable, but shifted pupils are not. */
    switch(facing) {
    case FT_FACE_LEFT:  rows[AV_SCREEN_ROW] = 0x56; break; /* .##.#.#. */
    case FT_FACE_RIGHT: rows[AV_SCREEN_ROW] = 0x6A; break; /* .#.#.##. */
    case FT_FACE_UP:    rows[AV_SCREEN_ROW] = 0x42; break; /* screen dark */
    case FT_FACE_DOWN:
    default:            break;
    }

    /* Two-frame walk: the feet close and open. Anything more elaborate is
     * invisible at this size. */
    if(bob) rows[FT_AVATAR_H - 1] = 0x24;

    blit_rows(c, rows, FT_AVATAR_H, x, y, FT_AVATAR_W);
}

void ft_overworld_render(
    Canvas*      canvas,
    const FtMap* map,
    FtPos        player,
    FtFacing     facing,
    uint32_t     step_ms,
    bool         moving,
    uint32_t     area_ms) {
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    const FtPos cam = ft_map_camera(map, player);

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

    const int32_t bob = moving ? (int32_t)((step_ms / 140u) % 2u) : 0;
    const int32_t ax = player.x - cam.x;
    const int32_t ay = player.y - cam.y;

    draw_avatar_halo(canvas, ax, ay);
    draw_avatar(canvas, ax, ay, facing, bob);

    /* Area name, in a cleared strip so it stays legible over any tile. It
     * retires after a couple of seconds rather than occupying the corner for
     * the whole visit. */
    if(map->name && area_ms < FT_AREA_BANNER_MS) {
        canvas_set_font(canvas, FontSecondary);
        const int32_t w = (int32_t)canvas_string_width(canvas, map->name) + 6;

        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, 0, 0, (size_t)w, 10);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_frame(canvas, 0, 0, (size_t)w, 10);
        canvas_draw_str(canvas, 3, 7, map->name);
    }
}
