/* Renders every battle phase to a .pbm so the 128x64 layout can be reviewed
 * as an image, and fails if anything is drawn off-panel. */
#include <gui/gui.h>

#include <stdio.h>
#include <string.h>

#include "ft_encounter.h"
#include "ft_practice.h"
#include "ft_world.h"
#include "ft_render.h"
#include "ft_tutorial.h"

typedef struct {
    const char* name;
    FtEnemyId   enemy;
    FtPhase     phase;
    uint32_t    phase_ms;
    uint8_t     menu_index;
    int         variant;
    uint8_t     extra_foes; /* 0 = duel, else a mixed group */
    bool        panel;      /* true = the Attack module panel is open */
} Shot;

/* The menu is two levels deep now, and menu_index is derived from the two
 * cursors rather than being one of them. Shots name the action they want to
 * show, so the cursors are worked backwards from it. */
static void aim_menu(FtEncounter* e, uint8_t action, bool panel) {
    for(uint8_t i = 0; i < FT_ATTACK_COUNT; i++) {
        if((uint8_t)FT_ATTACK_ITEMS[i] == action) e->attack_index = i;
    }

    if(panel) {
        e->menu_level = FT_MENU_ATTACK;
        e->root_index = FT_ROOT_ATTACK;
        return;
    }

    e->menu_level = FT_MENU_ROOT;
    e->root_index = (action == (uint8_t)FT_ACTION_DEFEND) ? FT_ROOT_DEFEND :
                    (action == (uint8_t)FT_ACTION_FOCUS)  ? FT_ROOT_FOCUS :
                                                            FT_ROOT_ATTACK;
}

static void build(FtEncounter* e, const Shot* s) {
    FtLoadout lo;
    ft_loadout_init(&lo);

    if(s->extra_foes) {
        const FtEnemyId group[FT_MAX_ENEMIES] = {
            FT_ENEMY_STRAY_PACKET, FT_ENEMY_DRIFT_BEACON, FT_ENEMY_SEALED_LOCK};
        ft_encounter_init(e, group, s->extra_foes, &lo, 42);
    } else {
        ft_encounter_init_single(e, s->enemy, &lo, 42);
    }

    e->phase = s->phase;
    e->phase_ms = s->phase_ms;
    e->menu_index = s->menu_index;
    aim_menu(e, s->menu_index, s->panel);

    /* Mid-fight numbers read better than a pristine board. Kept within the
     * real maximum: feeding impossible values hides genuine bar bugs. */
    e->stats.charge_max = 25;
    e->roll.current = 13;
    e->roll.target = 13;
    e->stats.charge = 13;
    e->signal.value = 70;

    for(uint8_t i = 0; i < e->foe_count; i++) {
        e->foes[i].charge = (int16_t)((e->foes[i].charge_max * 2) / 3);
    }

    /* Widest case for the pip row: four bars, three of them filled. */
    if(s->enemy == FT_ENEMY_SEALED_LOCK) {
        e->signal.max_bars = 4;
        e->signal.value = 320;
    }

    switch(s->phase) {
    case FT_PHASE_TELEGRAPH:
    case FT_PHASE_IMPACT:
        e->foes[e->acting_foe].attack_index = (uint8_t)s->variant;
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

    if(s->variant == 9) e->coach = false;

    /* Variant 7 kills the row, so the defeat fold has something to play. */
    if(s->variant == 7) {
        for(uint8_t i = 0; i < e->foe_count; i++) {
            e->foe_charge_before[i] = e->foes[i].charge_max;
            e->foes[i].charge = 0;
            e->foe_hit_valid[i] = true;
            e->foe_hits[i].outcome = FT_HIT_OK;
            e->foe_hits[i].damage = e->foes[i].charge_max;
        }
        e->last_player_hit = e->foe_hits[0];
    }

    /* A broadcast result needs per-foe outcomes filled in. */
    if(s->phase == FT_PHASE_RESULT && s->menu_index == FT_ACTION_BROADCAST) {
        for(uint8_t i = 0; i < e->foe_count; i++) {
            const bool encrypted =
                (FT_ENEMIES[e->foes[i].id].attrs & FT_ATTR_ENCRYPTED) != 0u;

            e->foe_hit_valid[i] = true;
            e->foe_hits[i].outcome = encrypted ? FT_HIT_LOCKED : FT_HIT_OK;
            e->foe_hits[i].damage = encrypted ? 0 : 3;
            e->foe_hits[i].ram_refund = encrypted ? 1 : 0;
        }
        e->last_player_hit = e->foe_hits[0];
    }

    /* A couple of stored signals, so the win screen has something to report. */
    ft_siglib_capture(&e->lib, 10);
    ft_siglib_capture(&e->lib, 11);
}

int main(void) {
    static const Shot shots[] = {
        {"menu-plain",      FT_ENEMY_STRAY_PACKET, FT_PHASE_MENU,       0,    0, 0, 0, false},
        {"menu-airborne",   FT_ENEMY_DRIFT_BEACON, FT_PHASE_MENU,       0,    1, 0, 0, false},
        {"menu-encrypted",  FT_ENEMY_SEALED_LOCK,  FT_PHASE_MENU,       0,    0, 0, 0, false},
        {"menu-last",       FT_ENEMY_SEALED_LOCK,  FT_PHASE_MENU,       0,    3, 0, 0, false},
        {"menu-nocoach",    FT_ENEMY_STRAY_PACKET, FT_PHASE_MENU,       0,    1, 9, 0, false},
        /* Phase times include the FT_READY_MS lead-in. */
        {"strike-ready",    FT_ENEMY_STRAY_PACKET, FT_PHASE_PLAYER_ACT, 200,  1, 0, 0, false},
        {"strike-early",    FT_ENEMY_STRAY_PACKET, FT_PHASE_PLAYER_ACT, 620,  1, 0, 0, false},
        {"strike-perfect",  FT_ENEMY_STRAY_PACKET, FT_PHASE_PLAYER_ACT, 850,  1, 0, 0, false},
        {"anim-strike",     FT_ENEMY_STRAY_PACKET, FT_PHASE_RESULT,     110,  1, 0, 0, false},
        {"result-hit",      FT_ENEMY_STRAY_PACKET, FT_PHASE_RESULT,    1100,  1, 0, 0, false},
        {"result-group",    FT_ENEMY_STRAY_PACKET, FT_PHASE_RESULT,    1100,  0, 0, 3, false},
        {"telegraph-ready", FT_ENEMY_DRIFT_BEACON, FT_PHASE_TELEGRAPH,  250,  0, 0, 0, false},
        {"telegraph-far",   FT_ENEMY_DRIFT_BEACON, FT_PHASE_TELEGRAPH,  800,  0, 0, 0, false},
        {"telegraph-near",  FT_ENEMY_DRIFT_BEACON, FT_PHASE_TELEGRAPH,  1270, 0, 0, 0, false},
        {"telegraph-guard", FT_ENEMY_DRIFT_BEACON, FT_PHASE_TELEGRAPH,  1240, 0, 1, 0, false},
        {"telegraph-undo",  FT_ENEMY_SEALED_LOCK,  FT_PHASE_TELEGRAPH,  1100, 0, 1, 0, false},
        {"anim-incoming",   FT_ENEMY_SEALED_LOCK,  FT_PHASE_IMPACT,     110,  0, 1, 0, false},
        {"anim-capture",    FT_ENEMY_DRIFT_BEACON, FT_PHASE_IMPACT,     110,  0, 0, 0, false},
        {"impact-capture",  FT_ENEMY_DRIFT_BEACON, FT_PHASE_IMPACT,    1100,  0, 0, 0, false},
        {"impact-jam",      FT_ENEMY_DRIFT_BEACON, FT_PHASE_IMPACT,    1100,  0, 1, 0, false},
        {"group3-menu",     FT_ENEMY_STRAY_PACKET, FT_PHASE_MENU,       0,    0, 0, 3, false},
        {"group3-target",   FT_ENEMY_STRAY_PACKET, FT_PHASE_MENU,       0,    1, 0, 3, false},
        {"group2-signal",   FT_ENEMY_STRAY_PACKET, FT_PHASE_MENU,       0,    4, 0, 2, false},
        {"anim-bcast",      FT_ENEMY_STRAY_PACKET, FT_PHASE_RESULT,     560,  0, 0, 3, false},
        {"anim-contact",    FT_ENEMY_STRAY_PACKET, FT_PHASE_RESULT,     560,  1, 0, 2, false},
        {"anim-foe-bcast",  FT_ENEMY_DRIFT_BEACON, FT_PHASE_IMPACT,     560,  0, 1, 0, false},
        /* The Attack panel: the second menu level, over a full three-foe row. */
        {"panel-bcast",     FT_ENEMY_STRAY_PACKET, FT_PHASE_MENU,       0,    0, 0, 3, true},
        {"panel-signal",    FT_ENEMY_STRAY_PACKET, FT_PHASE_MENU,       0,    4, 0, 3, true},
        {"panel-locked",    FT_ENEMY_SEALED_LOCK,  FT_PHASE_MENU,       0,    0, 0, 0, true},
        {"root-defend",     FT_ENEMY_STRAY_PACKET, FT_PHASE_MENU,       0,    2, 0, 3, false},
        {"root-focus",      FT_ENEMY_STRAY_PACKET, FT_PHASE_MENU,       0,    3, 0, 3, false},
        /* Taking a hit: the flinch strobe, then the result. A landed hit is
         * variant 1. There is no iris here any more — that belongs to the
         * scene wipe, not to every single connect. */
        {"flinch",          FT_ENEMY_DRIFT_BEACON, FT_PHASE_IMPACT,     760,  0, 1, 0, false},
        {"hit-taken",       FT_ENEMY_DRIFT_BEACON, FT_PHASE_IMPACT,    1300,  0, 1, 0, false},
        /* The travelling broadcast, late in its flight across the row. */
        {"bcast-late",      FT_ENEMY_STRAY_PACKET, FT_PHASE_RESULT,     860,  0, 0, 3, false},
        /* The strike, frame by frame: wind-up, the dash, the burst, recovery. */
        {"hit-windup",      FT_ENEMY_STRAY_PACKET, FT_PHASE_RESULT,     200,  1, 0, 2, false},
        {"hit-dash",        FT_ENEMY_STRAY_PACKET, FT_PHASE_RESULT,     560,  1, 0, 2, false},
        {"hit-burst",       FT_ENEMY_STRAY_PACKET, FT_PHASE_RESULT,     700,  1, 0, 2, false},
        {"hit-recover",     FT_ENEMY_STRAY_PACKET, FT_PHASE_RESULT,     830,  1, 0, 2, false},
        /* And a foe going down, across the fold. */
        {"die-early",       FT_ENEMY_STRAY_PACKET, FT_PHASE_RESULT,     740,  0, 7, 3, false},
        {"die-mid",         FT_ENEMY_STRAY_PACKET, FT_PHASE_RESULT,     800,  0, 7, 3, false},
        {"die-late",        FT_ENEMY_STRAY_PACKET, FT_PHASE_RESULT,     870,  0, 7, 3, false},
        /* Every enemy, so new art is looked at rather than assumed. */
        {"foe-crawler",     FT_ENEMY_SCRAP_CRAWLER, FT_PHASE_MENU,      0,    0, 0, 0, false},
        {"foe-rime",        FT_ENEMY_RIME_SHELL,    FT_PHASE_MENU,      0,    0, 0, 0, false},
        {"foe-drone",       FT_ENEMY_GATE_DRONE,    FT_PHASE_MENU,      0,    0, 0, 0, false},
        {"foe-relay",       FT_ENEMY_MAST_RELAY,    FT_PHASE_MENU,      0,    0, 0, 0, false},
        {"foe-null",        FT_ENEMY_NULL_FIELD,    FT_PHASE_MENU,      0,    0, 0, 0, false},
        {"win",             FT_ENEMY_STRAY_PACKET, FT_PHASE_WIN,        100,  0, 0, 0, false},
        {"lose",            FT_ENEMY_SEALED_LOCK,  FT_PHASE_LOSE,       100,  0, 0, 0, false},
    };

    Canvas* canvas = ft_stub_canvas_alloc();
    int total_clipped = 0;

    /* The menus are screens too, and they were the two nobody was looking at
     * until a row was added to one of them. */
    for(uint8_t i = 0; i < FT_PAUSE_COUNT; i++) {
        ft_render_pause(canvas, i, (i % 2) != 0);

        char pp[64];
        snprintf(pp, sizeof(pp), "preview/80_pause%u.pbm", i);
        ft_stub_canvas_write_pbm(canvas, pp);

        const int c = ft_stub_canvas_clipped(canvas);
        total_clipped += c;
        printf("  pause row %u         %s\n", i, c ? "CLIPPED" : "ok");
    }

    {
        /* The debug menu, every row, with the widest room name it can show. */
        const char* widest = ft_room(0)->map->name;
        for(uint8_t r = 0; r < ft_room_count(); r++) {
            if(strlen(ft_room(r)->map->name) > strlen(widest)) {
                widest = ft_room(r)->map->name;
            }
        }

        for(uint8_t i = 0; i < FT_DEBUG_COUNT; i++) {
            ft_render_debug(canvas, i, widest);

            char dp[64];
            snprintf(dp, sizeof(dp), "preview/84_debug%u.pbm", i);
            ft_stub_canvas_write_pbm(canvas, dp);

            const int c = ft_stub_canvas_clipped(canvas);
            total_clipped += c;
            printf("  debug row %u         %s\n", i, c ? "CLIPPED" : "ok");
        }
    }

    {
        /* The irreversible gate, both ways round. */
        for(uint8_t i = 0; i < 2u; i++) {
            ft_render_confirm(canvas, "Erase this run?", i != 0u);

            char cp[64];
            snprintf(cp, sizeof(cp), "preview/86_confirm%u.pbm", i);
            ft_stub_canvas_write_pbm(canvas, cp);

            const int c = ft_stub_canvas_clipped(canvas);
            total_clipped += c;
            printf("  confirm %u           %s\n", i, c ? "CLIPPED" : "ok");
        }
    }

    {
        /* The level-up screen: each row, and the capped case, which changes
         * what the value column says. */
        FtStats st;
        ft_stats_init(&st);
        st.level = 7;

        for(uint8_t i = 0; i < 3u; i++) {
            ft_render_levelup(canvas, &st, i, (i == 0u) ? 3 : 1);

            char lp[64];
            snprintf(lp, sizeof(lp), "preview/87_level%u.pbm", i);
            ft_stub_canvas_write_pbm(canvas, lp);

            const int c = ft_stub_canvas_clipped(canvas);
            total_clipped += c;
            printf("  level row %u         %s\n", i, c ? "CLIPPED" : "ok");
        }

        /* Every stat at its cap, and at three digits, which is the widest the
         * value column ever gets. */
        st.charge_max = FT_CAP_CHARGE;
        st.ram_max = FT_CAP_RAM;
        st.flash_max = FT_CAP_FLASH;
        st.level = 99;

        ft_render_levelup(canvas, &st, 0, 1);
        ft_stub_canvas_write_pbm(canvas, "preview/88_levelcap.pbm");
        total_clipped += ft_stub_canvas_clipped(canvas);
        printf("  level capped        %s\n",
               ft_stub_canvas_clipped(canvas) ? "CLIPPED" : "ok");
    }

    {
        /* Every row of the arena, and the widest value each one can show. */
        FtPractice pr;
        ft_practice_init(&pr, 1u);

        for(uint8_t r = 0; r < FT_PRACTICE_ROWS; r++) {
            pr.row = r;
            pr.group = (uint8_t)(FT_PRACTICE_GROUPS - 1u);
            pr.level = FT_PRACTICE_MAX_LEVEL;
            pr.kit = FT_KIT_LOADED;

            ft_render_practice(canvas, &pr);

            char pp[64];
            snprintf(pp, sizeof(pp), "preview/85_arena%u.pbm", r);
            ft_stub_canvas_write_pbm(canvas, pp);

            const int c = ft_stub_canvas_clipped(canvas);
            total_clipped += c;
            printf("  arena row %u         %s\n", r, c ? "CLIPPED" : "ok");
        }

        /* And every value of every setting, checked for overflow rather than
         * eyeballed: the value column is right-aligned and a long label there
         * runs into the row name. */
        for(uint8_t r = 0; r < FT_PRACTICE_ROWS; r++) {
            for(int i = 0; i < 24; i++) {
                pr.row = r;
                ft_practice_adjust(&pr, 1);
                ft_render_practice(canvas, &pr);
                total_clipped += ft_stub_canvas_clipped(canvas);
            }
        }
    }

    for(uint8_t page = 0; page < FT_HELP_PAGES; page++) {
        ft_render_help(canvas, page);

        char hp[64];
        snprintf(hp, sizeof(hp), "preview/9%u_help%u.pbm", page, page + 1u);
        ft_stub_canvas_write_pbm(canvas, hp);

        const int c = ft_stub_canvas_clipped(canvas);
        total_clipped += c;
        printf("  help page %u        %s\n", page + 1u, c ? "CLIPPED" : "ok");
    }

    /* Every coaching line must fit the panel, whatever state produces it. */
    for(size_t i = 0; i < sizeof(shots) / sizeof(shots[0]); i++) {
        FtEncounter e;
        build(&e, &shots[i]);
        const char* hint = ft_tutorial_hint(&e);
        if(hint && strlen(hint) > FT_TUTORIAL_MAX_CHARS) {
            printf("  HINT TOO LONG (%zu): \"%s\"\n", strlen(hint), hint);
            total_clipped++;
        }
    }

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
