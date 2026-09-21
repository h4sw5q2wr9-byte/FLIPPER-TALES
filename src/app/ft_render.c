#include "ft_render.h"

#include "../core/ft_tutorial.h"
#include "ft_sprites.h"

#include <stdio.h>
#include <string.h>

/* ---- Text helpers ---------------------------------------------------- */

/* Draw a string, truncating it to fit. Every label on this screen comes from
 * data that could be longer than the space it has, so nothing is drawn without
 * a measured budget — that is what stops content running off the panel. */
static void draw_clipped(Canvas* c, int32_t x, int32_t y, const char* s, int32_t max_w) {
    if(max_w <= 0 || !s || !*s) return;

    if((int32_t)canvas_string_width(c, s) <= max_w) {
        canvas_draw_str(c, x, y, s);
        return;
    }

    char buf[32];
    size_t n = strlen(s);
    if(n >= sizeof(buf)) n = sizeof(buf) - 1;

    while(n > 0) {
        memcpy(buf, s, n);
        buf[n] = '\0';
        if((int32_t)canvas_string_width(c, buf) <= max_w) break;
        n--;
    }
    canvas_draw_str(c, x, y, buf);
}

/* Centre a string, nudged so it can never overhang either edge. */
static void draw_centred(Canvas* c, int32_t cx, int32_t y, const char* s) {
    const int32_t w = (int32_t)canvas_string_width(c, s);
    int32_t x = cx - w / 2;

    if(x + w > FT_SCREEN_W - 1) x = FT_SCREEN_W - 1 - w;
    if(x < 1) x = 1;

    canvas_draw_str(c, x, y, s);
}

/* Interior width of a bar for the given value, clamped at BOTH ends. Without
 * the upper clamp a value above max draws straight through the frame. */
static int32_t ft_bar_fill(int32_t value, int32_t max, int32_t bar_w) {
    const int32_t inner = bar_w - 2;
    if(value <= 0 || max <= 0 || inner <= 0) return 0;

    int32_t fill = (value * inner) / max;
    if(fill < 1) fill = 1;
    if(fill > inner) fill = inner;

    return fill;
}

/* A hatched region: on a panel with no colour, texture is the only way to say
 * "this is different but not solid" (DESIGN.md 5). */
static void hatch(Canvas* c, int32_t x, int32_t y, int32_t w, int32_t h) {
    for(int32_t j = 0; j < h; j++) {
        for(int32_t i = (j % 2); i < w; i += 2) canvas_draw_dot(c, x + i, y + j);
    }
}

/* ---- Header ---------------------------------------------------------- */

static void draw_header(Canvas* canvas, const FtEncounter* e) {
    /* Name whoever matters right now: the foe you are aiming at while
     * choosing, the foe swinging at you once one is. */
    const bool their_turn =
        (e->phase == FT_PHASE_TELEGRAPH || e->phase == FT_PHASE_IMPACT);
    const uint8_t who = their_turn ? e->acting_foe : ft_encounter_target(e);
    const FtEnemy* en = ft_encounter_foe(e, who);

    canvas_set_font(canvas, FontSecondary);

    /* Tags are laid out from the right using measured widths, and the name
     * gets whatever is left over. */
    int32_t right = FT_SCREEN_W - 2;

    char tag[10];
    if(en->attrs & FT_ATTR_ENCRYPTED) {
        strcpy(tag, "ENC");
        right -= (int32_t)canvas_string_width(canvas, tag);
        canvas_draw_str(canvas, right, 7, tag);
        right -= 3;
    }
    if(en->attrs & FT_ATTR_AIRBORNE) {
        strcpy(tag, "AIR");
        right -= (int32_t)canvas_string_width(canvas, tag);
        canvas_draw_str(canvas, right, 7, tag);
        right -= 3;
    }
    if(en->shielded > 0) {
        snprintf(tag, sizeof(tag), "SH%d", (int)en->shielded);
        right -= (int32_t)canvas_string_width(canvas, tag);
        canvas_draw_str(canvas, right, 7, tag);
        right -= 3;
    }

    draw_clipped(canvas, 2, 7, en->name, right - 4);
    canvas_draw_line(canvas, 0, FT_HEADER_H, FT_SCREEN_W - 1, FT_HEADER_H);
}

/* ---- Arena ----------------------------------------------------------- */

/* Blit a 16x16 sprite. Bit n of each row is column n from the left.
 *
 * Consecutive set bits are emitted as one box rather than a dot per pixel:
 * these sprites are mostly solid runs, so this turns ~250 canvas calls per
 * sprite into roughly 30. Draw cost matters here — the GUI thread is shared
 * with input dispatch, and burying it is what wedged the app. */
static void draw_sprite(Canvas* c, const uint16_t* rows, int32_t x, int32_t y) {
    for(int32_t sy = 0; sy < FT_SPRITE_H; sy++) {
        uint16_t bits = rows[sy];
        if(!bits) continue;

        int32_t sx = 0;
        while(sx < FT_SPRITE_W) {
            if(!(bits & (1u << sx))) {
                sx++;
                continue;
            }
            int32_t run = 0;
            while(sx + run < FT_SPRITE_W && (bits & (1u << (sx + run)))) run++;

            canvas_draw_box(c, x + sx, y + sy, (size_t)run, 1);
            sx += run;
        }
    }
}

static void draw_player(Canvas* c, int32_t x, int32_t y, bool hurt) {
    draw_sprite(c, FT_SPRITE_PLAYER, x, y);

    /* While Charge is still draining, the screen goes dark: the sprite reads
     * its own state rather than relying on the bar alone. */
    if(hurt) {
        canvas_set_color(c, ColorXOR);
        canvas_draw_box(c, x + 4, y + 4, 8, 6);
        canvas_set_color(c, ColorBlack);
    }
}

static const uint16_t* enemy_sprite(uint32_t attrs) {
    if(attrs & FT_ATTR_AIRBORNE) return FT_SPRITE_BEACON;
    if(attrs & FT_ATTR_ENCRYPTED) return FT_SPRITE_LOCK;
    return FT_SPRITE_PACKET;
}

/* Where each foe stands. Spread to fill the right of the arena so a lone foe
 * still looks deliberate rather than crowded into a corner. */
static int32_t foe_x(uint8_t i, uint8_t count) {
    /* Grouped tightly on the right rather than spread across the arena: a
     * scattered row reads as three separate fights instead of one crowd, and
     * leaves the player marooned on the far side. */
    static const int32_t LAYOUT[FT_MAX_ENEMIES][FT_MAX_ENEMIES] = {
        {104, 0, 0},
        {84, 106, 0},
        {66, 86, 106},
    };
    if(count == 0u || count > FT_MAX_ENEMIES) count = 1u;
    if(i >= count) i = 0;

    return LAYOUT[count - 1u][i];
}

/* A small marker over whoever a single-target action would hit. */
static void draw_target_caret(Canvas* c, int32_t x, int32_t y) {
    canvas_draw_line(c, x + 5, y, x + 10, y);
    canvas_draw_line(c, x + 6, y + 1, x + 9, y + 1);
    canvas_draw_dot(c, x + 7, y + 2);
    canvas_draw_dot(c, x + 8, y + 2);
}

/* A radio chevron, the visual vocabulary for anything broadcast. */
static void draw_chevron(Canvas* c, int32_t x, int32_t y, int32_t dir, int32_t size) {
    /* Apex at x, opening away from the direction of travel: the arms widen as
     * they trail behind, so the shape points where the wave is going. */
    for(int32_t i = 0; i <= size; i++) {
        canvas_draw_dot(c, x - dir * i, y - (i + 1));
        canvas_draw_dot(c, x - dir * i, y + (i + 1));
    }
    canvas_draw_dot(c, x, y);
}

/* Three chevrons leaving the emitter and crossing the gap, staggered so they
 * read as a train of waves rather than one moving blob. */
static void draw_broadcast(
    Canvas* c, int32_t from_x, int32_t to_x, int32_t y, int32_t dir, uint8_t t) {
    if(t >= FT_ANIM_STRIKE) return;

    for(int32_t w = 0; w < 3; w++) {
        const int32_t lead = (int32_t)t - FT_ANIM_EMIT - w * 34;
        if(lead <= 0) continue;

        const int32_t span = FT_ANIM_STRIKE - FT_ANIM_EMIT;
        int32_t p = (lead * 100) / span;
        if(p > 100) p = 100;

        const int32_t x = from_x + ((to_x - from_x) * p) / 100;
        draw_chevron(c, x, y, dir, 2 + w);
    }
}

/* Contact: a tight field crackling between the two sprites. */
static void draw_contact_spark(Canvas* c, int32_t x, int32_t y, uint8_t t) {
    if(t < FT_ANIM_EMIT || t > FT_ANIM_STRIKE) return;

    const int32_t n = 3;
    for(int32_t i = 0; i < n; i++) {
        const int32_t dy = (i - 1) * 4;
        canvas_draw_line(c, x, y + dy, x + 4, y + dy + ((i % 2) ? 2 : -2));
    }
    canvas_draw_box(c, x + 1, y - 1, 3, 3);
}

/* Two-pixel judder for whoever just took damage. */
static int32_t shake_px(uint8_t t) {
    if(t < FT_ANIM_STRIKE || t >= FT_ANIM_RECOVER) return 0;
    return (((t - FT_ANIM_STRIKE) / 12u) % 2u) ? 2 : -2;
}

/* Attacker travel: out during the emit window, back during recovery. */
static int32_t lunge_px(uint8_t t, int32_t reach) {
    if(t < FT_ANIM_WINDUP) return -(int32_t)t / 24;                 /* wind back */
    if(t < FT_ANIM_STRIKE) {
        return (reach * (int32_t)(t - FT_ANIM_WINDUP)) / (FT_ANIM_STRIKE - FT_ANIM_WINDUP);
    }
    if(t < FT_ANIM_RECOVER) {
        return (reach * (int32_t)(FT_ANIM_RECOVER - t)) / (FT_ANIM_RECOVER - FT_ANIM_STRIKE);
    }
    return 0;
}

/* The antenna charging before a broadcast leaves the player. */
static void draw_antenna_charge(Canvas* c, int32_t x, int32_t y, uint8_t t) {
    if(t >= FT_ANIM_EMIT) return;
    if(((t / 14u) % 2u) == 0u) return;

    canvas_draw_box(c, x + 6, y - 3, 4, 3);
}

static void draw_arena(Canvas* canvas, const FtEncounter* e) {
    const int32_t floor_y = FT_ARENA_Y + 18;
    const uint8_t t = ft_encounter_anim_progress(e);
    const uint8_t count = e->foe_count;

    for(int32_t x = 2; x < FT_SCREEN_W - 2; x += 3) canvas_draw_dot(canvas, x, floor_y);

    int32_t px = 4;
    int32_t py = floor_y - 16;
    const uint8_t tgt = ft_encounter_target(e);

    /* --- the player's action --- */
    if(e->phase == FT_PHASE_RESULT) {
        const FtAction2 act = (FtAction2)e->menu_index;
        const bool broadcast = ft_encounter_action_is_broadcast(e, act);
        const bool attacked =
            (act == FT_ACTION_BROADCAST || act == FT_ACTION_CONTACT ||
             act == FT_ACTION_SIGNAL) &&
            e->last_player_hit.outcome != FT_HIT_MISSED;

        if(attacked && broadcast) {
            draw_antenna_charge(canvas, px, py, t);
            draw_broadcast(canvas, 24, 120, floor_y - 9, 1, t);
        } else if(attacked) {
            px += lunge_px(t, foe_x(tgt, count) - 22);
            draw_contact_spark(canvas, px + 17, floor_y - 8, t);
        } else if(act == FT_ACTION_FOCUS) {
            draw_antenna_charge(canvas, px, py, t);
        }
    }

    /* --- the acting foe --- */
    int32_t foe_shift = 0;
    if(e->phase == FT_PHASE_IMPACT) {
        const FtAttack* atk = ft_encounter_incoming(e);
        const int32_t ax = foe_x(e->acting_foe, count);

        if(atk && atk->delivery == FT_DELIVERY_CONTACT) {
            foe_shift = -lunge_px(t, ax - 24);
        } else if(atk) {
            draw_broadcast(canvas, ax - 4, 20, floor_y - 9, -1, t);
        }
        if(e->last_enemy_hit.damage > 0) px += shake_px(t);
        if(e->last_enemy_hit.captured) draw_broadcast(canvas, 20, 44, floor_y - 9, 1, t);
    }

    if(e->phase == FT_PHASE_MENU && ((e->phase_ms / 600u) % 2u)) py -= 1;

    draw_player(canvas, px, py, ft_roll_active(&e->roll));

    /* --- the row of foes --- */
    for(uint8_t i = 0; i < count; i++) {
        if(!ft_encounter_foe_alive(e, i)) continue;

        int32_t x = foe_x(i, count);
        int32_t y = py;

        if(e->phase == FT_PHASE_IMPACT && i == e->acting_foe) x += foe_shift;
        if(e->phase == FT_PHASE_RESULT && e->foe_hit_valid[i] && e->foe_hits[i].damage > 0) {
            x += shake_px(t);
        }
        if(e->phase == FT_PHASE_MENU && ((e->phase_ms / 700u) % 2u)) y -= 1;

        draw_sprite(canvas, enemy_sprite(FT_ENEMIES[e->foes[i].id].attrs), x, y);

        /* Health, directly beneath each foe. */
        const int32_t bw = 16;
        canvas_draw_frame(canvas, x, floor_y + 2, (size_t)bw, 4);
        const int32_t fill = ft_bar_fill(e->foes[i].charge, e->foes[i].charge_max, bw);
        if(fill > 0) canvas_draw_box(canvas, x + 1, floor_y + 3, (size_t)fill, 2);

        if(e->phase == FT_PHASE_MENU && count > 1u && i == tgt) {
            draw_target_caret(canvas, x, FT_ARENA_Y);
        }
    }
}

/* ---- Skill checks ---------------------------------------------------- */

#define TRACK_X 4
#define TRACK_W 120
#define TRACK_Y (FT_ARENA_Y + 8)
#define TRACK_H 15

/* The cursor must stay visible over both empty track and solid zones, so it is
 * XORed across the track and given plain black flags above and below. */
static void draw_cursor(Canvas* c, int32_t x) {
    if(x < TRACK_X) x = TRACK_X;
    if(x > TRACK_X + TRACK_W - 3) x = TRACK_X + TRACK_W - 3;

    canvas_set_color(c, ColorXOR);
    canvas_draw_box(c, x, TRACK_Y + 1, 3, TRACK_H - 2);
    canvas_set_color(c, ColorBlack);

    canvas_draw_box(c, x - 1, TRACK_Y - 3, 5, 3);
    canvas_draw_box(c, x - 1, TRACK_Y + TRACK_H, 5, 3);
}

static int32_t ms_to_px(uint32_t ms, uint32_t window) {
    if(window == 0u) return 0;
    return (int32_t)((ms * (uint32_t)TRACK_W) / window);
}

/* "Time your strike": hit the solid block in the middle. */
/* During the ready beat the track and its zones are already drawn; this marks
 * the start line and counts the player in. */
static void draw_ready_overlay(Canvas* canvas, const FtEncounter* e) {
    canvas_draw_box(canvas, TRACK_X, TRACK_Y + 1, 3, TRACK_H - 2);

    /* Three pips that empty as the beat runs out. */
    const int32_t lit = 3 - (int32_t)((e->phase_ms * 3u) / FT_READY_MS);
    for(int32_t i = 0; i < 3; i++) {
        const int32_t px = FT_SCREEN_W / 2 - 10 + i * 8;
        if(i < lit) {
            canvas_draw_box(canvas, px, TRACK_Y + 5, 5, 5);
        } else {
            canvas_draw_frame(canvas, px, TRACK_Y + 5, 5, 5);
        }
    }
}

static void draw_strike_check(Canvas* canvas, const FtEncounter* e) {
    canvas_set_font(canvas, FontSecondary);
    draw_centred(canvas, FT_SCREEN_W / 2, FT_ARENA_Y + 4,
                 ft_encounter_in_ready(e) ? "GET READY" : "TIME YOUR STRIKE");

    canvas_draw_frame(canvas, TRACK_X, TRACK_Y, TRACK_W, TRACK_H);

    const int32_t mid = TRACK_X + TRACK_W / 2;
    const int32_t good = ms_to_px(FT_BAND_GOOD_MS, FT_ACTION_WINDOW_MS);
    const int32_t best = ms_to_px(FT_BAND_EXCELLENT_MS, FT_ACTION_WINDOW_MS);

    /* Widening bands, drawn from loosest to tightest. */
    hatch(canvas, mid - good, TRACK_Y + 1, good * 2, TRACK_H - 2);
    canvas_draw_box(canvas, mid - best, TRACK_Y + 1, (size_t)(best * 2), TRACK_H - 2);

    if(ft_encounter_in_ready(e)) {
        draw_ready_overlay(canvas, e);
        return;
    }

    draw_cursor(canvas, TRACK_X + ms_to_px(ft_encounter_sweep_ms(e), FT_ACTION_WINDOW_MS));

    if(e->action_pressed) draw_centred(canvas, FT_SCREEN_W / 2, FT_ARENA_Y + 25, "LOCKED IN");
}

/* The guard check. Only the zones the player can actually hit are drawn, so
 * the attack class teaches itself: GUARDED loses its capture block, and
 * UNDODGEABLE has no zones at all. */
static void draw_guard_check(Canvas* canvas, const FtEncounter* e) {
    const FtAttack* atk = ft_encounter_incoming(e);
    if(atk == NULL) return;

    canvas_set_font(canvas, FontSecondary);

    const char* title;
    switch(atk->klass) {
    case FT_CLASS_UNDODGEABLE: title = "UNDODGEABLE"; break;
    case FT_CLASS_GUARDED:     title = "GUARDED - NO CAPTURE"; break;
    default:                   title = "INCOMING"; break;
    }
    draw_centred(canvas, FT_SCREEN_W / 2, FT_ARENA_Y + 4, title);

    canvas_draw_frame(canvas, TRACK_X, TRACK_Y, TRACK_W, TRACK_H);

    if(atk->klass == FT_CLASS_UNDODGEABLE) {
        /* Nothing to aim at: say so rather than leaving an empty track. */
        canvas_draw_line(canvas, TRACK_X + 1, TRACK_Y + 1, TRACK_X + TRACK_W - 2,
                         TRACK_Y + TRACK_H - 2);
        canvas_draw_line(canvas, TRACK_X + 1, TRACK_Y + TRACK_H - 2, TRACK_X + TRACK_W - 2,
                         TRACK_Y + 1);
    } else {
        const bool hard = e->fx.hard_mode;
        const uint32_t jam_ms = hard ? FT_JAM_WINDOW_MS / 2u : FT_JAM_WINDOW_MS;
        const uint32_t cap_ms = hard ? FT_CAPTURE_WINDOW_MS / 2u : FT_CAPTURE_WINDOW_MS;

        const int32_t jam_w = ms_to_px(jam_ms, FT_TELEGRAPH_MS);
        const int32_t cap_w = ms_to_px(cap_ms, FT_TELEGRAPH_MS);
        const int32_t right = TRACK_X + TRACK_W - 1;

        hatch(canvas, right - jam_w, TRACK_Y + 1, jam_w, TRACK_H - 2);

        /* Only NORMAL attacks can be captured, so only they get the block. */
        if(atk->klass == FT_CLASS_NORMAL) {
            canvas_draw_box(canvas, right - cap_w, TRACK_Y + 1, (size_t)cap_w, TRACK_H - 2);
        }
    }

    if(ft_encounter_in_ready(e)) {
        draw_ready_overlay(canvas, e);
        return;
    }

    draw_cursor(canvas, TRACK_X + ms_to_px(ft_encounter_sweep_ms(e), FT_TELEGRAPH_MS));
}

/* ---- Popups ---------------------------------------------------------- */

/* Sized to its own content, then clamped, so a long line can never overflow. */
static void draw_popup(Canvas* canvas, const char* top, const char* bottom) {
    canvas_set_font(canvas, FontSecondary);

    int32_t w = (int32_t)canvas_string_width(canvas, top);
    if(bottom) {
        const int32_t bw = (int32_t)canvas_string_width(canvas, bottom);
        if(bw > w) w = bw;
    }
    w += 10;
    if(w > FT_SCREEN_W - 8) w = FT_SCREEN_W - 8;

    const int32_t h = bottom ? 21 : 13;
    const int32_t x = (FT_SCREEN_W - w) / 2;
    const int32_t y = FT_ARENA_Y + 2;

    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, x, y, (size_t)w, (size_t)h);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_frame(canvas, x, y, (size_t)w, (size_t)h);

    /* A one-pixel drop shadow: cheap depth on a panel with no colour. */
    canvas_draw_line(canvas, x + 2, y + h, x + w, y + h);
    canvas_draw_line(canvas, x + w, y + 2, x + w, y + h);

    draw_centred(canvas, FT_SCREEN_W / 2, y + 8, top);
    if(bottom) draw_centred(canvas, FT_SCREEN_W / 2, y + 17, bottom);
}

static const char* rating_text(FtRating r) {
    switch(r) {
    case FT_RATING_EXCELLENT: return "EXCELLENT!";
    case FT_RATING_AMAZING:   return "AMAZING!";
    case FT_RATING_GREAT:     return "GREAT!";
    case FT_RATING_GOOD:      return "GOOD";
    case FT_RATING_NICE:      return "NICE";
    default:                  return "MISS";
    }
}

static void draw_player_result(Canvas* canvas, const FtEncounter* e) {
    const FtHitResult* r = &e->last_player_hit;
    char detail[24];

    switch(r->outcome) {
    case FT_HIT_LOCKED:
        draw_popup(canvas, "NO EFFECT", r->ram_refund ? "encrypted" : "out of reach");
        return;
    case FT_HIT_DEFLECTED:
        draw_popup(canvas, "DEFLECTED", "shield held");
        return;
    case FT_HIT_MISSED:
        draw_popup(canvas, "MISS", NULL);
        return;
    default:
        break;
    }

    if(r->damage > 0) {
        snprintf(detail, sizeof(detail), "-%d", (int)r->damage);
        draw_popup(canvas, rating_text(e->last_rating), detail);
    } else {
        draw_popup(canvas, "NO DAMAGE", NULL);
    }
}

static void draw_enemy_result(Canvas* canvas, const FtEncounter* e) {
    const FtHitResult* r = &e->last_enemy_hit;
    char detail[24];

    if(r->captured) {
        draw_popup(canvas, "CAPTURED!",
                   e->last_capture_was_new ? "signal stored" : "already held");
        return;
    }
    if(e->last_guard == FT_GUARD_JAM) {
        snprintf(detail, sizeof(detail), "jammed  -%d", (int)r->damage);
        draw_popup(canvas, "JAM", detail);
        return;
    }
    if(r->damage > 0) {
        snprintf(detail, sizeof(detail), "-%d", (int)r->damage);
        draw_popup(canvas, "HIT", detail);
    } else {
        draw_popup(canvas, "NO DAMAGE", NULL);
    }
}

/* The coach line during the menu, where the action row is already full. Sits
 * low in the arena so the characters' faces stay visible behind it. */
static void draw_coach_callout(Canvas* canvas, const char* hint) {
    canvas_set_font(canvas, FontSecondary);

    int32_t w = (int32_t)canvas_string_width(canvas, hint) + 8;
    if(w > FT_SCREEN_W - 6) w = FT_SCREEN_W - 6;

    const int32_t x = (FT_SCREEN_W - w) / 2;
    const int32_t y = FT_ARENA_Y + 14;

    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, x, y, (size_t)w, 11);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_frame(canvas, x, y, (size_t)w, 11);
    canvas_draw_line(canvas, x + 2, y + 11, x + w, y + 11);

    draw_centred(canvas, FT_SCREEN_W / 2, y + 8, hint);
}

/* ---- Status ---------------------------------------------------------- */

static void draw_status(Canvas* canvas, const FtEncounter* e) {
    canvas_set_font(canvas, FontSecondary);
    char buf[12];

    canvas_draw_line(canvas, 0, FT_STATUS_Y - 1, FT_SCREEN_W - 1, FT_STATUS_Y - 1);

    canvas_draw_str(canvas, 2, FT_STATUS_Y + 7, "CHG");

    const int32_t bx = 22, bw = 38;
    canvas_draw_frame(canvas, bx, FT_STATUS_Y + 1, (size_t)bw, 7);
    {
        const int32_t fill = ft_bar_fill(e->roll.current, e->stats.charge_max, bw);
        if(fill > 0) canvas_draw_box(canvas, bx + 1, FT_STATUS_Y + 2, (size_t)fill, 5);

        /* Quarter ticks turn the bar into a gauge you can read at a glance
         * instead of a featureless slab. */
        for(int32_t q = 1; q < 4; q++) {
            const int32_t tx = bx + (bw * q) / 4;
            canvas_set_color(canvas, (tx - bx - 1 < fill) ? ColorWhite : ColorBlack);
            canvas_draw_dot(canvas, tx, FT_STATUS_Y + 4);
            canvas_set_color(canvas, ColorBlack);
        }
    }

    snprintf(buf, sizeof(buf), "%d", (int)e->roll.current);
    canvas_draw_str(canvas, 62, FT_STATUS_Y + 7, buf);

    snprintf(buf, sizeof(buf), "R%d", (int)e->stats.ram);
    canvas_draw_str(canvas, 77, FT_STATUS_Y + 7, buf);

    /* One letter, not three: the pips beside it are self-explanatory once
     * they start filling, and the width is needed for four of them. */
    canvas_draw_str(canvas, 92, FT_STATUS_Y + 7, "S");

    const uint8_t bars = ft_signal_bars(&e->signal);
    for(uint8_t i = 0; i < e->signal.max_bars && i < 4u; i++) {
        const int32_t sx = 100 + i * 7;
        if(e->signal.locked) {
            hatch(canvas, sx, FT_STATUS_Y + 1, 5, 7);
            canvas_draw_frame(canvas, sx, FT_STATUS_Y + 1, 5, 7);
        } else if(i < bars) {
            canvas_draw_box(canvas, sx, FT_STATUS_Y + 1, 5, 7);
        } else {
            canvas_draw_frame(canvas, sx, FT_STATUS_Y + 1, 5, 7);
        }
    }
}

/* ---- Action row ------------------------------------------------------ */

static const char* action_label(FtAction2 a) {
    switch(a) {
    case FT_ACTION_BROADCAST: return "SUB";
    case FT_ACTION_CONTACT:   return "NFC";
    case FT_ACTION_DEFEND:    return "DEF";
    case FT_ACTION_FOCUS:     return "FOC";
    case FT_ACTION_SIGNAL:    return "SIG";
    default:                  return "?";
    }
}

/* What the highlighted action actually does. Five three-letter buttons are
 * unreadable on their own — this row is why Defend and Focus stop looking
 * like filler. */
static const char* action_desc(const FtEncounter* e, FtAction2 a) {
    /* A refusal is more useful than a description. */
    const char* blocked = ft_encounter_action_block(e, a);
    if(blocked) return blocked;

    switch(a) {
    case FT_ACTION_BROADCAST: return "All foes, weaker.";
    case FT_ACTION_CONTACT:   return "One foe, strong.";
    case FT_ACTION_DEFEND:    return "Shield 2, +1 RAM.";
    case FT_ACTION_FOCUS:     return "Charge the S bar.";
    case FT_ACTION_SIGNAL:    return "Replay a capture.";
    default:                  return "";
    }
}

static void draw_menu(Canvas* canvas, const FtEncounter* e) {
    canvas_set_font(canvas, FontSecondary);

    const int32_t cell = 25;

    for(uint8_t i = 0; i < FT_ACTION_COUNT; i++) {
        const int32_t x = 1 + (int32_t)i * cell;
        const bool selected = (i == e->menu_index);
        const bool available = ft_encounter_action_available(e, (FtAction2)i);
        const char* label = action_label((FtAction2)i);

        if(selected) {
            canvas_draw_box(canvas, x, FT_ACTION_Y, (size_t)(cell - 1), 9);
            canvas_set_color(canvas, ColorWhite);
        }

        const int32_t lw = (int32_t)canvas_string_width(canvas, label);
        const int32_t lx = x + (cell - 1 - lw) / 2;
        canvas_draw_str(canvas, lx, FT_ACTION_Y + 7, label);

        /* Struck through rather than hidden: the option stays visible and the
         * row below says why it is refused. */
        if(!available) canvas_draw_line(canvas, lx, FT_ACTION_Y + 4, lx + lw, FT_ACTION_Y + 4);

        if(selected) canvas_set_color(canvas, ColorBlack);
    }

    draw_centred(canvas, FT_SCREEN_W / 2, FT_ACTION_Y + 16,
                 action_desc(e, (FtAction2)e->menu_index));
}

static void draw_prompt(Canvas* canvas, const char* s) {
    canvas_set_font(canvas, FontSecondary);
    draw_centred(canvas, FT_SCREEN_W / 2, FT_ACTION_Y + 12, s);
}

/* ---- Help ------------------------------------------------------------ */

void ft_render_help(Canvas* canvas, uint8_t page) {
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);

    if(page >= FT_HELP_PAGES) page = 0;

    static const char* const TITLES[FT_HELP_PAGES] = {
        "1/4  CONTROLS",
        "2/4  YOUR STRIKE",
        "3/4  THEIR TURN",
        "4/4  MODULES",
    };

    /* Four lines per page, 21 characters each: the panel's width budget. */
    static const char* const BODY[FT_HELP_PAGES][4] = {
        {
            "LEFT/RIGHT: action.",
            "UP/DOWN: target.",
            "The line below says",
            "what each one does.",
        },
        {
            "A bar sweeps. Wait",
            "for the pips, then",
            "tap OK in the black",
            "block. Centre = x2.",
        },
        {
            "Tap OK as the cursor",
            "reaches the far end.",
            "Dots = jam (half).",
            "Solid = capture (0).",
        },
        {
            "SIG replays a kept",
            "attack, costs 1 bar.",
            "AIR blocks NFC.",
            "ENC blocks SUB.",
        },
    };

    draw_centred(canvas, FT_SCREEN_W / 2, 7, TITLES[page]);
    canvas_draw_line(canvas, 0, 9, FT_SCREEN_W - 1, 9);

    for(int32_t i = 0; i < 4; i++) {
        canvas_draw_str(canvas, 2, 20 + i * 9, BODY[page][i]);
    }

    draw_centred(canvas, FT_SCREEN_W / 2, 62,
                 (page + 1 < FT_HELP_PAGES) ? "RIGHT: more  OK: go" :
                                              "LEFT: back   OK: go");
}

/* ---- Entry ----------------------------------------------------------- */

void ft_render_battle(Canvas* canvas, const FtEncounter* e) {
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    if(e->phase == FT_PHASE_WIN || e->phase == FT_PHASE_LOSE) {
        const bool won = (e->phase == FT_PHASE_WIN);
        char detail[26];

        canvas_set_font(canvas, FontSecondary);
        draw_centred(canvas, FT_SCREEN_W / 2, 20, won ? "VICTORY" : "DOWNED");

        if(won) {
            snprintf(detail, sizeof(detail), "%d signal(s) held", (int)e->lib.count);
        } else {
            snprintf(detail, sizeof(detail), "charge depleted");
        }
        draw_centred(canvas, FT_SCREEN_W / 2, 34, detail);

        /* Baselines stay at or below 62: a glyph cell extends one row past
         * its baseline, and descenders need that row. */
        draw_centred(canvas, FT_SCREEN_W / 2, 46, "OK: next fight");
        draw_centred(canvas, FT_SCREEN_W / 2, 54, "UP: how to play");
        draw_centred(canvas, FT_SCREEN_W / 2, 62, "BACK: quit");
        return;
    }

    draw_header(canvas, e);

    /* The arena and the skill check share the middle band. */
    switch(e->phase) {
    case FT_PHASE_PLAYER_ACT:
        draw_strike_check(canvas, e);
        break;
    case FT_PHASE_TELEGRAPH:
        draw_guard_check(canvas, e);
        break;
    default:
        draw_arena(canvas, e);
        break;
    }

    /* The popup waits for the animation, or it would cover the arena for the
     * whole hold and the sprites would never be seen to act. */
    if(!ft_encounter_in_anim(e)) {
        if(e->phase == FT_PHASE_RESULT) draw_player_result(canvas, e);
        if(e->phase == FT_PHASE_IMPACT) draw_enemy_result(canvas, e);
    }

    draw_status(canvas, e);

    const char* hint = ft_tutorial_hint(e);

    if(e->phase == FT_PHASE_MENU) {
        if(hint) draw_coach_callout(canvas, hint);
        draw_menu(canvas, e);
        return;
    }

    /* Everywhere else the action row is free, so the coach speaks there. */
    if(hint) {
        draw_prompt(canvas, hint);
    } else if(e->phase == FT_PHASE_PLAYER_ACT) {
        draw_prompt(canvas, "OK to strike");
    } else if(e->phase == FT_PHASE_TELEGRAPH) {
        draw_prompt(canvas, "OK to guard");
    }
}
