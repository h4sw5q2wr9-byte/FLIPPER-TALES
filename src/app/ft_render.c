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

/* Blit a 16x16 sprite. Bit n of each row is column n from the left. */
static void draw_sprite(Canvas* c, const uint16_t* rows, int32_t x, int32_t y) {
    for(int32_t sy = 0; sy < FT_SPRITE_H; sy++) {
        const uint16_t bits = rows[sy];
        if(!bits) continue;
        for(int32_t sx = 0; sx < FT_SPRITE_W; sx++) {
            if(bits & (1u << sx)) canvas_draw_dot(c, x + sx, y + sy);
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

/* Lunge curve: out fast, hold briefly, ease back. Returns pixels of travel
 * for a progress value of 0..255. */
static int32_t lunge_px(uint8_t t, int32_t reach) {
    if(t < 80u) return (reach * (int32_t)t) / 80;          /* strike out   */
    if(t < 150u) return reach;                              /* connect      */
    if(t < 255u) return (reach * (int32_t)(255u - t)) / 105; /* recover     */
    return 0;
}

/* Two-pixel judder, used on whoever just took damage. */
static int32_t shake_px(uint8_t t) {
    if(t >= 200u) return 0;
    return ((t / 20u) % 2u) ? 2 : -2;
}

/* A broadcast attack crossing the gap: a widening arc, so ranged reads
 * differently from a contact lunge. */
static void draw_wave(Canvas* c, int32_t from_x, int32_t to_x, int32_t y, uint8_t t) {
    if(t >= 200u) return;

    const int32_t x = from_x + ((to_x - from_x) * (int32_t)t) / 200;
    for(int32_t i = 0; i < 3; i++) {
        const int32_t r = 2 + i * 2;
        canvas_draw_dot(c, x, y - r);
        canvas_draw_dot(c, x, y + r);
        canvas_draw_dot(c, x + (from_x < to_x ? -i : i), y);
    }
}

/* A capture bursts outward from the player. */
static void draw_capture_burst(Canvas* c, int32_t cx, int32_t cy, uint8_t t) {
    if(t >= 220u) return;
    const int32_t r = 4 + ((int32_t)t * 10) / 220;

    for(int32_t i = -1; i <= 1; i++) {
        canvas_draw_dot(c, cx - r, cy + i * 3);
        canvas_draw_dot(c, cx + r, cy + i * 3);
        canvas_draw_dot(c, cx + i * 3, cy - r);
        canvas_draw_dot(c, cx + i * 3, cy + r);
    }
}

static void draw_arena(Canvas* canvas, const FtEncounter* e) {
    const int32_t floor_y = FT_ARENA_Y + 18;

    /* A dashed ground line gives the sprites somewhere to stand and stops the
     * arena reading as two shapes floating in a void. */
    for(int32_t x = 2; x < FT_SCREEN_W - 2; x += 3) canvas_draw_dot(canvas, x, floor_y);

    const uint8_t t = ft_encounter_anim_progress(e);
    int32_t px = 4, py = floor_y - 16;
    int32_t ex = 106, ey = floor_y - 16;

    if(e->phase == FT_PHASE_RESULT) {
        const FtHitResult* r = &e->last_player_hit;
        const bool contact =
            (e->menu_index == FT_ACTION_CONTACT) && (r->outcome == FT_HIT_OK);

        if(contact) px += lunge_px(t, 70);
        if(r->damage > 0) ex += shake_px(t);

        if(!contact && r->outcome == FT_HIT_OK && e->menu_index == FT_ACTION_BROADCAST) {
            draw_wave(canvas, 24, 102, floor_y - 8, t);
        }
    } else if(e->phase == FT_PHASE_IMPACT) {
        const FtAttack* atk = ft_encounter_incoming(e);
        const bool contact = atk && atk->delivery == FT_DELIVERY_CONTACT;

        if(contact) ex -= lunge_px(t, 70);
        else if(atk) draw_wave(canvas, 102, 24, floor_y - 8, t);

        if(e->last_enemy_hit.damage > 0) px += shake_px(t);
        if(e->last_enemy_hit.captured) draw_capture_burst(canvas, 12, floor_y - 8, t);
    } else if(e->phase == FT_PHASE_MENU) {
        /* A slow idle bob, so the board is never completely still. */
        if((e->phase_ms / 600u) % 2u) py -= 1;
        if((e->phase_ms / 700u) % 2u) ey -= 1;
    }

    draw_player(canvas, px, py, ft_roll_active(&e->roll));
    draw_sprite(canvas, enemy_sprite(ft_encounter_enemy(e)->attrs), ex, ey);

    /* Enemy health, directly under its sprite. */
    const int32_t bw = 24;
    const int32_t bx = 100;
    const int32_t by = floor_y + 2;

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

        if(selected) canvas_draw_str(canvas, x + 2, y + 7, ">");
        draw_clipped(canvas, x + 9, y + 7, label, w - 12);

        /* A locked module is struck through rather than hidden, so the player
         * reads the attribute instead of wondering where the option went. */
        if(!available) {
            const int32_t lw = (int32_t)canvas_string_width(canvas, label);
            canvas_draw_line(canvas, x + 9, y + 4, x + 9 + lw, y + 4);
        }

        if(selected) canvas_set_color(canvas, ColorBlack);
    }
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
        "1/3  THE FIGHT",
        "2/3  YOUR STRIKE",
        "3/3  THEIR TURN",
    };

    /* Four lines per page, 21 characters each: the panel's width budget. */
    static const char* const BODY[FT_HELP_PAGES][4] = {
        {
            "Turns alternate. You",
            "pick a module, they",
            "hit back. Read their",
            "tags: AIR, ENC, SH2.",
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
            "Dots = jam, half hit.",
            "Solid = capture, 0",
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
