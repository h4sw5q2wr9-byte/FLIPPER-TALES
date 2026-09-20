#include "ft_render.h"

#include <stdio.h>

/* ---- Small helpers --------------------------------------------------- */

/* A hatched frame: the GUARDED attack class, encoded without colour.
 * See DESIGN.md 5 — on a 1-bit panel the border style carries what the source
 * game carried with yellow and red text. */
static void draw_hatched_frame(Canvas* canvas, int32_t x, int32_t y, int32_t w, int32_t h) {
    for(int32_t i = 0; i < w; i += 2) {
        canvas_draw_dot(canvas, x + i, y);
        canvas_draw_dot(canvas, x + i, y + h - 1);
    }
    for(int32_t i = 0; i < h; i += 2) {
        canvas_draw_dot(canvas, x, y + i);
        canvas_draw_dot(canvas, x + w - 1, y + i);
    }
}

/* A labelled bar, e.g. "CHG 18/25". */
static void draw_meter(
    Canvas* canvas,
    int32_t x,
    int32_t y,
    int32_t w,
    const char* label,
    int32_t value,
    int32_t max) {
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, x, y + 7, label);

    const int32_t bx = x + 20;
    const int32_t bw = w - 20;
    canvas_draw_frame(canvas, bx, y + 1, bw, 6);

    if(max > 0 && value > 0) {
        int32_t fill = (value * (bw - 2)) / max;
        if(fill > bw - 2) fill = bw - 2;
        if(fill < 1) fill = 1;
        canvas_draw_box(canvas, bx + 1, y + 2, fill, 4);
    }
}

/* ---- Scene ----------------------------------------------------------- */

/* Placeholder 1-bit art: the player reads as a small handheld device. */
static void draw_player(Canvas* canvas, int32_t x, int32_t y, bool hurt) {
    canvas_draw_rframe(canvas, x, y, 12, 18, 2);
    canvas_draw_box(canvas, x + 2, y + 3, 8, 6); /* screen */

    if(hurt) {
        /* A cracked screen while the roll is draining. */
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_line(canvas, x + 3, y + 4, x + 8, y + 8);
        canvas_set_color(canvas, ColorBlack);
    }

    canvas_draw_dot(canvas, x + 4, y + 13);
    canvas_draw_dot(canvas, x + 7, y + 13);
}

/* Enemy silhouette keyed to its attributes, so the lock is legible at a glance. */
static void draw_enemy(Canvas* canvas, int32_t x, int32_t y, uint32_t attrs) {
    if(attrs & FT_ATTR_AIRBORNE) {
        canvas_draw_circle(canvas, x + 8, y + 8, 6);
        canvas_draw_line(canvas, x + 1, y + 4, x - 3, y + 1);  /* wings */
        canvas_draw_line(canvas, x + 15, y + 4, x + 19, y + 1);
        canvas_draw_dot(canvas, x + 6, y + 7);
        canvas_draw_dot(canvas, x + 10, y + 7);
    } else if(attrs & FT_ATTR_ENCRYPTED) {
        canvas_draw_box(canvas, x + 1, y + 6, 14, 11); /* padlock body */
        canvas_draw_circle(canvas, x + 8, y + 5, 4);   /* shackle */
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_dot(canvas, x + 8, y + 11);
        canvas_set_color(canvas, ColorBlack);
    } else {
        canvas_draw_rframe(canvas, x, y + 3, 16, 13, 3);
        canvas_draw_dot(canvas, x + 5, y + 8);
        canvas_draw_dot(canvas, x + 10, y + 8);
        canvas_draw_line(canvas, x + 5, y + 12, x + 10, y + 12);
    }
}

/* ---- Overlays -------------------------------------------------------- */

/* The attack telegraph. The border encodes the class; near impact the whole
 * banner inverts on alternate frames, which is the guard cue. */
static void draw_telegraph(Canvas* canvas, const FtEncounter* e) {
    const FtAttack* atk = ft_encounter_incoming(e);
    if(atk == NULL) return;

    const int32_t w = 92, h = 18;
    const int32_t x = (FT_SCREEN_W - w) / 2, y = 20;

    const int32_t remaining = (int32_t)FT_TELEGRAPH_MS - (int32_t)e->phase_ms;
    const bool imminent = remaining <= FT_JAM_WINDOW_MS && remaining >= 0;

    /* Clear behind the banner so the scene does not bleed through. */
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, x, y, w, h);
    canvas_set_color(canvas, ColorBlack);

    const char* label;
    switch(atk->klass) {
    case FT_CLASS_UNDODGEABLE:
        /* Inverted: cannot be jammed or captured. */
        canvas_draw_box(canvas, x, y, w, h);
        label = "UNDODGEABLE";
        break;
    case FT_CLASS_GUARDED:
        draw_hatched_frame(canvas, x, y, w, h);
        label = "GUARDED";
        break;
    case FT_CLASS_NORMAL:
    default:
        canvas_draw_frame(canvas, x, y, w, h);
        label = "INCOMING";
        break;
    }

    if(atk->klass == FT_CLASS_UNDODGEABLE) canvas_set_color(canvas, ColorWhite);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, x + w / 2, y + 5, AlignCenter, AlignTop, label);
    canvas_set_color(canvas, ColorBlack);

    /* Wind-up bar: closes on the impact point. */
    const int32_t bar_w = w - 8;
    int32_t done = ((int32_t)e->phase_ms * bar_w) / (int32_t)FT_TELEGRAPH_MS;
    if(done > bar_w) done = bar_w;

    canvas_draw_frame(canvas, x + 4, y + h - 5, bar_w, 3);
    canvas_draw_box(canvas, x + 4, y + h - 5, done, 3);

    if(imminent) {
        /* The guard cue: a bracket closing around the impact point. */
        canvas_draw_line(canvas, x + 4 + bar_w, y + h - 8, x + 4 + bar_w, y + h - 1);
    }
}

/* The action command sweep: hit the centre for EXCELLENT. */
static void draw_action_bar(Canvas* canvas, const FtEncounter* e) {
    const int32_t w = 88, h = 9;
    const int32_t x = (FT_SCREEN_W - w) / 2, y = 26;

    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, x - 1, y - 1, w + 2, h + 2);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_frame(canvas, x, y, w, h);

    /* The target band, centred. */
    const int32_t mid = x + w / 2;
    const int32_t band = (FT_BAND_GREAT_MS * w) / FT_ACTION_WINDOW_MS;
    canvas_draw_frame(canvas, mid - band, y + 1, band * 2, h - 2);

    /* The sweeping cursor. */
    int32_t pos = ((int32_t)e->phase_ms * w) / (int32_t)FT_ACTION_WINDOW_MS;
    if(pos > w - 1) pos = w - 1;
    canvas_draw_box(canvas, x + pos, y + 1, 2, h - 2);

    if(e->action_pressed) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, mid, y + h + 2, AlignCenter, AlignTop, "LOCKED");
    }
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

/* What just happened, in one line. */
static void draw_popup(Canvas* canvas, const char* top, const char* bottom) {
    const int32_t w = 90, h = 22;
    const int32_t x = (FT_SCREEN_W - w) / 2, y = 18;

    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, x, y, w, h);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_frame(canvas, x, y, w, h);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, x + w / 2, y + 3, AlignCenter, AlignTop, top);

    if(bottom) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, x + w / 2, y + 13, AlignCenter, AlignTop, bottom);
    }
}

static void draw_player_result(Canvas* canvas, const FtEncounter* e) {
    char detail[24];
    const FtHitResult* r = &e->last_player_hit;

    switch(r->outcome) {
    case FT_HIT_LOCKED:
        draw_popup(canvas, "NO EFFECT", r->ram_refund ? "encrypted: RAM back" : "out of reach");
        return;
    case FT_HIT_DEFLECTED:
        draw_popup(canvas, "DEFLECTED", "shield held");
        return;
    case FT_HIT_MISSED:
        draw_popup(canvas, "MISS", NULL);
        return;
    case FT_HIT_OK:
    default:
        break;
    }

    if(r->damage > 0) {
        snprintf(detail, sizeof(detail), "-%d", (int)r->damage);
        draw_popup(canvas, rating_text(e->last_rating), detail);
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

/* ---- Chrome ---------------------------------------------------------- */

static void draw_top_strip(Canvas* canvas, const FtEncounter* e) {
    const FtEnemy* en = ft_encounter_enemy(e);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 9, en->name);

    /* Attribute tags, right-aligned. */
    int32_t x = FT_SCREEN_W - 2;
    char tag[12];

    if(en->shielded > 0) {
        snprintf(tag, sizeof(tag), "SHLD%d", (int)en->shielded);
        x -= 26;
        canvas_draw_str(canvas, x, 9, tag);
    }
    if(en->attrs & FT_ATTR_AIRBORNE) {
        x -= 20;
        canvas_draw_str(canvas, x, 9, "AIR");
    }
    if(en->attrs & FT_ATTR_ENCRYPTED) {
        x -= 22;
        canvas_draw_str(canvas, x, 9, "ENC");
    }

    canvas_draw_line(canvas, 0, FT_STRIP_H - 1, FT_SCREEN_W - 1, FT_STRIP_H - 1);
}

static void draw_bars(Canvas* canvas, const FtEncounter* e) {
    char label[12];

    /* Charge shows the rolling value, which is the whole point of the system:
     * the number the player reads is the one they can still act on. */
    snprintf(label, sizeof(label), "CHG");
    draw_meter(canvas, 2, FT_BARS_Y, 58, label, e->roll.current, e->stats.charge_max);

    snprintf(label, sizeof(label), "%d", (int)e->roll.current);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 62, FT_BARS_Y + 8, label);

    /* Signal, drawn as discrete bars because it is spent in whole bars. */
    const int32_t sx = 82;
    canvas_draw_str(canvas, sx, FT_BARS_Y + 8, "SIG");

    const uint8_t bars = ft_signal_bars(&e->signal);
    for(uint8_t i = 0; i < e->signal.max_bars; i++) {
        const int32_t bx = sx + 20 + i * 7;
        if(e->signal.locked) {
            draw_hatched_frame(canvas, bx, FT_BARS_Y + 1, 6, 7);
        } else if(i < bars) {
            canvas_draw_box(canvas, bx, FT_BARS_Y + 1, 6, 7);
        } else {
            canvas_draw_frame(canvas, bx, FT_BARS_Y + 1, 6, 7);
        }
    }
}

static const char* action_label(FtAction2 a) {
    switch(a) {
    case FT_ACTION_BROADCAST: return "SUBGHZ";
    case FT_ACTION_CONTACT:   return "NFC";
    case FT_ACTION_DEFEND:    return "DEF";
    case FT_ACTION_FOCUS:     return "FOCUS";
    default:                  return "?";
    }
}

static void draw_menu(Canvas* canvas, const FtEncounter* e) {
    canvas_set_font(canvas, FontSecondary);

    static const int32_t xs[FT_ACTION_COUNT] = {2, 42, 70, 96};

    for(uint8_t i = 0; i < FT_ACTION_COUNT; i++) {
        const int32_t x = xs[i];
        const bool available = ft_encounter_action_available(e, (FtAction2)i);
        const bool selected = (i == e->menu_index);

        if(selected) {
            canvas_draw_box(canvas, x - 2, FT_MENU_Y, 38, 10);
            canvas_set_color(canvas, ColorWhite);
        }

        canvas_draw_str(canvas, x, FT_MENU_Y + 8, action_label((FtAction2)i));

        /* A locked module is shown struck through rather than hidden, so the
         * player reads the attribute instead of wondering where it went. */
        if(!available) {
            const size_t len = 6u * 4u;
            canvas_draw_line(canvas, x, FT_MENU_Y + 5, x + (int32_t)len, FT_MENU_Y + 5);
        }

        if(selected) canvas_set_color(canvas, ColorBlack);
    }
}

/* ---- Entry ----------------------------------------------------------- */

void ft_render_battle(Canvas* canvas, const FtEncounter* e) {
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    if(e->phase == FT_PHASE_WIN || e->phase == FT_PHASE_LOSE) {
        const bool won = (e->phase == FT_PHASE_WIN);
        char detail[28];

        if(won) {
            snprintf(detail, sizeof(detail), "%d signal(s) held", (int)e->lib.count);
        } else {
            snprintf(detail, sizeof(detail), "charge depleted");
        }

        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, FT_SCREEN_W / 2, 20, AlignCenter, AlignTop,
                                won ? "VICTORY" : "DOWNED");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, FT_SCREEN_W / 2, 34, AlignCenter, AlignTop, detail);
        canvas_draw_str_aligned(canvas, FT_SCREEN_W / 2, 50, AlignCenter, AlignTop,
                                "OK: again   BACK: exit");
        return;
    }

    draw_top_strip(canvas, e);

    /* Scene. The player sprite cracks while Charge is still draining. */
    draw_player(canvas, 8, FT_SCENE_Y + 8, ft_roll_active(&e->roll));
    draw_enemy(canvas, 96, FT_SCENE_Y + 8, ft_encounter_enemy(e)->attrs);

    /* Enemy charge bar under its sprite. */
    {
        const int32_t bw = 26;
        canvas_draw_frame(canvas, 92, FT_SCENE_Y + 27, bw, 4);
        if(e->enemy_charge > 0) {
            int32_t fill = ((int32_t)e->enemy_charge * (bw - 2)) / e->enemy_charge_max;
            if(fill < 1) fill = 1;
            canvas_draw_box(canvas, 93, FT_SCENE_Y + 28, fill, 2);
        }
    }

    switch(e->phase) {
    case FT_PHASE_PLAYER_ACT:
        draw_action_bar(canvas, e);
        break;
    case FT_PHASE_TELEGRAPH:
        draw_telegraph(canvas, e);
        break;
    case FT_PHASE_RESULT:
        draw_player_result(canvas, e);
        break;
    case FT_PHASE_IMPACT:
        draw_enemy_result(canvas, e);
        break;
    default:
        break;
    }

    draw_bars(canvas, e);

    if(e->phase == FT_PHASE_MENU) {
        draw_menu(canvas, e);
    } else {
        canvas_set_font(canvas, FontSecondary);
        if(e->phase == FT_PHASE_TELEGRAPH) {
            canvas_draw_str(canvas, 2, FT_MENU_Y + 8, "OK: guard");
        } else if(e->phase == FT_PHASE_PLAYER_ACT) {
            canvas_draw_str(canvas, 2, FT_MENU_Y + 8, "OK: strike");
        }
    }
}
