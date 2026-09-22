#include "ft_render.h"

#include "../core/ft_tutorial.h"
#include "ft_enemy_art.h"
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
    const uint8_t who =
        their_turn ? e->acting_foe :
                     ft_encounter_effective_target(e, (FtAction2)e->menu_index);
    const FtEnemy* en = ft_encounter_foe(e, who);

    canvas_set_font(canvas, FontSecondary);

    /* Tags are laid out from the right using measured widths, and the name
     * gets whatever is left over. */
    int32_t right = FT_SCREEN_W - 2;

    char tag[10];

    /* Right to left, least urgent first, because the name is clipped from the
     * right and the last tag placed is the one nearest it.
     *
     * FAST and JAM are here because both became real: FAST orders the round
     * and JAM locks the meter, and until they were wired up neither did
     * anything, so neither needed saying. An attribute that changes the
     * fight and is not on the screen is a rule the player has to lose to. */
    if(en->attrs & FT_ATTR_SLEEPER) {
        strcpy(tag, "SLP");
        right -= (int32_t)canvas_string_width(canvas, tag);
        canvas_draw_str(canvas, right, 7, tag);
        right -= 3;
    }
    if(en->attrs & FT_ATTR_BULWARK) {
        strcpy(tag, "WALL");
        right -= (int32_t)canvas_string_width(canvas, tag);
        canvas_draw_str(canvas, right, 7, tag);
        right -= 3;
    }
    if(en->attrs & FT_ATTR_JAMMER) {
        strcpy(tag, "JAM");
        right -= (int32_t)canvas_string_width(canvas, tag);
        canvas_draw_str(canvas, right, 7, tag);
        right -= 3;
    }
    if(en->attrs & FT_ATTR_FAST) {
        strcpy(tag, "FST");
        right -= (int32_t)canvas_string_width(canvas, tag);
        canvas_draw_str(canvas, right, 7, tag);
        right -= 3;
    }
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

/* The hero, the same 16x18 art the overworld draws. y is the *floor* line, so
 * he stands on it rather than being top-aligned like the 16x16 foes. */
static void draw_player(Canvas* c, int32_t x, int32_t floor_y, bool hurt) {
    const int32_t y = floor_y - FT_HERO_H;

    for(int32_t sy = 0; sy < FT_HERO_H; sy++) {
        uint16_t bits = FT_SPRITE_HERO[sy];
        if(!bits) continue;

        int32_t sx = 0;
        while(sx < FT_HERO_W) {
            if(!(bits & (1u << sx))) {
                sx++;
                continue;
            }
            int32_t run = 0;
            while(sx + run < FT_HERO_W && (bits & (1u << (sx + run)))) run++;

            canvas_draw_box(c, x + sx, y + sy, (size_t)run, 1);
            sx += run;
        }
    }

    /* While Charge is still draining, the screen goes dark: the sprite reads
     * its own state rather than relying on the bar alone. */
    if(hurt) {
        canvas_set_color(c, ColorXOR);
        canvas_draw_box(
            c, x + FT_HERO_SCREEN_X, y + FT_HERO_SCREEN_Y,
            FT_HERO_SCREEN_W, FT_HERO_SCREEN_H);
        canvas_set_color(c, ColorBlack);
    }
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


/* A radio chevron, the visual vocabulary for anything broadcast. */
/* A travelling signal is meant to leave the arena, so the apex runs past the
 * right edge on purpose. Clipping here keeps the exit clean for every caller
 * rather than making each one guess a safe stopping point. */
static void chevron_dot(Canvas* c, int32_t x, int32_t y) {
    if(x < 0 || x >= FT_SCREEN_W || y < 0 || y >= FT_SCREEN_H) return;
    canvas_draw_dot(c, x, y);
}

static void draw_chevron(Canvas* c, int32_t x, int32_t y, int32_t dir, int32_t size) {
    /* Apex at x, opening away from the direction of travel: the arms widen as
     * they trail behind, so the shape points where the wave is going. */
    for(int32_t i = 0; i <= size; i++) {
        chevron_dot(c, x - dir * i, y - (i + 1));
        chevron_dot(c, x - dir * i, y + (i + 1));
    }
    chevron_dot(c, x, y);
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
/* Integer easing over 0..span, returning 0..range. No floating point, and the
 * worst case (range 128, span 255) stays inside int32 by a wide margin. */
static int32_t ease_in(int32_t t, int32_t span, int32_t range) {
    if(t <= 0 || span <= 0) return 0;
    if(t >= span) return range;
    return (range * t * t) / (span * span);
}

static int32_t ease_out(int32_t t, int32_t span, int32_t range) {
    if(t <= 0 || span <= 0) return 0;
    if(t >= span) return range;

    const int32_t u = span - t;
    return range - (range * u * u) / (span * span);
}

/* The moment of contact: spokes thrown out from the point of impact, growing
 * fast and gone almost at once.
 *
 * The old spark was three fixed diagonal scratches drawn for the whole
 * approach, so the "impact" was visible long before anything arrived. This
 * exists only after the strike frame and lasts a fifth of the time, which is
 * what makes it read as a hit rather than as decoration. */
#define BURST_MS 34

static void draw_contact_spark(Canvas* c, int32_t x, int32_t y, uint8_t t) {
    if(t < FT_ANIM_STRIKE || t >= FT_ANIM_STRIKE + BURST_MS) return;

    const int32_t age = (int32_t)t - FT_ANIM_STRIKE;
    const int32_t r = 2 + ease_out(age, BURST_MS, 7);

    /* Eight spokes, the diagonals shortened so the burst reads round. */
    static const int8_t DIR[8][2] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

    for(int32_t i = 0; i < 8; i++) {
        const int32_t len = (i < 4) ? r : (r * 2) / 3;
        const int32_t inner = len / 2;

        canvas_draw_line(
            c, x + DIR[i][0] * inner, y + DIR[i][1] * inner,
            x + DIR[i][0] * len, y + DIR[i][1] * len);
    }

    /* A solid core for the first couple of frames only. */
    if(age < BURST_MS / 3) canvas_draw_box(c, x - 1, y - 1, 3, 3);
}

/* A foe going down: it folds toward the floor and breaks up as it goes.
 * Returns how many rows of the sprite to skip from the top, and fills a
 * dither mask, so the caller can draw a collapsing silhouette.
 *
 * Before this, a foe vanished between two frames the instant its bar hit
 * zero, which is the single most common note anyone gives about combat feel:
 * things have to be *seen* to die. */
static void draw_defeat(Canvas* c, const uint16_t* rows, int32_t x, int32_t y,
                        uint8_t progress) {
    /* Fold: the sprite loses height from the top as it crumples down. */
    const int32_t squash = ease_in(progress, 255, FT_SPRITE_H - 3);
    const int32_t top = y + squash;

    for(int32_t sy = squash; sy < FT_SPRITE_H; sy++) {
        uint16_t bits = rows[sy];
        if(!bits) continue;

        /* Break up as it falls: past halfway, drop every other pixel, then
         * two in three. A silhouette thinning out reads as coming apart; a
         * solid shape that shrinks reads as a bug. */
        if(progress > 128u) {
            const uint16_t keep = (progress > 192u) ? 0x9249u : 0x5555u;
            bits = (uint16_t)(bits & ((sy & 1) ? keep : (uint16_t)~keep));
        }

        for(int32_t sx = 0; sx < FT_SPRITE_W; sx++) {
            if(bits & (1u << sx)) canvas_draw_dot(c, x + sx, top + sy - squash);
        }
    }
}

/* Two-pixel judder for whoever just took damage, `since` units after its own
 * strike moment. Per-foe rather than global, so a signal crossing the row
 * shakes each one as it arrives. */
static int32_t shake_since(uint32_t since) {
    if(since >= 60u) return 0;
    return ((since / 12u) % 2u) ? 2 : -2;
}

static int32_t shake_px(uint8_t t) {
    if(t < FT_ANIM_STRIKE) return 0;
    return shake_since((uint32_t)(t - FT_ANIM_STRIKE));
}

/* Attacker travel: out during the emit window, back during recovery. */
#define LUNGE_BACK 4  /* how far the wind-up pulls away from the target */
#define LUNGE_HOLD 30 /* progress units spent at full extension */

/* A strike with weight: pull away, accelerate across the gap, land, hold for
 * a beat, then drift back.
 *
 * The first version was three straight lines — constant-speed out, constant
 * speed back — which is why it read as a sprite being slid around rather than
 * as something hitting something. The dash accelerates *into* the target
 * (ease_in) so the fastest frame is the frame of contact, and the recovery
 * decelerates (ease_out) so the return is a settle, not a second dash. */
static int32_t lunge_px(uint8_t t, int32_t reach) {
    const int32_t p = (int32_t)t;

    if(p < FT_ANIM_WINDUP) {
        /* Away quickly, then hang there: the pause before a punch. */
        return -ease_out(p, FT_ANIM_WINDUP, LUNGE_BACK);
    }

    if(p < FT_ANIM_STRIKE) {
        const int32_t span = FT_ANIM_STRIKE - FT_ANIM_WINDUP;
        return -LUNGE_BACK + ease_in(p - FT_ANIM_WINDUP, span, reach + LUNGE_BACK);
    }

    if(p < FT_ANIM_STRIKE + LUNGE_HOLD) return reach;

    const int32_t span = FT_ANIM_RECOVER - (FT_ANIM_STRIKE + LUNGE_HOLD);
    return reach - ease_out(p - (FT_ANIM_STRIKE + LUNGE_HOLD), span, reach);
}

/* The antenna charging before a broadcast leaves the player. */
static void draw_antenna_charge(Canvas* c, int32_t x, int32_t y, uint8_t t) {
    if(t >= FT_ANIM_EMIT) return;
    if(((t / 14u) % 2u) == 0u) return;

    canvas_draw_box(c, x + 6, y - 3, 4, 3);
}

/* The signal crossing the arena, for a broadcast. It keeps travelling until it
 * leaves the screen, striking each foe as it arrives rather than damaging the
 * whole row at once. */
static void draw_travelling_signal(Canvas* c, int32_t y, uint8_t t) {
    if(t < FT_ANIM_EMIT) return;

    const int32_t from = 22, to = FT_SCREEN_W + 10;
    const int32_t span = FT_ANIM_RECOVER - FT_ANIM_EMIT;
    const int32_t lead = (int32_t)t - FT_ANIM_EMIT;

    for(int32_t wv = 0; wv < 3; wv++) {
        const int32_t p = lead - wv * 26;
        if(p < 0) continue;

        const int32_t x = from + ((to - from) * p) / span;
        if(x > FT_SCREEN_W + 6) continue;

        draw_chevron(c, x, y, 1, 2 + wv);
    }
}

static void draw_arena(Canvas* canvas, const FtEncounter* e) {
    const int32_t floor_y = FT_ARENA_Y + 18;
    const uint8_t t = ft_encounter_anim_progress(e);
    const uint8_t count = e->foe_count;

    for(int32_t x = 2; x < FT_SCREEN_W - 2; x += 3) canvas_draw_dot(canvas, x, floor_y);

    int32_t px = 4;

    /* Foes are top-aligned 16px tall; the hero is 18 and stands on the floor
     * line, so his top edge is two rows higher. */
    int32_t py = floor_y - 16;
    int32_t hero_floor = floor_y;
    /* Whoever the highlighted action would land on. Nothing points at them:
     * targeting is automatic, so there is no choice to show. */
    const uint8_t tgt = ft_encounter_effective_target(e, (FtAction2)e->menu_index);

    /* --- the player's action --- */
    if(e->phase == FT_PHASE_RESULT) {
        const FtAction2 act = (FtAction2)e->menu_index;
        const bool broadcast = ft_encounter_action_is_broadcast(e, act);
        const bool attacked =
            (act == FT_ACTION_BROADCAST || act == FT_ACTION_CONTACT) &&
            e->last_player_hit.outcome != FT_HIT_MISSED;

        if(attacked && broadcast) {
            draw_antenna_charge(canvas, px, py, t);
            draw_travelling_signal(canvas, floor_y - 9, t);
        } else if(attacked) {
            /* Close the real distance to the target: a foe on the far side of
             * a three-wide row is a longer walk than one standing next to you,
             * and the approach should show that. */
            const int32_t reach = foe_x(tgt, count) - 4 - (FT_SPRITE_W - 2);
            px += lunge_px(t, reach > 0 ? reach : 0);
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

        /* A deflection is drawn travelling back the way the attack came, so
         * the bounce is something you watch rather than read about. */
        if(e->last_deflect_fired || e->last_enemy_hit.perfect) {
            draw_broadcast(canvas, 20, 44, floor_y - 9, 1, t);
        }
    }

    if(e->phase == FT_PHASE_MENU && ((e->phase_ms / 600u) % 2u)) hero_floor -= 1;

    /* Both fighters strobe on the frame the hit lands, before the iris. The
     * XOR goes over the drawn sprite: inverting the empty space first and
     * then drawing black on black just gives a solid brick. */
    const FtHitFx arena_fx = ft_encounter_hit_fx(e);

    draw_player(canvas, px, hero_floor, ft_roll_active(&e->roll));

    /* Whatever is eating you, named over your own head. The three payloads
     * were in the data from the start and none of them did anything, so
     * none of them needed showing; now they do. */
    const char* status = ft_encounter_status_tag(e);
    if(status) {
        canvas_set_font(canvas, FontSecondary);

        /* Beside him, inside the arena. Above his head is the title bar,
         * which is not the arena's to draw in. */
        const int32_t w = (int32_t)canvas_string_width(canvas, status) + 4;
        const int32_t sx = px + FT_HERO_W + 1;
        const int32_t sy = FT_ARENA_Y + 1;

        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, sx, sy, (size_t)w, 9);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_frame(canvas, sx, sy, (size_t)w, 9);
        canvas_draw_str(canvas, sx + 2, sy + 7, status);
    }

    /* The deflect stance, under whatever is eating you.
     *
     * A turn spent arming something has to leave a mark, or the player has
     * bought an invisible thing and will forget they own it before the hit
     * lands. Inverted, because it is the one badge that is good news. */
    if(e->deflect_armed) {
        canvas_set_font(canvas, FontSecondary);

        /* "DEF", not "DEFLECT": at seven characters the badge reached across
         * the first foe's sprite on a full three-foe row. The other badges
         * on this row are three and four characters for the same reason. */
        const int32_t w = (int32_t)canvas_string_width(canvas, "DEF") + 4;
        const int32_t sx = px + FT_HERO_W + 1;
        const int32_t sy = FT_ARENA_Y + (status ? 11 : 1);

        canvas_draw_box(canvas, sx, sy, (size_t)w, 9);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_str(canvas, sx + 2, sy + 7, "DEF");
        canvas_set_color(canvas, ColorBlack);
    }

    if(arena_fx.strobe) {
        canvas_set_color(canvas, ColorXOR);
        canvas_draw_box(
            canvas, px, hero_floor - FT_HERO_H, FT_HERO_W, FT_HERO_H);
        canvas_set_color(canvas, ColorBlack);
    }

    /* --- the row of foes --- */
    for(uint8_t i = 0; i < count; i++) {
        if(!ft_encounter_foe_visible(e, i)) continue;

        int32_t x = foe_x(i, count);
        int32_t y = py;

        if(e->phase == FT_PHASE_IMPACT && i == e->acting_foe) x += foe_shift;
        if(e->phase == FT_PHASE_RESULT && e->foe_hit_valid[i] && e->foe_hits[i].damage > 0) {
            /* Each foe reacts when the signal reaches it, not in unison. */
            const uint8_t at = ft_encounter_foe_hit_at(e, i);
            if(t >= at) x += shake_since((uint32_t)(t - at));
        }
        if(e->phase == FT_PHASE_MENU && ((e->phase_ms / 700u) % 2u)) y -= 1;

        /* A foe this turn killed folds up instead of being drawn standing. */
        const uint8_t dying = ft_encounter_foe_defeat(e, i);
        const uint16_t* art = ft_enemy_art(e->foes[i].id);

        if(dying > 0u) {
            draw_defeat(canvas, art, x, y, dying);

            /* No health bar under something that no longer has any. */
            continue;
        }

        draw_sprite(canvas, art, x, y);

        /* A sleeper is faded rather than badged.
         *
         * This used to be a white box with three dots stamped across the
         * middle of the sprite, which on the Blank Wall read as a random bar
         * through the art — the marker was less legible than the thing it was
         * marking. Knocking out every other pixel greys the foe out while
         * leaving its silhouette whole, which is the 1-bit way to say "not
         * active" without covering anything up.
         *
         * A BULWARK gets nothing. It is the most present thing on the board,
         * not a dormant one: fading it would say the opposite of the truth,
         * and the WALL tag in the title bar already names it. */
        if(!ft_encounter_foe_awake(e, i) &&
           !(FT_ENEMIES[e->foes[i].id].attrs & FT_ATTR_BULWARK)) {
            canvas_set_color(canvas, ColorWhite);
            for(int32_t dy = 0; dy < FT_SPRITE_H; dy++) {
                for(int32_t dx = (dy & 1); dx < FT_SPRITE_W; dx += 2) {
                    canvas_draw_dot(canvas, x + dx, y + dy);
                }
            }
            canvas_set_color(canvas, ColorBlack);
        }

        /* Only the foe that actually landed the hit flickers with you. */
        if(arena_fx.strobe && e->phase == FT_PHASE_IMPACT && i == e->acting_foe) {
            canvas_set_color(canvas, ColorXOR);
            canvas_draw_box(canvas, x, y, FT_SPRITE_W, FT_SPRITE_H);
            canvas_set_color(canvas, ColorBlack);
        }

        /* Health, directly beneath each foe. */
        const int32_t bw = 16;
        canvas_draw_frame(canvas, x, floor_y + 2, (size_t)bw, 4);
        const int32_t fill =
            ft_bar_fill(ft_encounter_foe_shown_charge(e, i), e->foes[i].charge_max, bw);
        if(fill > 0) canvas_draw_box(canvas, x + 1, floor_y + 3, (size_t)fill, 2);
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

    /* +6, not +4: the glyph cell is six rows deep and its top row was being
     * painted over by the header rule, so the title read as struck through.
     * At +6 there is a blank row between the two. */
    draw_centred(canvas, FT_SCREEN_W / 2, FT_ARENA_Y + 6,
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

    if(e->action_pressed) {
        /* Frozen where it stopped, flashing: the point is to see exactly
         * where the hit landed rather than watch the cursor sail past it. */
        const int32_t at = TRACK_X + ms_to_px(e->action_press_ms, FT_ACTION_WINDOW_MS);

        if((e->action_locked_ms / 70u) % 2u) {
            canvas_draw_box(canvas, at - 1, TRACK_Y - 2, 5, TRACK_H + 4);
        } else {
            draw_cursor(canvas, at);
        }
        return;
    }

    draw_cursor(canvas, TRACK_X + ms_to_px(ft_encounter_sweep_ms(e), FT_ACTION_WINDOW_MS));
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
    /* "UNDODGEABLE" was a lie by omission: it means no *timed* guard, and
     * PROTECT still blunts the hit — which is exactly what the coach line
     * tells you to do. Say what is true. */
    case FT_CLASS_UNDODGEABLE: title = "NO JAM - PROTECT"; break;
    case FT_CLASS_GUARDED:     title = "JAM ONLY - NO CAPTURE"; break;
    default:                   title = "INCOMING"; break;
    }
    draw_centred(canvas, FT_SCREEN_W / 2, FT_ARENA_Y + 6, title);

    canvas_draw_frame(canvas, TRACK_X, TRACK_Y, TRACK_W, TRACK_H);

    if(atk->klass == FT_CLASS_UNDODGEABLE) {
        /* Nothing to aim at: say so rather than leaving an empty track. */
        canvas_draw_line(canvas, TRACK_X + 1, TRACK_Y + 1, TRACK_X + TRACK_W - 2,
                         TRACK_Y + TRACK_H - 2);
        canvas_draw_line(canvas, TRACK_X + 1, TRACK_Y + TRACK_H - 2, TRACK_X + TRACK_W - 2,
                         TRACK_Y + 1);
    } else {
        const bool hard = e->fx.hard_mode;
        const uint32_t jam_ms = ft_jam_window_ms(hard, atk->klass);
        const uint32_t cap_ms = hard ? FT_CAPTURE_WINDOW_MS / 2u : FT_CAPTURE_WINDOW_MS;

        const int32_t jam_w = ms_to_px(jam_ms, FT_TELEGRAPH_MS);
        const int32_t cap_w = ms_to_px(cap_ms, FT_TELEGRAPH_MS);
        const int32_t right = TRACK_X + TRACK_W - 1;

        hatch(canvas, right - jam_w, TRACK_Y + 1, jam_w, TRACK_H - 2);

        /* Only NORMAL attacks allow a perfect block, so only they get it. */
        if(atk->klass == FT_CLASS_NORMAL) {
            canvas_draw_box(canvas, right - cap_w, TRACK_Y + 1, (size_t)cap_w, TRACK_H - 2);
        }
    }

    if(ft_encounter_in_ready(e)) {
        draw_ready_overlay(canvas, e);
        return;
    }

    const int32_t live = TRACK_X + ms_to_px(ft_encounter_sweep_ms(e), FT_TELEGRAPH_MS);

    if(e->guard_pressed) {
        /* Frozen where the guard went up, flashing, with the impact edge
         * still closing on it. The strike check has always shown the player
         * exactly where they landed; the guard showed nothing at all, so a
         * mistimed block was indistinguishable from an unblockable hit. */
        const int32_t at = TRACK_X + ms_to_px(e->guard_press_ms, FT_TELEGRAPH_MS);

        if((e->guard_locked_ms / 70u) % 2u) {
            canvas_draw_box(canvas, at - 1, TRACK_Y - 2, 5, TRACK_H + 4);
        } else {
            draw_cursor(canvas, at);
        }

        /* A thin line, not a second cursor: the eye should follow the gap
         * shrinking, not mistake this for another thing to aim with. */
        canvas_draw_line(canvas, live, TRACK_Y - 2, live, TRACK_Y + TRACK_H + 1);
        return;
    }

    draw_cursor(canvas, live);
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

/* Where the guard actually landed, in words, in under 20 characters.
 *
 * "you were hit" and "you blocked too early by a fifth of a second" used to
 * look identical. The bar now freezes at the press; this is the same reading
 * as a number, which is what survives being looked at for half a second. */
static void guard_note(const FtEncounter* e, char* out, size_t n) {
    const FtAttack* atk = ft_encounter_incoming(e);
    const int32_t   off = ft_encounter_guard_offset(e);

    if(atk == NULL || atk->klass == FT_CLASS_UNDODGEABLE) {
        out[0] = '\0';
        return;
    }
    if(off < 0) {
        snprintf(out, n, "no guard");
        return;
    }

    const int32_t jam = (int32_t)ft_jam_window_ms(e->fx.hard_mode, atk->klass);

    /* Inside the window the number is how tight it was; outside it is how
     * much too early, which is the number you can actually act on. */
    if(off <= jam) {
        snprintf(out, n, "at %dms", (int)off);
    } else {
        snprintf(out, n, "%dms early", (int)(off - jam));
    }
}

static void draw_enemy_result(Canvas* canvas, const FtEncounter* e) {
    const FtHitResult* r = &e->last_enemy_hit;

    /* Wide enough that the compiler can see no format can be truncated; the
     * popup clamps to the panel anyway, so the extra bytes cost a frame of
     * stack and nothing else. */
    char detail[48];
    char note[20];

    guard_note(e, note, sizeof(note));

    /* The bounce is the headline whenever there was one: it is what the bar
     * and the turn were spent on. */
    if(e->last_deflect_fired) {
        snprintf(detail, sizeof(detail), "-%d  %s", (int)e->last_deflect_damage, note);
        draw_popup(canvas,
                   (e->last_guard == FT_GUARD_CAPTURE) ? "SENT BACK!" : "HALF BACK",
                   detail);
        return;
    }
    if(r->perfect) {
        snprintf(detail, sizeof(detail), "no damage  %s", note);
        draw_popup(canvas, "PERFECT", detail);
        return;
    }
    if(e->last_guard == FT_GUARD_JAM) {
        snprintf(detail, sizeof(detail), "-%d  %s", (int)r->damage, note);
        draw_popup(canvas, "JAM", detail);
        return;
    }
    if(r->damage > 0) {
        if(note[0]) {
            snprintf(detail, sizeof(detail), "-%d  %s", (int)r->damage, note);
        } else {
            snprintf(detail, sizeof(detail), "-%d", (int)r->damage);
        }
        draw_popup(canvas, "HIT", detail);
    } else {
        draw_popup(canvas, "NO DAMAGE", note[0] ? note : NULL);
    }
}

/* ---- Status ---------------------------------------------------------- */

static void draw_status(Canvas* canvas, const FtEncounter* e) {
    canvas_set_font(canvas, FontSecondary);
    char buf[12];

    canvas_draw_line(canvas, 0, FT_STATUS_Y - 1, FT_SCREEN_W - 1, FT_STATUS_Y - 1);

    /* Everything here stops at FT_STATUS_Y+6, one row short of the menu band.
     * Filled to the edge, a full Charge bar and the menu's highlight ran
     * together into a single black slab. */

    /* No "CHG" label. A quarter-ticked bar with a number beside it is already
     * unambiguous, and the three characters were the difference between a row
     * that reads and a row that is merely full. */
    const int32_t bx = 2, bw = 40;
    canvas_draw_frame(canvas, bx, FT_STATUS_Y + 1, (size_t)bw, 6);
    {
        const int32_t fill = ft_bar_fill(e->roll.current, e->stats.charge_max, bw);
        if(fill > 0) canvas_draw_box(canvas, bx + 1, FT_STATUS_Y + 2, (size_t)fill, 4);

        /* Quarter ticks turn the bar into a gauge you can read at a glance
         * instead of a featureless slab. */
        for(int32_t q = 1; q < 4; q++) {
            const int32_t tx = bx + (bw * q) / 4;
            canvas_set_color(canvas, (tx - bx - 1 < fill) ? ColorWhite : ColorBlack);
            canvas_draw_dot(canvas, tx, FT_STATUS_Y + 3);
            canvas_set_color(canvas, ColorBlack);
        }
    }

    /* "HP", not "CHG". The three numbers on this row are the whole readout
     * and a player should not have to be told twice what any of them is. */
    snprintf(buf, sizeof(buf), "HP%d", (int)e->roll.current);
    canvas_draw_str(canvas, bx + bw + 3, FT_STATUS_Y + 6, buf);

    snprintf(buf, sizeof(buf), "MP%d", (int)e->stats.ram);
    canvas_draw_str(canvas, 72, FT_STATUS_Y + 6, buf);

    /* One letter, not three: the pips beside it are self-explanatory once
     * they start filling, and the width is needed for four of them. */
    canvas_draw_str(canvas, 92, FT_STATUS_Y + 6, "SP");

    const uint8_t bars = ft_signal_bars(&e->signal);
    for(uint8_t i = 0; i < e->signal.max_bars && i < 4u; i++) {
        const int32_t sx = 103 + i * 6;
        if(e->signal.locked) {
            hatch(canvas, sx, FT_STATUS_Y + 1, 5, 6);
            canvas_draw_frame(canvas, sx, FT_STATUS_Y + 1, 5, 6);
        } else if(i < bars) {
            canvas_draw_box(canvas, sx, FT_STATUS_Y + 1, 5, 6);
        } else {
            canvas_draw_frame(canvas, sx, FT_STATUS_Y + 1, 5, 6);
        }
    }
}

/* ---- Action row ------------------------------------------------------ */

/* What the highlighted action actually does. Five three-letter buttons are
 * unreadable on their own — this row is why Defend and Focus stop looking
 * like filler. */
static const char* action_desc(const FtEncounter* e, FtAction2 a) {
    /* Something you do not *have* is worth saying. An enemy's attributes are
     * not: they move the caret, they do not refuse the module. */
    const char* blocked = ft_encounter_action_block(e, a);
    if(blocked) return blocked;

    /* When a swing has nothing it can reach, say so — the one case the caret
     * cannot explain by itself. */
    if(a == FT_ACTION_BROADCAST || a == FT_ACTION_CONTACT) {
        bool any = false;
        for(uint8_t i = 0; i < e->foe_count; i++) {
            if(ft_encounter_can_reach(e, a, i)) any = true;
        }
        if(!any) {
            return (a == FT_ACTION_CONTACT) ? "None on the floor" :
                                              "All of them shut";
        }
    }

    switch(a) {
    case FT_ACTION_BROADCAST: return "All foes, weaker.";
    case FT_ACTION_CONTACT:   return "One foe, strong.";
    case FT_ACTION_DEFEND:    return "Guard, heal, +MP";
    case FT_ACTION_FOCUS:     return "Fill SP for DEF.";
    case FT_ACTION_DEFLECT:   return "Free. Blocks bite.";
    case FT_ACTION_ITEM: {
        const FtItemId id = ft_encounter_item_at(e);
        return (id < FT_ITEM_COUNT) ? ft_item_def(id)->what : "Nothing on you.";
    }
    default:                  return "";
    }
}

/* The action row: one action at a time, with its name and what it does.
 *
 * It was a bar of three words with the attack modules a drill-down away, so
 * two of the five actions were an extra press from the player and the screen
 * carried two rows of competing buttons on top of a status strip and a
 * description. One row, five actions, LEFT and RIGHT: nothing is hidden and
 * there is one thing to look at. */
static void draw_menu(Canvas* canvas, const FtEncounter* e) {
    canvas_set_font(canvas, FontSecondary);

    const FtAction2 act = (FtAction2)e->menu_index;

    /* The Use row names what it will actually eat, not "Use". A picker with
     * a second level would be the drill-down menu all over again; the row is
     * already a left/right ring, so the item just rides in the same box. */
    char label[20];
    const char* name = ft_action_name(act);

    if(act == FT_ACTION_ITEM) {
        const FtItemId id = ft_encounter_item_at(e);

        if(id < FT_ITEM_COUNT) {
            snprintf(label, sizeof(label), "%s x%d", ft_item_def(id)->name,
                     (int)ft_pockets_count(&e->pockets, id));
            name = label;
        }
    }

    const int32_t tw = (int32_t)canvas_string_width(canvas, name);
    const int32_t bw = tw + 10;
    const int32_t bx = (FT_SCREEN_W - bw) / 2;

    canvas_draw_box(canvas, bx, FT_ACTION_Y, (size_t)bw, 10);

    canvas_set_color(canvas, ColorWhite);
    canvas_draw_str(canvas, bx + 5, FT_ACTION_Y + 7, name);
    canvas_set_color(canvas, ColorBlack);

    /* Arrows outside the box, so the box stays the thing you read. */
    canvas_draw_str(canvas, bx - 8, FT_ACTION_Y + 7, "<");
    canvas_draw_str(canvas, bx + bw + 3, FT_ACTION_Y + 7, ">");

    /* What you cannot afford is struck through where the name is — the only
     * place left for it now there is no list. */
    if(!ft_encounter_action_available(e, act)) {
        canvas_draw_line(
            canvas, bx + 5, FT_ACTION_Y + 4, bx + 5 + tw, FT_ACTION_Y + 4);
    }

    /* One line: what the coach wants to say, or what this action does. */
    const char* hint = ft_tutorial_hint(e);

    draw_centred(canvas, FT_SCREEN_W / 2, FT_ACTION_Y + 17,
                 hint ? hint : action_desc(e, act));
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
        "4/4  HP, MP, SP",
    };

    /* Four lines per page, 21 characters each: the panel's width budget. */
    static const char* const BODY[FT_HELP_PAGES][4] = {
        {
            "LEFT/RIGHT picks an",
            "action, OK takes it.",
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
            "HP: health. MP: NFC.",
            "SP arms DEFLECT free.",
            "Then a block takes 0",
            "and bites back.",
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

/* A scrolling menu: a title, a list, an optional value on each row, and a
 * scrollbar when there is more than fits.
 *
 * One renderer for every list in the game. The pause menu grew from four rows
 * to seven and each addition re-derived the spacing by hand, which is how one
 * of them ended up with its highlight touching the rules above and below. */
void ft_render_menu_list(
    Canvas*            canvas,
    const char*        title,
    const char* const* items,
    const char* const* values,
    uint8_t            count,
    uint8_t            selected) {
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);

    draw_centred(canvas, FT_SCREEN_W / 2, 8, title);
    canvas_draw_line(canvas, 0, 11, FT_SCREEN_W - 1, 11);

    const uint8_t visible = (count < FT_MENU_VISIBLE) ? count : FT_MENU_VISIBLE;
    const int32_t top = 13, step = 10;

    uint8_t first = 0;
    if(selected >= visible) first = (uint8_t)(selected - (visible - 1u));
    if(count > visible && first > count - visible) first = (uint8_t)(count - visible);

    /* Room for the scrollbar, which only appears when there is more to see. */
    const int32_t right = (count > visible) ? 118 : 124;

    for(uint8_t row = 0; row < visible; row++) {
        const uint8_t i = (uint8_t)(first + row);
        if(i >= count) break;

        const int32_t y = top + (int32_t)row * step;
        const bool on = (i == selected);

        if(on) {
            canvas_draw_box(canvas, 4, y, (size_t)(right - 4), 9);
            canvas_set_color(canvas, ColorWhite);
        }

        /* The value takes its space first and the label gets what is left.
         * Clipping the label against the row's full width instead let a long
         * value run straight through it — "Travel" and "Boot Corridor"
         * printed on top of each other, which the off-panel check cannot see
         * because both were comfortably on screen. */
        int32_t label_w = right - 14;

        if(values && values[i]) {
            const int32_t w = (int32_t)canvas_string_width(canvas, values[i]);
            const int32_t vx = right - 5 - w;

            canvas_draw_str(canvas, vx, y + 7, values[i]);
            label_w = vx - 9 - 4;
        }

        if(label_w > 0) draw_clipped(canvas, 9, y + 7, items[i], label_w);

        if(on) canvas_set_color(canvas, ColorBlack);
    }

    if(count > visible) {
        const int32_t track_y = top, track_h = visible * step - 1;
        const int32_t grip_h = (track_h * visible) / count;
        const int32_t grip_y =
            track_y + (track_h - grip_h) * (int32_t)first / (int32_t)(count - visible);

        canvas_draw_frame(canvas, 121, track_y, 5, (size_t)track_h);
        canvas_draw_box(canvas, 122, grip_y + 1, 3, (size_t)(grip_h - 2));
    }
}

void ft_render_pause(
    Canvas* canvas, uint8_t selected, bool tips_on, bool sound_on, int16_t orbs,
    bool in_battle) {
    static const char* const ITEMS[FT_PAUSE_COUNT] = {
        "Resume",
        "Pockets",
        "Orbs",
        "Quests",
        "Save",
        "Field guide",
        "How to play",
        "Tips",
        "Sound",
        "Debug",
        "New game",
        "Quit",
    };

    char orbval[12];
    const char* values[FT_PAUSE_COUNT] = {NULL};
    values[FT_PAUSE_TIPS] = tips_on ? "ON" : "OFF";
    values[FT_PAUSE_SOUND] = sound_on ? "ON" : "OFF";

    /* Rebuilding mid-fight would let a losing turn be undone by moving a
     * point, so the row says why rather than vanishing. */
    if(in_battle) {
        values[FT_PAUSE_ORBS] = "not in battle";
    } else {
        snprintf(orbval, sizeof(orbval), "%d", (int)orbs);
        values[FT_PAUSE_ORBS] = orbval;
    }

    ft_render_menu_list(
        canvas, "PAUSED", ITEMS, values, FT_PAUSE_COUNT, selected);
}

void ft_render_debug(Canvas* canvas, uint8_t selected, const char* room_name) {
    /* "Go", not "Travel": room names run to thirteen characters and the value
     * takes its space first, so a long label is a clipped label. */
    static const char* const ITEMS[FT_DEBUG_COUNT] = {
        "Go",
        "Practice arena",
        "Heal",
        "Add 100 XP",
        "Clear room",
        "Back",
    };

    const char* values[FT_DEBUG_COUNT] = {NULL};
    values[FT_DEBUG_TRAVEL] = room_name;

    ft_render_menu_list(
        canvas, "DEBUG", ITEMS, values, FT_DEBUG_COUNT, selected);
}

/* Erasing a run is the one thing on that menu that cannot be undone, so it
 * asks. OK is deliberately not the default answer. */
void ft_render_confirm(Canvas* canvas, const char* what, bool yes) {
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);

    draw_centred(canvas, FT_SCREEN_W / 2, 18, what);
    draw_centred(canvas, FT_SCREEN_W / 2, 30, "Cannot be undone.");

    static const char* const OPT[2] = {"No", "Yes"};
    static const int32_t X[2] = {26, 74};

    for(uint8_t i = 0; i < 2u; i++) {
        const bool on = (yes == (i == 1u));

        if(on) {
            canvas_draw_box(canvas, X[i], 42, 28, 12);
            canvas_set_color(canvas, ColorWhite);
        } else {
            canvas_draw_frame(canvas, X[i], 42, 28, 12);
        }

        const int32_t w = (int32_t)canvas_string_width(canvas, OPT[i]);
        canvas_draw_str(canvas, X[i] + (28 - w) / 2, 51, OPT[i]);

        if(on) canvas_set_color(canvas, ColorBlack);
    }
}

/* The practice arena's setup. Three settings and a button: a list, not a
 * form. Each row carries its value on the right, so the whole configuration
 * is readable without moving the cursor. */
void ft_render_practice(Canvas* canvas, const FtPractice* p) {
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);

    draw_centred(canvas, FT_SCREEN_W / 2, 8, "PRACTICE");
    canvas_draw_line(canvas, 0, 11, FT_SCREEN_W - 1, 11);

    /* Four rows of 10 from y=13 leaves the bottom line clear for the help
     * text; at 11 the FIGHT row and the help ran into each other. */
    for(uint8_t i = 0; i < FT_PRACTICE_ROWS; i++) {
        const int32_t y = 13 + (int32_t)i * 10;
        const bool on = (i == p->row);

        if(on) {
            canvas_draw_box(canvas, 4, y, 120, 10);
            canvas_set_color(canvas, ColorWhite);
        }

        const char* name = ft_practice_row_name(i);

        if(i == FT_PRACTICE_FIGHT) {
            /* The button reads as a button: centred, no value column. */
            const int32_t w = (int32_t)canvas_string_width(canvas, name);
            canvas_draw_str(canvas, (FT_SCREEN_W - w) / 2, y + 8, name);
        } else {
            const char* value = ft_practice_value(p, i);
            canvas_draw_str(canvas, 9, y + 8, name);

            const int32_t vw = (int32_t)canvas_string_width(canvas, value);
            const int32_t vx = 115 - vw;
            canvas_draw_str(canvas, vx, y + 8, value);

            /* Arrows only on the row you are on: three sets at once is
             * decoration, one set is an instruction. */
            if(on) {
                canvas_draw_str(canvas, vx - 6, y + 8, "<");
                canvas_draw_str(canvas, 116, y + 8, ">");
            }
        }

        if(on) canvas_set_color(canvas, ColorBlack);
    }

    draw_centred(canvas, FT_SCREEN_W / 2, 62, ft_practice_help(p));
}

/* The level-up screen. Three stats, what each is worth, and what it would
 * become — a choice nobody can make from the stat's name alone. */
void ft_render_orbs(Canvas* canvas, const FtStats* stats, uint8_t selected) {
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);

    char head[32];
    snprintf(head, sizeof(head), "L%d   %d orb%s", (int)stats->level, (int)stats->orbs,
             (stats->orbs == 1) ? "" : "s");
    draw_centred(canvas, FT_SCREEN_W / 2, 8, head);
    canvas_draw_line(canvas, 0, 11, FT_SCREEN_W - 1, 11);

    static const char* const NAMES[FT_UP_COUNT] = {"HP", "MP", "Cards"};
    static const FtLevelChoice CHOICE[FT_UP_COUNT] = {FT_UP_CHARGE, FT_UP_RAM, FT_UP_FLASH};

    const int16_t now[FT_UP_COUNT] = {
        stats->charge_max, stats->ram_max, stats->flash_max};

    for(uint8_t i = 0; i < FT_UP_COUNT; i++) {
        const int32_t y = 14 + (int32_t)i * 12;
        const bool    on = (i == selected);

        if(on) {
            canvas_draw_box(canvas, 4, y, 120, 11);
            canvas_set_color(canvas, ColorWhite);
        }

        canvas_draw_str(canvas, 9, y + 8, NAMES[i]);

        /* Value, then how many orbs are sitting in it. Seeing "35 (4)" is
         * what tells you there are four to take back out. */
        char value[20];
        snprintf(value, sizeof(value), "%d (%d)", (int)now[i], (int)stats->spent[CHOICE[i]]);

        const int32_t vw = (int32_t)canvas_string_width(canvas, value);
        canvas_draw_str(canvas, 119 - vw, y + 8, value);

        if(on) canvas_set_color(canvas, ColorBlack);
    }

    draw_centred(canvas, FT_SCREEN_W / 2, 62, "LEFT/RIGHT to move");
}

void ft_render_pockets(
    Canvas* canvas, const FtPockets* p, uint8_t selected, const FtStats* stats) {
    const uint8_t kinds = ft_pockets_kinds(p);

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);

    char head[28];
    snprintf(head, sizeof(head), "POCKETS  %d/%d", (int)ft_pockets_used(p),
             FT_POCKET_MAX);
    draw_centred(canvas, FT_SCREEN_W / 2, 8, head);
    canvas_draw_line(canvas, 0, 11, FT_SCREEN_W - 1, 11);

    if(kinds == 0u) {
        draw_centred(canvas, FT_SCREEN_W / 2, 30, "Nothing on you.");
        draw_centred(canvas, FT_SCREEN_W / 2, 44, "Face a tree and");
        draw_centred(canvas, FT_SCREEN_W / 2, 54, "press OK.");
        return;
    }

    for(uint8_t i = 0; i < kinds && i < 4u; i++) {
        const FtItemId   id = ft_pockets_nth(p, i);
        const FtItemDef* d = ft_item_def(id);
        const int32_t    y = 14 + (int32_t)i * 11;
        const bool       on = (i == selected);

        if(on) {
            canvas_draw_box(canvas, 2, y, FT_SCREEN_W - 4, 10);
            canvas_set_color(canvas, ColorWhite);
        }

        char row[28];
        snprintf(row, sizeof(row), "%s x%d", d->name, (int)ft_pockets_count(p, id));
        canvas_draw_str(canvas, 6, y + 8, row);

        const int32_t vw = (int32_t)canvas_string_width(canvas, d->what);
        canvas_draw_str(canvas, FT_SCREEN_W - 6 - vw, y + 8, d->what);

        if(on) canvas_set_color(canvas, ColorBlack);
    }

    /* Whether eating it here would do anything, so OK is never a wasted
     * slot. Full HP with only apples on you is a thing worth being told. */
    const FtItemId at = ft_pockets_nth(p, (selected < kinds) ? selected : 0u);
    const bool worth = (at < FT_ITEM_COUNT) &&
                       ft_item_useful(at, stats->charge, stats->charge_max,
                                      stats->ram, stats->ram_max);

    draw_centred(canvas, FT_SCREEN_W / 2, 62, worth ? "OK to use" : "Nothing to mend");
}

void ft_render_quests(Canvas* canvas, const FtQuests* q, uint8_t selected) {
    static const char* names[FT_QUEST_COUNT];
    static const char* values[FT_QUEST_COUNT];

    for(uint8_t i = 0; i < FT_QUEST_COUNT; i++) {
        names[i] = ft_quest_def((FtQuestId)i)->name;
        values[i] = ft_quest_status_line(q, (FtQuestId)i);
    }

    ft_render_menu_list(canvas, "QUESTS", names, values, FT_QUEST_COUNT, selected);
}

void ft_render_talk(
    Canvas* canvas, const FtTalk* t, uint8_t beat, bool choosing, bool yes) {
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);

    if(t->count == 0u) return;
    if(beat >= t->count) beat = (uint8_t)(t->count - 1u);

    const FtBeat* at = &t->beats[beat];
    const bool    mine = (at->who == FT_SAY_YOU);

    /* Whoever is talking, named. This is the whole of what makes it read as
     * two people rather than a sign on a wall. */
    const char* who = mine ? "You" : t->speaker;

    if(mine) {
        /* Your own lines are inverted, so a glance at the shape of the
         * screen tells you whose turn it is without reading the name. */
        const int32_t w = (int32_t)canvas_string_width(canvas, who) + 6;
        canvas_draw_box(canvas, FT_SCREEN_W - 2 - w, 1, (size_t)w, 10);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_str(canvas, FT_SCREEN_W - 2 - w + 3, 9, who);
        canvas_set_color(canvas, ColorBlack);
    } else {
        canvas_draw_str(canvas, 2, 9, who);
    }
    canvas_draw_line(canvas, 0, 12, FT_SCREEN_W - 1, 12);

    canvas_draw_frame(canvas, 2, 16, FT_SCREEN_W - 4, 30);
    if(at->a) canvas_draw_str(canvas, 6, 28, at->a);
    if(at->b) canvas_draw_str(canvas, 6, 40, at->b);

    /* How far through, as pips. A conversation you cannot see the end of is
     * one you start mashing through. */
    for(uint8_t i = 0; i < t->count && i < 10u; i++) {
        const int32_t px = FT_SCREEN_W / 2 - (int32_t)t->count * 2 + (int32_t)i * 4;
        if(i <= beat) {
            canvas_draw_box(canvas, px, 50, 3, 3);
        } else {
            canvas_draw_frame(canvas, px, 50, 3, 3);
        }
    }

    if(choosing) {
        char left[24], right[24];
        snprintf(left, sizeof(left), "%s%s%s", yes ? "[" : " ", t->yes ? t->yes : "Yes",
                 yes ? "]" : " ");
        snprintf(right, sizeof(right), "%s%s%s", yes ? " " : "[", t->no ? t->no : "No",
                 yes ? " " : "]");

        const int32_t lw = (int32_t)canvas_string_width(canvas, left);
        const int32_t rw = (int32_t)canvas_string_width(canvas, right);
        const int32_t gap = 8;
        int32_t x = (FT_SCREEN_W - (lw + gap + rw)) / 2;
        if(x < 2) x = 2;

        canvas_draw_str(canvas, x, 62, left);
        canvas_draw_str(canvas, x + lw + gap, 62, right);
        return;
    }

    draw_centred(canvas, FT_SCREEN_W / 2, 62,
                 (beat + 1u >= t->count) ? "OK" : "OK >");
}

/* The field guide's index. Empty until something has been fought, because a
 * guide that lists what you have not met is a manual, not a record. */
void ft_render_guide_list(Canvas* canvas, const FtGuide* g, uint8_t selected) {
    const uint8_t n = ft_guide_count(g);

    if(n == 0u) {
        canvas_clear(canvas);
        canvas_set_color(canvas, ColorBlack);
        canvas_set_font(canvas, FontSecondary);

        draw_centred(canvas, FT_SCREEN_W / 2, 8, "FIELD GUIDE");
        canvas_draw_line(canvas, 0, 11, FT_SCREEN_W - 1, 11);
        draw_centred(canvas, FT_SCREEN_W / 2, 30, "Nothing met yet.");
        draw_centred(canvas, FT_SCREEN_W / 2, 42, "Everything you fight");
        draw_centred(canvas, FT_SCREEN_W / 2, 52, "writes itself here.");
        return;
    }

    /* Names, and what to hit each one with. The value column used to be
     * empty, so the list was ten names and no information — you had to open
     * every page to find the one you wanted. */
    static char names[FT_ENEMY_COUNT][20];
    const char* items[FT_ENEMY_COUNT];
    const char* values[FT_ENEMY_COUNT];

    for(uint8_t i = 0; i < n; i++) {
        const FtEnemyId id = ft_guide_nth(g, i);

        snprintf(names[i], sizeof(names[i]), "%s", FT_ENEMIES[id].name);
        items[i] = names[i];
        values[i] = ft_guide_tag(id);
    }

    ft_render_menu_list(canvas, "FIELD GUIDE", items, values, n, selected);
}

/* One entry: what it is, what its traits cost you, and what it throws. */
void ft_render_guide_entry(Canvas* canvas, FtEnemyId id) {
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);

    if(id >= FT_ENEMY_COUNT) return;
    const FtEnemy* en = &FT_ENEMIES[id];

    draw_clipped(canvas, 2, 8, en->name, FT_SCREEN_W - 4);
    canvas_draw_line(canvas, 0, 11, FT_SCREEN_W - 1, 11);

    /* The sprite, so the page and the thing in the corridor match. */
    draw_sprite(canvas, ft_enemy_art(id), FT_SCREEN_W - 18, 14);

    char line[28];

    /* Six rows of eight, which is what fits and what the busiest entry needs:
     * vitals, the advice, two traits and two attacks. */
    ft_guide_vitals(id, line, (uint8_t)sizeof(line));
    draw_clipped(canvas, 2, 20, line, FT_SCREEN_W - 24);

    /* The advice gets a marker, because it is the only line on this page the
     * player can act on and the rest is background. */
    const char* advice = ft_guide_advice(id);
    canvas_draw_box(canvas, 2, 24, 3, 6);
    draw_clipped(canvas, 8, 29, advice, FT_SCREEN_W - 30);

    int32_t y = 38;
    for(uint8_t i = 0; i < 2u && y <= 62; i++) {
        const char* note = ft_guide_note(id, i);
        if(!note) break;

        draw_clipped(canvas, 2, y, note, FT_SCREEN_W - 4);
        y += 8;
    }

    for(uint8_t i = 0; i < FT_ENEMY_MAX_ATTACKS && y <= 62; i++) {
        if(!ft_guide_attack_line(id, i, line, (uint8_t)sizeof(line))) break;

        draw_clipped(canvas, 2, y, line, FT_SCREEN_W - 4);
        y += 8;
    }

    /* Something that never takes a turn says so, rather than leaving the
     * bottom of the page blank and the player wondering what it hits for. */
    if(ft_guide_attack_count(id) == 0u && y <= 62) {
        draw_clipped(canvas, 2, y, "Never attacks.", FT_SCREEN_W - 4);
    }
}

/* ---- Entry ----------------------------------------------------------- */

/* The iris: a closing ring of black drawn from the screen edges inward, then
 * reopened. Implemented as four growing bars rather than a filled circle
 * because at 128x64 a true circle's corners are the whole effect. */
void ft_render_iris(Canvas* canvas, uint8_t amount) {
    if(amount == 0u) return;

    if(amount >= 250u) {
        canvas_draw_box(canvas, 0, 0, FT_SCREEN_W, FT_SCREEN_H);
        return;
    }

    const int32_t hx = (FT_SCREEN_W / 2) * amount / 255;
    const int32_t hy = (FT_SCREEN_H / 2) * amount / 255;

    canvas_draw_box(canvas, 0, 0, (size_t)hx, FT_SCREEN_H);
    canvas_draw_box(canvas, FT_SCREEN_W - hx, 0, (size_t)hx, FT_SCREEN_H);
    canvas_draw_box(canvas, 0, 0, FT_SCREEN_W, (size_t)hy);
    canvas_draw_box(canvas, 0, FT_SCREEN_H - hy, FT_SCREEN_W, (size_t)hy);

    /* Bevel the corners so the closing edge reads as a ring rather than a
     * rectangle shrinking. */
    for(int32_t i = 0; i < 6; i++) {
        const int32_t cx = hx + i;
        const int32_t cy = hy + (5 - i);
        canvas_draw_box(canvas, cx, 0, 2, (size_t)cy);
        canvas_draw_box(canvas, FT_SCREEN_W - cx - 2, 0, 2, (size_t)cy);
        canvas_draw_box(canvas, cx, FT_SCREEN_H - cy, 2, (size_t)cy);
        canvas_draw_box(canvas, FT_SCREEN_W - cx - 2, FT_SCREEN_H - cy, 2, (size_t)cy);
    }
}

void ft_render_battle(Canvas* canvas, const FtEncounter* e) {
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    if(e->phase == FT_PHASE_WIN || e->phase == FT_PHASE_LOSE) {
        const bool won = (e->phase == FT_PHASE_WIN);
        char detail[26];

        canvas_set_font(canvas, FontSecondary);
        draw_centred(canvas, FT_SCREEN_W / 2, 20, won ? "VICTORY" : "DOWNED");

        if(won) {
            snprintf(detail, sizeof(detail), "+%d XP", (int)ft_encounter_xp(e));
        } else {
            snprintf(detail, sizeof(detail), "out of HP");
        }
        draw_centred(canvas, FT_SCREEN_W / 2, 32, detail);

        /* Losing costs the run everything since the last save, so the screen
         * says so before the player presses anything. */
        if(!won) draw_centred(canvas, FT_SCREEN_W / 2, 42, "Back to your save");

        /* Baselines stay at or below 62: a glyph cell extends one row past
         * its baseline, and descenders need that row. */
        draw_centred(canvas, FT_SCREEN_W / 2, 52, won ? "OK: carry on" : "OK: continue");
        draw_centred(canvas, FT_SCREEN_W / 2, 62, "UP: how to play");
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
        draw_menu(canvas, e); /* one row, and its own line */
    } else if(hint) {
        /* Everywhere else the action row is free, so the coach speaks there. */
        draw_prompt(canvas, hint);
    } else if(e->phase == FT_PHASE_PLAYER_ACT) {
        draw_prompt(canvas, "OK to strike");
    } else if(e->phase == FT_PHASE_TELEGRAPH) {
        draw_prompt(canvas, "OK to guard");
    }

}
