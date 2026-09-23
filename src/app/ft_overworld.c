#include "ft_overworld.h"

#include "ft_enemy_art.h"
#include "ft_people.h"
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
/* Echo is built like you, so it is your height: bottom-aligned on its tile
 * the way the avatar is, not the way a 16px foe is. */
static void draw_echo(Canvas* c, int32_t x, int32_t y) {
    const int32_t sx = x * FT_ZOOM;
    const int32_t sy = y * FT_ZOOM + (FT_TILE_PX * FT_ZOOM - FT_HERO_H);

    if(sx + FT_HERO_W < 0 || sx >= FT_PANEL_W) return;
    if(sy + FT_HERO_H < 0 || sy >= FT_PANEL_H) return;

    uint32_t rows[FT_HERO_H];
    for(int32_t i = 0; i < FT_HERO_H; i++) rows[i] = FT_SPRITE_ECHO[i];
    blit_keyed(c, rows, FT_HERO_H, sx, sy, FT_HERO_W);
}

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

/* The mark over something that has just seen you. White-backed so it reads
 * over any tile, and gone the moment the group starts moving. */
static void draw_notice(Canvas* c, int32_t x, int32_t y) {
    const int32_t sx = x * FT_ZOOM + FT_SPRITE_W / 2 - 2;

    /* Clamped downward rather than skipped: against the top wall the mark
     * would otherwise silently not be drawn, and a warning you only get in
     * open ground is not a warning. */
    int32_t sy = y * FT_ZOOM + (FT_TILE_PX * FT_ZOOM - FT_SPRITE_H) - 9;
    if(sy < 1) sy = 1;

    if(sx < 1 || sx + 5 >= FT_PANEL_W) return;
    if(sy + 9 >= FT_PANEL_H) return;

    canvas_set_color(c, ColorWhite);
    canvas_draw_box(c, sx - 1, sy - 1, 7, 11);
    canvas_set_color(c, ColorBlack);
    canvas_draw_box(c, sx + 1, sy, 3, 6);
    canvas_draw_box(c, sx + 1, sy + 7, 3, 2);
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

/* Something said out loud, in a bubble over the head of whoever said it. The
 * sprite is bottom-aligned on its tile, so its top is the tile's top at this
 * zoom; the bubble sits above that, and is pushed back on screen rather than
 * dropped when it would run off an edge — a remark you only see in the middle
 * of the room is not much of a remark. */
static void draw_bark(Canvas* c, const char* text, int32_t x, int32_t y) {
    if(!text || !text[0]) return;

    canvas_set_font(c, FontSecondary);
    const int32_t tw = (int32_t)canvas_string_width(c, text);
    const int32_t bw = tw + 6, bh = 11;

    const int32_t head_x = x * FT_ZOOM + FT_SPRITE_W / 2;
    const int32_t head_y = y * FT_ZOOM + (FT_TILE_PX * FT_ZOOM - FT_SPRITE_H);

    int32_t bx = head_x - bw / 2;
    int32_t by = head_y - bh - 3;
    if(bx < 0) bx = 0;
    if(bx + bw > FT_PANEL_W) bx = FT_PANEL_W - bw;
    if(by < 0) by = 0;
    if(by + bh + 3 > FT_PANEL_H) by = FT_PANEL_H - bh - 3;

    canvas_set_color(c, ColorWhite);
    canvas_draw_box(c, bx, by, (size_t)bw, (size_t)bh);
    canvas_set_color(c, ColorBlack);
    canvas_draw_frame(c, bx, by, (size_t)bw, (size_t)bh);
    canvas_draw_str(c, bx + 3, by + 8, text);

    /* The tail, pointing at whoever said it, when there is room for one. */
    int32_t tx = head_x;
    if(tx < bx + 2) tx = bx + 2;
    if(tx > bx + bw - 3) tx = bx + bw - 3;
    if(by + bh + 2 < FT_PANEL_H) {
        canvas_set_color(c, ColorWhite);
        canvas_draw_line(c, tx - 1, by + bh - 1, tx + 1, by + bh - 1);
        canvas_set_color(c, ColorBlack);
        canvas_draw_line(c, tx - 2, by + bh - 1, tx, by + bh + 1);
        canvas_draw_line(c, tx + 2, by + bh - 1, tx, by + bh + 1);
    }
}

static void render_world(Canvas* canvas, const FtWorld* w, FtPos focus, bool talking);

void ft_overworld_render(Canvas* canvas, const FtWorld* w) {
    render_world(canvas, w, ft_stepper_pos(&w->mv, FT_STEP_MS), false);
}

void ft_overworld_render_talk(Canvas* canvas, const FtWorld* w, int32_t tx, int32_t ty) {
    const FtPos you = ft_stepper_pos(&w->mv, FT_STEP_MS);

    /* Half way between you, and six world pixels lower than that so the
     * camera moves down and the pair of you move up the screen, clear of the
     * box. Worked out so that even stacked one above the other, heads and
     * feet both stay between the top of the panel and the top of the box. */
    FtPos focus = {(you.x + tx * FT_TILE_PX) / 2, (you.y + ty * FT_TILE_PX) / 2 + 6};
    render_world(canvas, w, focus, true);
}

static void render_world(Canvas* canvas, const FtWorld* w, FtPos focus, bool talking) {
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    const FtMap*  map = ft_world_map(w);
    const FtPos   player = ft_stepper_pos(&w->mv, FT_STEP_MS);
    const FtPos   cam = ft_map_camera(map, focus);
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
            const FtTile here = ft_map_tile(map, mx, my);

            /* A way down somebody has shown you. The map still says long
             * grass here — that is how it stayed hidden — so the hole is
             * drawn over it, and the grass is not drawn in front of it. */
            if(ft_world_pit_at(w, mx, my)) {
                blit_rows(canvas, FT_TILE_ART[FT_TILE_ART_PIT], FT_TILE_PX, sx, sy,
                          FT_TILE_PX);
                continue;
            }

            /* Long grass goes down whole, here, as ground. Only its lower
             * half is drawn again over the actors, below. */
            if(here == FT_TILE_TALL_GRASS) {
                blit_rows(canvas, FT_TILE_ART[FT_TILE_TALL_GRASS], FT_TILE_PX, sx, sy,
                          FT_TILE_PX);
                continue;
            }

            if(ft_tile_foreground(here)) {
                /* A canopy or a roof is drawn last, over the actors. What
                 * goes down here is the ground it is hanging over. */
                blit_rows(canvas, FT_TILE_ART[FT_TILE_FLOOR], FT_TILE_PX, sx, sy,
                          FT_TILE_PX);
                continue;
            }

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
        /* Things you can take. Gone for the visit once picked, so a cleared
         * room looks cleared. */
        /* A tree draws nothing of its own: it is map tiles, and whether
         * anything is up in it is something you find out by shaking it. */
        if(room->ents[i].kind == FT_ENT_TREE) continue;

        if(room->ents[i].kind == FT_ENT_CACHE) {
            /* A cache that has been taken is gone. */
            if(!ft_world_bearing(w, i)) continue;

            draw_foe(canvas, FT_SPRITE_CACHE,
                     (int32_t)room->ents[i].tx * FT_TILE_PX - cam.x,
                     (int32_t)room->ents[i].ty * FT_TILE_PX - cam.y);
            continue;
        }

        /* Wren, while she is still waiting to be found. Once she is walking
         * with you she is drawn from the world's escort stepper instead. */
        if(room->ents[i].kind == FT_ENT_WREN) {
            if(!ft_world_wren_present(w, i)) continue;

            draw_foe(canvas,
                     FT_SPRITE_KID,
                     (int32_t)room->ents[i].tx * FT_TILE_PX - cam.x,
                     (int32_t)room->ents[i].ty * FT_TILE_PX - cam.y);
            continue;
        }
        if(room->ents[i].kind == FT_ENT_NPC) {
            /* Standing still on their tile, drawn like a foe so the world has
             * one scale. They never move, so there is no stepper to ask. */
            draw_foe(canvas,
                     ft_person_art((FtQuestId)room->ents[i].roster),
                     (int32_t)room->ents[i].tx * FT_TILE_PX - cam.x,
                     (int32_t)room->ents[i].ty * FT_TILE_PX - cam.y);
            continue;
        }
        if(room->ents[i].kind != FT_ENT_FOE) continue;
        if(!w->foes[i].alive) continue;

        const FtRoster* roster = ft_roster(room->ents[i].roster);

        for(uint8_t m = 0; m < w->foes[i].count && m < FT_MAX_ENEMIES; m++) {
            const FtPos fp = ft_stepper_pos(&w->foes[i].w[m].mv, FT_FOE_STEP_MS);

            /* Both actors are bottom-aligned on their tile by draw_foe and
             * draw_avatar, so no fudge is needed here. */
            draw_foe(canvas, ft_enemy_art(roster->foes[m]), fp.x - cam.x, fp.y - cam.y);
        }

        /* Spotted. The mark sits over the first walker only — three of them
         * with three marks is a row of punctuation, not a warning. */
        if(ft_world_foe_noticing(w, i) && w->foes[i].count > 0u) {
            const FtPos fp = ft_stepper_pos(&w->foes[i].w[0].mv, FT_FOE_STEP_MS);
            draw_notice(canvas, fp.x - cam.x, fp.y - cam.y);
        }
    }

    /* Hale, wherever he has got to. He is drawn from his own stepper rather
     * than a room's entity table, because he is the one person who walks
     * from room to room. */
    if(ft_world_hale_here(w)) {
        const FtPos hp = ft_stepper_pos(&w->hale_mv, ft_world_hale_step_ms(w));
        draw_foe(canvas, FT_SPRITE_GUARD, hp.x - cam.x, hp.y - cam.y);
    }

    /* Echo, across the gap, for as long as it is looking at you. */
    if(ft_world_echo_here(w)) {
        draw_echo(canvas, FT_ECHO_TX * FT_TILE_PX - cam.x, FT_ECHO_TY * FT_TILE_PX - cam.y);
    }

    /* Whoever is walking with you, behind the avatar so the player is never
     * hidden by their own escort. */
    if(w->escort) {
        const FtPos ep = ft_stepper_pos(&w->escort_mv, FT_STEP_MS);
        draw_foe(canvas, FT_SPRITE_KID, ep.x - cam.x, ep.y - cam.y);
    }

    const bool moving = ft_world_moving(w);
    const int32_t bob = moving ? (int32_t)((w->walk_ms / 150u) % 2u) : 0;
    const int32_t ax = player.x - cam.x;
    const int32_t ay = player.y - cam.y;

    draw_avatar(canvas, ax, ay, w->facing, bob);

    /* --- the foreground ---
     *
     * Canopies and roofs, drawn last so everything walks *behind* them. The
     * art is gappy and the blit only sets black pixels, so whoever is under
     * a tree shows through the leaves instead of being swallowed by them.
     * This is the only thing in the game that gives the overworld depth. */
    for(int32_t ty = 0; ty <= FT_VIEW_H; ty++) {
        for(int32_t tx = 0; tx <= FT_VIEW_W; tx++) {
            const int32_t mx = first_tx + tx;
            const int32_t my = first_ty + ty;

            const FtTile t = ft_map_tile(map, mx, my);
            if(!ft_tile_foreground(t)) continue;
            if(ft_world_pit_at(w, mx, my)) continue;

            /* Knee-high: the bottom half of the blades, over the legs of
             * whoever is standing in it. The top half is already down. */
            if(t == FT_TILE_TALL_GRASS) {
                const int32_t half = FT_TILE_PX / 2;
                blit_rows(canvas, FT_TILE_ART[FT_TILE_TALL_GRASS] + half, half,
                          tx * FT_TILE_PX - off_x, ty * FT_TILE_PX - off_y + half,
                          FT_TILE_PX);
                continue;
            }

            /* The art index, not the raw tile. This drew FT_TILE_ART[t] for a
             * while, which is the square leaf: the rounded canopy corners
             * showed up in every preview (which draws the whole map through
             * ft_map_art_index) and in no game, because the one pass that
             * draws leaves on the device skipped the lookup. */
            /* A tree being shaken sways, crown only: the trunk stays put. */
            const int32_t sway = ft_world_shake_offset(w, mx, my);

            blit_rows(canvas, FT_TILE_ART[ft_map_art_index(map, mx, my)], FT_TILE_PX,
                      tx * FT_TILE_PX - off_x + sway, ty * FT_TILE_PX - off_y, FT_TILE_PX);
        }
    }

    /* Somebody saying something out loud, over their head. */
    if(!talking && w->bark_ms > 0u) {
        const char* line = ft_quest_bark(w->bark);

        if(w->bark_who == FT_BARK_BY_HALE && ft_world_hale_here(w)) {
            const FtPos hp = ft_stepper_pos(&w->hale_mv, ft_world_hale_step_ms(w));
            draw_bark(canvas, line, hp.x - cam.x, hp.y - cam.y);
        } else if(w->bark_who == FT_BARK_BY_WREN && w->escort) {
            const FtPos ep = ft_stepper_pos(&w->escort_mv, FT_STEP_MS);
            draw_bark(canvas, line, ep.x - cam.x, ep.y - cam.y);
        } else if(w->bark_who == FT_BARK_BY_TERMINAL || w->bark_who == FT_BARK_BY_ECHO) {
            draw_bark(canvas, line, (int32_t)w->bark_tx * FT_TILE_PX - cam.x,
                      (int32_t)w->bark_ty * FT_TILE_PX - cam.y);
        }
    }

    /* Area name, in a cleared strip so it stays legible over any tile. It
     * retires after a couple of seconds rather than occupying the corner for
     * the whole visit. */
    const char* banner = ft_world_banner(w);
    if(!talking && banner && w->area_ms < FT_AREA_BANNER_MS) {
        canvas_set_font(canvas, FontSecondary);
        const int32_t w = (int32_t)canvas_string_width(canvas, banner) + 6;

        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, 0, 0, (size_t)w, 10);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_frame(canvas, 0, 0, (size_t)w, 10);
        canvas_draw_str(canvas, 3, 7, banner);
    }
}
