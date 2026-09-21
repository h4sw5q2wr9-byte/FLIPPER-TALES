#include "ft_overworld.h"

#include "ft_sprites.h"
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

    /* XOR, not a knocked-out halo. The halo cleared a white box around the
     * avatar, which swallowed anything it stood next to; inverting keeps the
     * player readable against both bare floor and solid black without erasing
     * the scenery. */
    canvas_set_color(c, ColorXOR);
    blit_rows(c, rows, FT_AVATAR_H, x, y, FT_AVATAR_W);
    canvas_set_color(c, ColorBlack);
}

/* Foes standing in the room. Drawn at half the battle sprite's size by
 * sampling every other pixel — a 16x16 enemy would dwarf an 8x12 player. */
static void draw_foe(Canvas* c, const uint16_t* rows, int32_t x, int32_t y) {
    /* A foe on the far side of a room is off-panel, and drawing it there is
     * both wasted work and, in the preview harness, a clipping failure. */
    if(x + FT_SPRITE_W / 2 < 0 || x >= FT_SCREEN_PX_W) return;
    if(y + FT_SPRITE_H / 2 < 0 || y >= FT_SCREEN_PX_H) return;

    for(int32_t sy = 0; sy < FT_SPRITE_H; sy += 2) {
        const uint16_t bits = rows[sy];
        if(!bits) continue;

        const int32_t py = y + sy / 2;
        if(py < 0 || py >= FT_SCREEN_PX_H) continue;

        for(int32_t sx = 0; sx < FT_SPRITE_W; sx += 2) {
            const int32_t px = x + sx / 2;
            if(px < 0 || px >= FT_SCREEN_PX_W) continue;
            if(bits & (1u << sx)) canvas_draw_dot(c, px, py);
        }
    }
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
     * A group walks as a group: if the roster is three, three sprites follow
     * the leader. What you see is what you are about to fight, which is the
     * whole point of making encounters visible. */
    static const int8_t FOLLOW[FT_MAX_ENEMIES][2] = {
        {0, 0}, {-6, 3}, {6, 3},
    };

    for(uint8_t i = 0; i < room->ent_count && i < FT_MAX_ROOM_ENTS; i++) {
        if(room->ents[i].kind != FT_ENT_FOE) continue;
        if(!w->foes[i].alive) continue;

        const FtRoster* roster = ft_roster(room->ents[i].roster);
        const FtPos fp = ft_stepper_pos(&w->foes[i].mv, FT_FOE_STEP_MS);

        for(uint8_t m = 0; m < roster->count && m < FT_MAX_ENEMIES; m++) {
            const uint32_t attrs = FT_ENEMIES[roster->foes[m]].attrs;

            draw_foe(
                canvas, foe_sprite(attrs),
                fp.x - cam.x + FOLLOW[m][0],
                fp.y - cam.y + FOLLOW[m][1] + (FT_AVATAR_H - FT_TILE_PX) - 2);
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
