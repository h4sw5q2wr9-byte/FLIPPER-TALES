/* Renders every battle phase to a .pbm so the 128x64 layout can be reviewed
 * as an image, and fails if anything is drawn off-panel. */
#include <gui/gui.h>

#include <stdio.h>
#include <string.h>

#include "ft_encounter.h"
#include "ft_guide.h"
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
    bool        panel;      /* kept so the shot table's shape does not move */
} Shot;


static void build(FtEncounter* e, const Shot* s) {
    FtLoadout lo;
    ft_loadout_init(&lo);

    if(s->extra_foes) {
        const FtEnemyId plain[FT_MAX_ENEMIES] = {
            FT_ENEMY_STRAY_PACKET, FT_ENEMY_DRIFT_BEACON, FT_ENEMY_SEALED_LOCK};
        const FtEnemyId walled[FT_MAX_ENEMIES] = {
            FT_ENEMY_BLANK_WALL, FT_ENEMY_DRIFT_BEACON, FT_ENEMY_STRAY_PACKET};

        ft_encounter_init(
            e, (s->enemy == FT_ENEMY_BLANK_WALL) ? walled : plain,
            s->extra_foes, &lo, 42);
    } else {
        ft_encounter_init_single(e, s->enemy, &lo, 42);
    }

    e->phase = s->phase;
    e->phase_ms = s->phase_ms;
    e->menu_index = s->menu_index;

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

        /* `menu_index` is dead weight in these two phases, so it doubles as
         * the guard knob: 5 is a press inside the window, 6 one that went up
         * far too early, 7 no press at all. */
        if(s->menu_index >= 5u) {
            const bool early = (s->menu_index == 6u);
            e->guard_pressed = (s->menu_index != 7u);
            e->guard_press_ms = early ? 300u : (FT_TELEGRAPH_MS - 40u);
            e->guard_locked_ms = 90u;
            e->last_guard_offset =
                e->guard_pressed ? (int32_t)(FT_TELEGRAPH_MS - e->guard_press_ms) : -1;
        }
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
        e->last_enemy_hit.perfect = (s->variant == 0);
        e->last_enemy_hit.damage = (s->variant == 0) ? 0 : 3;

        /* menu_index 8 and 9: the bounce landed, whole or halved. */
        if(s->menu_index >= 8u) {
            e->deflect_armed = true;
            e->last_deflect_fired = true;
            e->last_deflect_damage = (s->menu_index == 8u) ? 6 : 3;
            e->last_guard = (s->menu_index == 8u) ? FT_GUARD_CAPTURE : FT_GUARD_JAM;
            e->guard_pressed = true;
            e->guard_press_ms = FT_TELEGRAPH_MS - 30u;
            e->last_guard_offset = 30;
            e->last_enemy_hit.perfect = (s->menu_index == 8u);
            e->last_enemy_hit.damage = (s->menu_index == 8u) ? 0 : 3;
        }

        /* A guard that lapsed, or was never raised, lands as a plain hit. */
        if(s->menu_index == 6u || s->menu_index == 7u) {
            e->last_guard = FT_GUARD_NONE;
            e->last_enemy_hit.perfect = false;
            e->last_enemy_hit.damage = 6;
        }
    }

    if(s->variant == 9) e->coach = false;

    /* Variant 12 fills the pockets, so the Use row has something to name. */
    if(s->variant == 12 || s->variant == 13) {
        e->coach = false;
        ft_pockets_add(&e->pockets, FT_ITEM_APPLE);
        ft_pockets_add(&e->pockets, FT_ITEM_APPLE);
        ft_pockets_add(&e->pockets, FT_ITEM_CELL);
        e->menu_index = (uint8_t)FT_ACTION_ITEM;
        e->item_index = (s->variant == 13) ? 1u : 0u;

        /* Hurt, so the row does not read "Nothing to mend". */
        e->roll.current = 6;
        e->roll.target = 6;
        e->stats.ram = 1;
    }

    /* Variant 10 arms the deflect; 11 arms it with a payload already on, so
     * the two badges are seen stacked. */
    if(s->variant == 10 || s->variant == 11) {
        e->coach = false;
        e->deflect_armed = true;
        if(s->variant == 11) e->status[FT_PAYLOAD_STALL] = FT_STATUS_TURNS;
    }

    /* Variant 8 shows what a payload looks like on the player. */
    if(s->variant == 8) {
        e->coach = false;
        e->status[FT_PAYLOAD_CORRUPT] = FT_STATUS_TURNS;
    }

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
        /* The guard aftermath: the marker frozen where the block went up,
         * with the impact edge still closing on it, and the readings the
         * popup gives afterwards. */
        {"guard-held",      FT_ENEMY_DRIFT_BEACON, FT_PHASE_TELEGRAPH,  1280, 5, 0, 0, false},
        {"guard-tooearly",  FT_ENEMY_DRIFT_BEACON, FT_PHASE_TELEGRAPH,  1000, 6, 0, 0, false},
        {"after-jam",       FT_ENEMY_DRIFT_BEACON, FT_PHASE_IMPACT,    1100,  5, 1, 0, false},
        {"after-early",     FT_ENEMY_DRIFT_BEACON, FT_PHASE_IMPACT,    1100,  6, 1, 0, false},
        {"after-noguard",   FT_ENEMY_DRIFT_BEACON, FT_PHASE_IMPACT,    1100,  7, 1, 0, false},
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
        {"foe-wall",        FT_ENEMY_BLANK_WALL,   FT_PHASE_MENU,      0,    0, 0, 3, false},
        {"foe-booter",      FT_ENEMY_COLD_BOOTER,  FT_PHASE_MENU,      0,    0, 0, 0, false},
        {"status-dot",      FT_ENEMY_MAST_RELAY,   FT_PHASE_MENU,      0,    0, 8, 0, false},
        /* The deflect: the stance on its own, the stance under a payload,
         * and the two readings the bounce can give. */
        {"use-apple",       FT_ENEMY_STRAY_PACKET, FT_PHASE_MENU,      0,    0, 12, 3, false},
        {"use-cell",        FT_ENEMY_STRAY_PACKET, FT_PHASE_MENU,      0,    0, 13, 0, false},
        {"use-empty",       FT_ENEMY_STRAY_PACKET, FT_PHASE_MENU,      0,    5, 9, 0, false},
        {"deflect-armed",   FT_ENEMY_STRAY_PACKET, FT_PHASE_MENU,      0,    0, 10, 3, false},
        {"deflect-stacked", FT_ENEMY_MAST_RELAY,   FT_PHASE_MENU,      0,    0, 11, 0, false},
        {"deflect-full",    FT_ENEMY_DRIFT_BEACON, FT_PHASE_IMPACT,   1100,  8, 0, 3, false},
        {"deflect-half",    FT_ENEMY_DRIFT_BEACON, FT_PHASE_IMPACT,   1100,  9, 0, 3, false},
        {"win",             FT_ENEMY_STRAY_PACKET, FT_PHASE_WIN,        100,  0, 0, 0, false},
        {"lose",            FT_ENEMY_SEALED_LOCK,  FT_PHASE_LOSE,       100,  0, 0, 0, false},
    };

    Canvas* canvas = ft_stub_canvas_alloc();
    int total_clipped = 0;

    /* The menus are screens too, and they were the two nobody was looking at
     * until a row was added to one of them. */
    for(uint8_t i = 0; i < FT_PAUSE_COUNT; i++) {
        ft_render_pause(canvas, i, (i % 2) != 0, (i == 0u) ? 0 : 12, (i % 3u) == 0u);

        char pp[64];
        snprintf(pp, sizeof(pp), "preview/80_pause%u.pbm", i);
        ft_stub_canvas_write_pbm(canvas, pp);

        const int c = ft_stub_canvas_clipped(canvas);
        total_clipped += c;
        printf("  pause row %u         %s\n", i, c ? "CLIPPED" : "ok");
    }

    {
        /* The field guide: empty, listed, and a page per enemy. */
        FtGuide g;
        ft_guide_init(&g);

        ft_render_guide_list(canvas, &g, 0);
        ft_stub_canvas_write_pbm(canvas, "preview/70_guide-empty.pbm");
        total_clipped += ft_stub_canvas_clipped(canvas);
        printf("  guide empty         %s\n",
               ft_stub_canvas_clipped(canvas) ? "CLIPPED" : "ok");

        FtLoadout glo;
        ft_loadout_init(&glo);
        for(uint8_t i = 0; i < FT_ENEMY_COUNT; i++) {
            FtEncounter one;
            ft_encounter_init_single(&one, (FtEnemyId)i, &glo, 1);
            ft_guide_note_encounter(&g, &one);
        }

        for(uint8_t i = 0; i < FT_ENEMY_COUNT; i++) {
            ft_render_guide_list(canvas, &g, i);
            total_clipped += ft_stub_canvas_clipped(canvas);

            ft_render_guide_entry(canvas, (FtEnemyId)i);

            char gp[64];
            snprintf(gp, sizeof(gp), "preview/71_guide%u.pbm", i);
            ft_stub_canvas_write_pbm(canvas, gp);

            const int c = ft_stub_canvas_clipped(canvas);
            total_clipped += c;
            printf("  guide %-13s %s\n", FT_ENEMIES[i].name,
                   c ? "CLIPPED" : "ok");
        }
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
        /* The orb screen: each row, and the widest the value column gets. */
        FtStats st;
        ft_stats_init(&st);
        st.level = 7;

        /* Built the way the game builds it, so the value column and the count
         * beside it agree. Setting `spent` by hand showed "14 (4)". */
        for(uint8_t l = 0; l < 5u; l++) {
            ft_level_take(&st);
            if(l < 4u) ft_orb_spend(&st, FT_UP_CHARGE);
        }

        for(uint8_t i = 0; i < FT_UP_COUNT; i++) {
            ft_render_orbs(canvas, &st, i);

            char lp[64];
            snprintf(lp, sizeof(lp), "preview/87_orbs%u.pbm", i);
            ft_stub_canvas_write_pbm(canvas, lp);

            const int c = ft_stub_canvas_clipped(canvas);
            total_clipped += c;
            printf("  orb row %u           %s\n", i, c ? "CLIPPED" : "ok");
        }

        /* Every stat at its cap, at three digits, with a two-digit orb count
         * beside each one: the widest this screen can ever be. */
        st.charge_max = FT_CAP_CHARGE;
        st.ram_max = FT_CAP_RAM;
        st.flash_max = FT_CAP_FLASH;
        st.level = 99;
        st.orbs = 99;
        for(uint8_t i = 0; i < FT_UP_COUNT; i++) st.spent[i] = 99u;
        st.charge = st.charge_max;

        ft_render_orbs(canvas, &st, 0);
        ft_stub_canvas_write_pbm(canvas, "preview/88_orbcap.pbm");
        total_clipped += ft_stub_canvas_clipped(canvas);
        printf("  orbs capped         %s\n",
               ft_stub_canvas_clipped(canvas) ? "CLIPPED" : "ok");
    }

    {
        /* Pockets: empty, one row, and full to the cap. */
        FtPockets pk;
        FtStats   st;
        ft_stats_init(&st);
        st.charge = 4;

        ft_pockets_init(&pk);
        ft_render_pockets(canvas, &pk, 0, &st);
        ft_stub_canvas_write_pbm(canvas, "preview/91_pockets-empty.pbm");
        total_clipped += ft_stub_canvas_clipped(canvas);
        printf("  pockets empty       %s\n",
               ft_stub_canvas_clipped(canvas) ? "CLIPPED" : "ok");

        for(uint8_t i = 0; i < FT_POCKET_MAX; i++) {
            ft_pockets_add(&pk, (FtItemId)(i % FT_ITEM_COUNT));
        }
        for(uint8_t i = 0; i < ft_pockets_kinds(&pk); i++) {
            ft_render_pockets(canvas, &pk, i, &st);

            char pp[64];
            snprintf(pp, sizeof(pp), "preview/92_pockets%u.pbm", i);
            ft_stub_canvas_write_pbm(canvas, pp);

            const int c = ft_stub_canvas_clipped(canvas);
            total_clipped += c;
            printf("  pockets row %u       %s\n", i, c ? "CLIPPED" : "ok");
        }

        /* Full health: nothing on you is worth eating. */
        st.charge = st.charge_max;
        st.ram = st.ram_max;
        ft_render_pockets(canvas, &pk, 0, &st);
        ft_stub_canvas_write_pbm(canvas, "preview/93_pockets-full.pbm");
        total_clipped += ft_stub_canvas_clipped(canvas);
        printf("  pockets unhurt      %s\n",
               ft_stub_canvas_clipped(canvas) ? "CLIPPED" : "ok");
    }

    {
        /* Quests, and everything anybody says. Both are new screens, and the
         * talk box is the only one whose content comes out of core as text. */
        FtQuests q;
        ft_quests_init(&q);

        for(uint8_t st = 0; st <= (uint8_t)FT_QUEST_DONE; st++) {
            q.state[0] = st;
            ft_render_quests(canvas, &q, 0);

            char qp[64];
            snprintf(qp, sizeof(qp), "preview/89_quests%u.pbm", st);
            ft_stub_canvas_write_pbm(canvas, qp);

            const int c = ft_stub_canvas_clipped(canvas);
            total_clipped += c;
            printf("  quests state %u      %s\n", st, c ? "CLIPPED" : "ok");
        }

        for(uint8_t st = 0; st <= (uint8_t)FT_QUEST_DONE; st++) {
            FtQuests talker;
            ft_quests_init(&talker);
            talker.state[0] = st;

            const FtQuestTalk t = ft_quest_talk(&talker, FT_QUEST_CLEAN_RUN);
            ft_render_talk(canvas, ft_quest_def(FT_QUEST_CLEAN_RUN)->name, &t);

            char tp[64];
            snprintf(tp, sizeof(tp), "preview/90_talk%u.pbm", st);
            ft_stub_canvas_write_pbm(canvas, tp);

            const int c = ft_stub_canvas_clipped(canvas);
            total_clipped += c;
            printf("  talk state %u        %s\n", st, c ? "CLIPPED" : "ok");
        }
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
