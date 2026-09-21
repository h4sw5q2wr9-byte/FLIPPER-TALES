#include "ft_render.h"

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
    const FtEnemy* en = ft_encounter_enemy(e);

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

static void draw_player(Canvas* c, int32_t x, int32_t y, bool hurt) {
    canvas_draw_rframe(c, x, y, 10, 14, 2);
    canvas_draw_box(c, x + 2, y + 2, 6, 5);

    if(hurt) {
        canvas_set_color(c, ColorWhite);
        canvas_draw_line(c, x + 3, y + 3, x + 6, y + 6);
        canvas_set_color(c, ColorBlack);
    }
    canvas_draw_dot(c, x + 3, y + 10);
    canvas_draw_dot(c, x + 6, y + 10);
}

/* Silhouette keyed to attributes, so the lock is legible at a glance. */
static void draw_enemy(Canvas* c, int32_t x, int32_t y, uint32_t attrs) {
    if(attrs & FT_ATTR_AIRBORNE) {
        canvas_draw_circle(c, x + 7, y + 6, 5);
        canvas_draw_line(c, x + 2, y + 3, x - 1, y);
        canvas_draw_line(c, x + 12, y + 3, x + 15, y);
        canvas_draw_dot(c, x + 5, y + 5);
        canvas_draw_dot(c, x + 9, y + 5);
    } else if(attrs & FT_ATTR_ENCRYPTED) {
        canvas_draw_box(c, x + 1, y + 5, 12, 9);
        canvas_draw_circle(c, x + 7, y + 4, 3);
        canvas_set_color(c, ColorWhite);
        canvas_draw_dot(c, x + 7, y + 9);
        canvas_set_color(c, ColorBlack);
    } else {
        canvas_draw_rframe(c, x, y + 2, 14, 11, 3);
        canvas_draw_dot(c, x + 4, y + 6);
        canvas_draw_dot(c, x + 9, y + 6);
        canvas_draw_line(c, x + 4, y + 9, x + 9, y + 9);
    }
}

static void draw_arena(Canvas* canvas, const FtEncounter* e) {
    draw_player(canvas, 6, FT_ARENA_Y + 5, ft_roll_active(&e->roll));
    draw_enemy(canvas, 104, FT_ARENA_Y + 5, ft_encounter_enemy(e)->attrs);

    /* Enemy health, directly under its sprite. */
    const int32_t bw = 24;
    const int32_t bx = 100;
    const int32_t by = FT_ARENA_Y + 21;

    canvas_draw_frame(canvas, bx, by, (size_t)bw, 4);
    {
        const int32_t fill = ft_bar_fill(e->enemy_charge, e->enemy_charge_max, bw);
        if(fill > 0) canvas_draw_box(canvas, bx + 1, by + 1, (size_t)fill, 2);
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
static void draw_strike_check(Canvas* canvas, const FtEncounter* e) {
    canvas_set_font(canvas, FontSecondary);
    draw_centred(canvas, FT_SCREEN_W / 2, FT_ARENA_Y + 4, "TIME YOUR STRIKE");

    canvas_draw_frame(canvas, TRACK_X, TRACK_Y, TRACK_W, TRACK_H);

    const int32_t mid = TRACK_X + TRACK_W / 2;
    const int32_t good = ms_to_px(FT_BAND_GOOD_MS, FT_ACTION_WINDOW_MS);
    const int32_t best = ms_to_px(FT_BAND_EXCELLENT_MS, FT_ACTION_WINDOW_MS);

    /* Widening bands, drawn from loosest to tightest. */
    hatch(canvas, mid - good, TRACK_Y + 1, good * 2, TRACK_H - 2);
    canvas_draw_box(canvas, mid - best, TRACK_Y + 1, (size_t)(best * 2), TRACK_H - 2);

    draw_cursor(canvas, TRACK_X + ms_to_px(e->phase_ms, FT_ACTION_WINDOW_MS));

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

    draw_cursor(canvas, TRACK_X + ms_to_px(e->phase_ms, FT_TELEGRAPH_MS));
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

/* ---- Status ---------------------------------------------------------- */

static void draw_status(Canvas* canvas, const FtEncounter* e) {
    canvas_set_font(canvas, FontSecondary);
    char buf[12];

    canvas_draw_str(canvas, 2, FT_STATUS_Y + 7, "CHG");

    const int32_t bx = 22, bw = 38;
    canvas_draw_frame(canvas, bx, FT_STATUS_Y + 1, (size_t)bw, 7);
    {
        const int32_t fill = ft_bar_fill(e->roll.current, e->stats.charge_max, bw);
        if(fill > 0) canvas_draw_box(canvas, bx + 1, FT_STATUS_Y + 2, (size_t)fill, 5);
    }

    snprintf(buf, sizeof(buf), "%d", (int)e->roll.current);
    canvas_draw_str(canvas, 63, FT_STATUS_Y + 7, buf);

    snprintf(buf, sizeof(buf), "R%d", (int)e->stats.ram);
    canvas_draw_str(canvas, 78, FT_STATUS_Y + 7, buf);

    canvas_draw_str(canvas, 93, FT_STATUS_Y + 7, "SIG");

    const uint8_t bars = ft_signal_bars(&e->signal);
    for(uint8_t i = 0; i < e->signal.max_bars && i < 2u; i++) {
        const int32_t sx = 112 + i * 7;
        if(e->signal.locked) {
            hatch(canvas, sx, FT_STATUS_Y + 1, 6, 7);
            canvas_draw_frame(canvas, sx, FT_STATUS_Y + 1, 6, 7);
        } else if(i < bars) {
            canvas_draw_box(canvas, sx, FT_STATUS_Y + 1, 6, 7);
        } else {
            canvas_draw_frame(canvas, sx, FT_STATUS_Y + 1, 6, 7);
        }
    }
}

/* ---- Action row ------------------------------------------------------ */

static const char* action_label(FtAction2 a) {
    switch(a) {
    case FT_ACTION_BROADCAST: return "SUBGHZ";
    case FT_ACTION_CONTACT:   return "NFC";
    case FT_ACTION_DEFEND:    return "DEFEND";
    case FT_ACTION_FOCUS:     return "FOCUS";
    default:                  return "?";
    }
}

/* Two rows of two. One row of four cannot hold these labels at this font
 * without either truncating them to initials or running off the panel. */
static void draw_menu(Canvas* canvas, const FtEncounter* e) {
    canvas_set_font(canvas, FontSecondary);

    for(uint8_t i = 0; i < FT_ACTION_COUNT; i++) {
        const int32_t col = i % 2;
        const int32_t row = i / 2;
        const int32_t x = col ? 64 : 2;
        const int32_t w = 61;
        const int32_t y = FT_ACTION_Y + row * 9;

        const bool selected = (i == e->menu_index);
        const bool available = ft_encounter_action_available(e, (FtAction2)i);
        const char* label = action_label((FtAction2)i);

        if(selected) {
            canvas_draw_box(canvas, x, y, (size_t)w, 9);
            canvas_set_color(canvas, ColorWhite);
        }

        draw_clipped(canvas, x + 3, y + 7, label, w - 6);

        /* A locked module is struck through rather than hidden, so the player
         * reads the attribute instead of wondering where the option went. */
        if(!available) {
            const int32_t w = (int32_t)canvas_string_width(canvas, label);
            canvas_draw_line(canvas, x + 3, y + 4, x + 3 + w, y + 4);
        }

        if(selected) canvas_set_color(canvas, ColorBlack);
    }
}

static void draw_prompt(Canvas* canvas, const char* s) {
    canvas_set_font(canvas, FontSecondary);
    draw_centred(canvas, FT_SCREEN_W / 2, FT_ACTION_Y + 12, s);
}

/* ---- Help ------------------------------------------------------------ */

void ft_render_help(Canvas* canvas) {
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    canvas_set_font(canvas, FontSecondary);
    draw_centred(canvas, FT_SCREEN_W / 2, 7, "HOW TO PLAY");
    canvas_draw_line(canvas, 0, 9, FT_SCREEN_W - 1, 9);

    /* At ~6px per character, 20 characters is the width budget per line. */
    canvas_draw_str(canvas, 2, 18, "Pick a module, tap");
    canvas_draw_str(canvas, 2, 26, "OK inside the black");
    canvas_draw_str(canvas, 2, 34, "block: harder hit.");

    canvas_draw_str(canvas, 2, 46, "Attacked? Tap OK in");
    canvas_draw_str(canvas, 2, 54, "the end zone. Solid");
    canvas_draw_str(canvas, 2, 62, "= capture, dots = jam");
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

    switch(e->phase) {
    case FT_PHASE_RESULT:
        draw_player_result(canvas, e);
        break;
    case FT_PHASE_IMPACT:
        draw_enemy_result(canvas, e);
        break;
    default:
        break;
    }

    draw_status(canvas, e);

    switch(e->phase) {
    case FT_PHASE_MENU:
        draw_menu(canvas, e);
        break;
    case FT_PHASE_PLAYER_ACT:
        draw_prompt(canvas, "OK to strike");
        break;
    case FT_PHASE_TELEGRAPH:
        draw_prompt(canvas, "OK to guard");
        break;
    default:
        break;
    }
}
