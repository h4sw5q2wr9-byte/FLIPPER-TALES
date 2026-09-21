/* Renders every battle phase to a .pbm so the 128x64 layout can be reviewed
 * as an image, and fails if anything is drawn off-panel. */
#include <gui/gui.h>

#include <stdio.h>
#include <string.h>

#include "ft_encounter.h"
#include "ft_render.h"

typedef struct {
    const char* name;
    FtEnemyId   enemy;
    FtPhase     phase;
    uint32_t    phase_ms;
    uint8_t     menu_index;
    int         variant;
} Shot;

static void build(FtEncounter* e, const Shot* s) {
    FtLoadout lo;
    ft_loadout_init(&lo);
    ft_encounter_init(e, s->enemy, &lo, 42);

    e->phase = s->phase;
    e->phase_ms = s->phase_ms;
    e->menu_index = s->menu_index;

    /* Mid-fight numbers read better than a pristine board. Kept within the
     * real maximum: feeding impossible values hides genuine bar bugs. */
    e->stats.charge_max = 25;
    e->roll.current = 13;
    e->roll.target = 13;
    e->stats.charge = 13;
    e->enemy_charge = (int16_t)(e->enemy_charge_max / 2);
    e->signal.value = 70;

    /* Widest case for the pip row: four bars, three of them filled. */
    if(s->enemy == FT_ENEMY_SEALED_LOCK) {
        e->signal.max_bars = 4;
        e->signal.value = 320;
    }

    switch(s->phase) {
    case FT_PHASE_TELEGRAPH:
    case FT_PHASE_IMPACT:
        e->enemy_attack_index = (uint8_t)s->variant;
        break;
    case FT_PHASE_PLAYER_ACT:
        e->action_pressed = (s->variant != 0);
        break;
    case FT_PHASE_RESULT:
        e->last_rating = FT_RATING_EXCELLENT;
        e->last_player_hit.outcome = (s->variant == 1) ? FT_HIT_LOCKED : FT_HIT_OK;
        e->last_player_hit.damage = (s->variant == 1) ? 0 : 8;
        e->last_player_hit.ram_refund = (s->variant == 1) ? 1 : 0;
        break;
    default:
        break;
    }

    if(s->phase == FT_PHASE_IMPACT) {
        e->last_guard = (s->variant == 0) ? FT_GUARD_CAPTURE : FT_GUARD_JAM;
        e->last_enemy_hit.captured = (s->variant == 0);
        e->last_enemy_hit.damage = (s->variant == 0) ? 0 : 3;
        e->last_capture_was_new = true;
    }

    /* A couple of stored signals, so the win screen has something to report. */
    ft_siglib_capture(&e->lib, 10);
    ft_siglib_capture(&e->lib, 11);
}

int main(void) {
    static const Shot shots[] = {
        {"menu-plain",      FT_ENEMY_STRAY_PACKET, FT_PHASE_MENU,       0,    0, 0},
        {"menu-airborne",   FT_ENEMY_DRIFT_BEACON, FT_PHASE_MENU,       0,    1, 0},
        {"menu-encrypted",  FT_ENEMY_SEALED_LOCK,  FT_PHASE_MENU,       0,    0, 0},
        {"menu-last",       FT_ENEMY_SEALED_LOCK,  FT_PHASE_MENU,       0,    3, 0},
        /* Phase times include the FT_READY_MS lead-in. */
        {"strike-ready",    FT_ENEMY_STRAY_PACKET, FT_PHASE_PLAYER_ACT, 200,  1, 0},
        {"strike-early",    FT_ENEMY_STRAY_PACKET, FT_PHASE_PLAYER_ACT, 620,  1, 0},
        {"strike-perfect",  FT_ENEMY_STRAY_PACKET, FT_PHASE_PLAYER_ACT, 850,  1, 0},
        {"result-hit",      FT_ENEMY_STRAY_PACKET, FT_PHASE_RESULT,     100,  1, 0},
        {"result-locked",   FT_ENEMY_SEALED_LOCK,  FT_PHASE_RESULT,     100,  0, 1},
        {"telegraph-ready", FT_ENEMY_DRIFT_BEACON, FT_PHASE_TELEGRAPH,  250,  0, 0},
        {"telegraph-far",   FT_ENEMY_DRIFT_BEACON, FT_PHASE_TELEGRAPH,  800,  0, 0},
        {"telegraph-near",  FT_ENEMY_DRIFT_BEACON, FT_PHASE_TELEGRAPH,  1270, 0, 0},
        {"telegraph-guard", FT_ENEMY_DRIFT_BEACON, FT_PHASE_TELEGRAPH,  1240, 0, 1},
        {"telegraph-undo",  FT_ENEMY_SEALED_LOCK,  FT_PHASE_TELEGRAPH,  1100, 0, 1},
        {"impact-capture",  FT_ENEMY_DRIFT_BEACON, FT_PHASE_IMPACT,     100,  0, 0},
        {"impact-jam",      FT_ENEMY_DRIFT_BEACON, FT_PHASE_IMPACT,     100,  0, 1},
        {"win",             FT_ENEMY_STRAY_PACKET, FT_PHASE_WIN,        100,  0, 0},
        {"lose",            FT_ENEMY_SEALED_LOCK,  FT_PHASE_LOSE,       100,  0, 0},
    };

    Canvas* canvas = ft_stub_canvas_alloc();
    int total_clipped = 0;

    ft_render_help(canvas);
    ft_stub_canvas_write_pbm(canvas, "preview/99_help.pbm");
    total_clipped += ft_stub_canvas_clipped(canvas);
    printf("  %-18s %s\n", "help", ft_stub_canvas_clipped(canvas) ? "CLIPPED" : "ok");

    for(size_t i = 0; i < sizeof(shots) / sizeof(shots[0]); i++) {
        FtEncounter e;
        build(&e, &shots[i]);

        ft_render_battle(canvas, &e);

        char path[128];
        snprintf(path, sizeof(path), "preview/%02zu_%s.pbm", i, shots[i].name);
        ft_stub_canvas_write_pbm(canvas, path);

        const int clipped = ft_stub_canvas_clipped(canvas);
        total_clipped += clipped;

        printf("  %-18s %s\n", shots[i].name,
               clipped ? "CLIPPED" : "ok");
        if(clipped) printf("      %d pixel(s) drawn off-panel\n", clipped);
    }

    ft_stub_canvas_free(canvas);

    printf("\n%s\n", total_clipped ? "LAYOUT FAILED: content is drawn off-screen"
                                   : "layout clean: nothing drawn off-screen");
    return total_clipped ? 1 : 0;
}
