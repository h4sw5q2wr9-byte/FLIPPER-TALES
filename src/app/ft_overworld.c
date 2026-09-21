#include "ft_overworld.h"

#include "ft_sprites.h"
#include "ft_tiles.h"

/* The overworld avatar, 8x12: the same handheld device as the battle sprite,
 * shrunk. It is the player character, so it must be recognisably the thing you
 * are in combat — antenna, screen, stubby legs — not a generic little figure.
 *
 * Smaller than the 16x16 battle sprite on purpose: that would eat a quarter of
 * the viewport's height. */
/* Solid, with the screen knocked out in white afterwards rather than drawn as
 * a hole in the bitmap. Outlining the case left the head an empty rectangle:
 * at 8x12 that reads as a picture frame standing on legs, not a character. */
static const uint8_t FT_AVATAR[FT_AVATAR_H] = {
    0x18, /* ...##...  antenna */
    0x18, /* ...##... */
    0x7E, /* .######.  case */
    0x7E, /* .######.  <- the screen is knocked out of these three */
    0x7E, /* .######. */
    0x7E, /* .######. */
    0x7E, /* .######. */
    0x3C, /* ..####..  shoulders */
    0x7E, /* .######.  body */
    0x24, /* ..#..#..  legs */
    0x24, /* ..#..#.. */
    0x66, /* .##..##.  feet */
};

/* The screen: four pixels across, three down, inset into the case. */
#define AV_SCREEN_X 2
#define AV_SCREEN_Y 3
#define AV_SCREEN_W 4
#define AV_SCREEN_H 3

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

/* Same, for masks wider than eight pixels. */
static void blit_rows16(
    Canvas* c, const uint16_t* rows, int32_t n, int32_t x, int32_t y, int32_t w) {
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

static void draw_avatar(Canvas* c, int32_t x, int32_t y, FtFacing facing, int32_t bob) {
    uint8_t rows[FT_AVATAR_H];
    for(int32_t i = 0; i < FT_AVATAR_H; i++) rows[i] = FT_AVATAR[i];

    /* Two-frame walk: the feet close and open. Anything more elaborate is
     * invisible at this size. */
    if(bob) rows[FT_AVATAR_H - 1] = 0x24;

    /* A keyline, not a box and not an inversion.
     *
     * The white box swallowed whatever the player stood next to. XOR fixed
     * that but broke the silhouette: over a dithered tile the body came out
     * checkered, and standing half on a dark tile split it down the middle
     * into two colours. Outlining the shape itself — one white pixel around
     * the sprite's own edge, then the sprite in black on top — keeps the
     * player black and whole on every background and erases nothing beyond
     * its own outline. */
    uint16_t halo[FT_AVATAR_H + 2];
    for(int32_t i = 0; i < FT_AVATAR_H + 2; i++) halo[i] = 0;

    for(int32_t i = 0; i < FT_AVATAR_H; i++) {
        /* Widen this row by a pixel each way, in the 1-pixel-shifted frame. */
        const uint16_t r = (uint16_t)((uint16_t)rows[i] << 1);
        const uint16_t wide = (uint16_t)(r | (uint16_t)(r << 1) | (uint16_t)(r >> 1));

        halo[i]     = (uint16_t)(halo[i] | wide);
        halo[i + 1] = (uint16_t)(halo[i + 1] | wide);
        halo[i + 2] = (uint16_t)(halo[i + 2] | wide);
    }

    canvas_set_color(c, ColorWhite);
    blit_rows16(c, halo, FT_AVATAR_H + 2, x - 1, y - 1, FT_AVATAR_W + 2);
    canvas_set_color(c, ColorBlack);

    blit_rows(c, rows, FT_AVATAR_H, x, y, FT_AVATAR_W);

    /* Facing lives in the screen rather than in four sprite sets: at 8px wide
     * a turned body is unreadable, but shifted pupils are not. Facing away
     * leaves the screen dark, which is the back of the head. */
    if(facing == FT_FACE_UP) return;

    const int32_t sx = x + AV_SCREEN_X, sy = y + AV_SCREEN_Y;
    if(sx < 0 || sx + AV_SCREEN_W > FT_SCREEN_PX_W) return;
    if(sy < 0 || sy + AV_SCREEN_H > FT_SCREEN_PX_H) return;

    canvas_set_color(c, ColorWhite);
    canvas_draw_box(c, sx, sy, AV_SCREEN_W, AV_SCREEN_H);
    canvas_set_color(c, ColorBlack);

    const int32_t eye = sy + 1;
    switch(facing) {
    case FT_FACE_LEFT:
        canvas_draw_dot(c, sx, eye);
        canvas_draw_dot(c, sx + 1, eye);
        break;
    case FT_FACE_RIGHT:
        canvas_draw_dot(c, sx + AV_SCREEN_W - 2, eye);
        canvas_draw_dot(c, sx + AV_SCREEN_W - 1, eye);
        break;
    case FT_FACE_DOWN:
    default:
        canvas_draw_dot(c, sx, eye);
        canvas_draw_dot(c, sx + AV_SCREEN_W - 1, eye);
        break;
    }
}

/* Foes standing in the room. Drawn at half the battle sprite's size by
 * sampling every other pixel — a 16x16 enemy would dwarf an 8x12 player. */
static void draw_foe(Canvas* c, const uint16_t* rows, int32_t x, int32_t y) {
    /* A foe on the far side of a room is off-panel, and drawing it there is
     * both wasted work and, in the preview harness, a clipping failure. */
    if(x + FT_SPRITE_W / 2 < 0 || x >= FT_SCREEN_PX_W) return;
    if(y + FT_SPRITE_H / 2 < 0 || y >= FT_SCREEN_PX_H) return;

    /* Halve the 16x16 battle sprite into an 8x8 mask, then draw it the same
     * way as the player: white keyline, black body. A foe standing on the
     * black wall band was otherwise a smudge in it. */
    uint16_t small[FT_SPRITE_H / 2];
    for(int32_t sy = 0; sy < FT_SPRITE_H; sy += 2) {
        const uint16_t bits = rows[sy];
        uint16_t out = 0;

        for(int32_t sx = 0; sx < FT_SPRITE_W; sx += 2) {
            if(bits & (1u << sx)) out = (uint16_t)(out | (1u << (sx / 2)));
        }
        small[sy / 2] = out;
    }

    const int32_t h = FT_SPRITE_H / 2, w = FT_SPRITE_W / 2;

    uint16_t halo[FT_SPRITE_H / 2 + 2];
    for(int32_t i = 0; i < h + 2; i++) halo[i] = 0;

    for(int32_t i = 0; i < h; i++) {
        const uint16_t r = (uint16_t)(small[i] << 1);
        const uint16_t wide = (uint16_t)(r | (uint16_t)(r << 1) | (uint16_t)(r >> 1));

        halo[i]     = (uint16_t)(halo[i] | wide);
        halo[i + 1] = (uint16_t)(halo[i + 1] | wide);
        halo[i + 2] = (uint16_t)(halo[i + 2] | wide);
    }

    canvas_set_color(c, ColorWhite);
    blit_rows16(c, halo, h + 2, x - 1, y - 1, w + 2);
    canvas_set_color(c, ColorBlack);

    blit_rows16(c, small, h, x, y, w);
}

static const uint16_t* foe_sprite(uint32_t attrs) {
    if(attrs & FT_ATTR_AIRBORNE) return FT_SPRITE_BEACON;
    if(attrs & FT_ATTR_ENCRYPTED) return FT_SPRITE_LOCK;
    return FT_SPRITE_PACKET;
}

void ft_overworld_toast(Canvas* canvas, const char* text) {
    if(!text) return;

    canvas_set_font(canvas, FontSecondary);
    const int32_t w = (int32_t)canvas_string_width(canvas, text) + 8;
    const int32_t x = (FT_SCREEN_PX_W - w) / 2;
    const int32_t y = FT_SCREEN_PX_H - 14;

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
            const uint32_t attrs = FT_ENEMIES[roster->foes[m]].attrs;
            const FtPos fp = ft_stepper_pos(&w->foes[i].w[m].mv, FT_FOE_STEP_MS);

            draw_foe(
                canvas, foe_sprite(attrs),
                fp.x - cam.x,
                fp.y - cam.y + (FT_AVATAR_H - FT_TILE_PX) - 2);
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
