/* Headless balance simulator.
 *
 * Drives the real encounter state machine rather than a hand-rolled model, so
 * what it measures is what ships. Deterministic: same seed, same battle. */
#include <stdio.h>
#include <string.h>

#include "ft_encounter.h"
#include "ft_rng.h"
#include "ft_world.h"

#define SIM_BATTLES   400
#define SIM_MAX_MS    240000u
#define SIM_TICK_MS   10u

typedef struct {
    uint32_t wins, losses, stalls;
    uint32_t total_turns;
    int32_t  charge_left; /* summed over wins */
} SimResult;

/* Skill is the chance of hitting a window; the rest of the time the press
 * lands somewhere random in the sweep, which is what a real miss looks like. */
static uint32_t press_offset(FtRng* rng, uint32_t window, uint32_t skill_pct, bool tight) {
    if(ft_rng_chance(rng, skill_pct)) {
        /* Aim: land inside the tight band near the ideal moment. */
        const uint32_t spread = tight ? 30u : 60u;
        return (window / 2u) + ft_rng_below(rng, spread) - spread / 2u;
    }
    return ft_rng_below(rng, window);
}

static void play(const FtRoster* roster, uint32_t skill, uint32_t seed, SimResult* out) {
    FtLoadout lo;
    ft_loadout_init(&lo);

    FtEncounter e;
    ft_encounter_init(&e, roster->foes, roster->count, &lo, seed);
    e.coach = false;

    FtRng rng;
    ft_rng_seed(&rng, seed ^ 0xA5A5u);

    uint32_t strike_at = 0, guard_at = 0;
    bool strike_set = false, guard_set = false;
    uint32_t turns = 0;

    for(uint32_t t = 0; t < SIM_MAX_MS && !ft_encounter_over(&e); t += SIM_TICK_MS) {
        switch(e.phase) {
        case FT_PHASE_MENU: {
            /* Pick whatever is usable, preferring the stronger single hit when
             * there is only one foe left. */
            FtAction2 want =
                (ft_encounter_living(&e) > 1u) ? FT_ACTION_BROADCAST : FT_ACTION_CONTACT;
            if(!ft_encounter_action_available(&e, want)) {
                want = (want == FT_ACTION_CONTACT) ? FT_ACTION_BROADCAST : FT_ACTION_CONTACT;
            }
            if(!ft_encounter_action_available(&e, want)) want = FT_ACTION_DEFEND;

            e.menu_index = (uint8_t)want;
            ft_encounter_press_ok(&e);

            strike_set = false;
            guard_set = false;
            turns++;
            break;
        }

        case FT_PHASE_PLAYER_ACT:
            if(!strike_set && !ft_encounter_in_ready(&e)) {
                strike_at = press_offset(&rng, FT_ACTION_WINDOW_MS, skill, true);
                strike_set = true;
            }
            if(strike_set && ft_encounter_sweep_ms(&e) >= strike_at) ft_encounter_press_ok(&e);
            break;

        case FT_PHASE_TELEGRAPH:
            if(!guard_set && !ft_encounter_in_ready(&e)) {
                /* Guarding means pressing near the very end of the sweep. */
                const uint32_t w = FT_TELEGRAPH_MS;
                guard_at = ft_rng_chance(&rng, skill) ?
                               w - ft_rng_below(&rng, FT_CAPTURE_WINDOW_MS + 20u) :
                               ft_rng_below(&rng, w);
                guard_set = true;
            }
            if(guard_set && ft_encounter_sweep_ms(&e) >= guard_at) ft_encounter_press_ok(&e);
            break;

        default:
            break;
        }

        ft_encounter_tick(&e, SIM_TICK_MS);
    }

    out->total_turns += turns;

    if(e.phase == FT_PHASE_WIN) {
        out->wins++;
        if(e.roll.current > 0) out->charge_left += e.roll.current;
    } else if(e.phase == FT_PHASE_LOSE) {
        out->losses++;
    } else {
        out->stalls++;
    }
}

static void report(const char* label, const SimResult* r) {
    const uint32_t n = r->wins + r->losses + r->stalls;
    if(!n) return;

    printf(
        "    %-18s win %3u%%   avg turns %2u   charge left %2u\n", label,
        (r->wins * 100u) / n, r->total_turns / n,
        r->wins ? (uint32_t)r->charge_left / r->wins : 0u);
}

int main(void) {
    printf("\nFlipper Tales — balance (%d battles per cell, level 1 loadout)\n", SIM_BATTLES);

    const uint32_t skills[] = {20, 50, 80};

    for(uint8_t ri = 0; ri < 4; ri++) {
        const FtRoster* roster = ft_roster(ri);

        printf("\n  roster %u: %u foe(s) —", ri, roster->count);
        for(uint8_t i = 0; i < roster->count; i++) {
            printf(" %s", FT_ENEMIES[roster->foes[i]].name);
        }
        printf("\n");

        for(size_t s = 0; s < sizeof(skills) / sizeof(skills[0]); s++) {
            SimResult r;
            memset(&r, 0, sizeof(r));

            for(uint32_t b = 0; b < SIM_BATTLES; b++) {
                play(roster, skills[s], 1000u + ri * 977u + (uint32_t)s * 31u + b, &r);
            }

            char label[24];
            snprintf(label, sizeof(label), "skill %u%%", skills[s]);
            report(label, &r);
        }
    }

    printf("\n");
    return 0;
}
