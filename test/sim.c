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

static uint32_t g_arms = 0, g_bounces = 0, g_bounce_damage = 0;

typedef struct {
    uint32_t wins, losses, stalls;
    uint32_t total_turns;
    int32_t  charge_left; /* summed over wins */
} SimResult;

/* Skill is the chance of hitting a window; the rest of the time the press
 * lands somewhere random in the sweep, which is what a real miss looks like. */
/* How many living foes this action can actually touch. */
static uint8_t reachable(const FtEncounter* e, FtAction2 a) {
    uint8_t n = 0;
    for(uint8_t i = 0; i < e->foe_count; i++) {
        if(ft_encounter_can_reach(e, a, i)) n++;
    }
    return n;
}

static uint32_t press_offset(FtRng* rng, uint32_t window, uint32_t skill_pct, bool tight) {
    if(ft_rng_chance(rng, skill_pct)) {
        /* Aim: land inside the tight band near the ideal moment. */
        const uint32_t spread = tight ? 30u : 60u;
        return (window / 2u) + ft_rng_below(rng, spread) - spread / 2u;
    }
    return ft_rng_below(rng, window);
}

/* Whether this run's player knows about the deflect stance. The baseline
 * never used the old replay either, so leaving it out measures the same
 * player as every previous balance table; turning it on measures the
 * ceiling the new stance adds. */
static bool g_use_deflect = false;

static void play(const FtRoster* roster, uint32_t skill, uint32_t seed, SimResult* out) {
    FtLoadout lo;
    ft_loadout_init(&lo);

    FtEncounter e;
    ft_encounter_init(&e, roster->foes, roster->count, &lo, seed);
    e.coach = false;

    FtRng rng;
    ft_rng_seed(&rng, seed ^ 0xA5A5u);

    uint32_t strike_at = 0, guard_at = 0;
    bool strike_set = false, guard_set = false, counted = false;
    uint32_t turns = 0;

    for(uint32_t t = 0; t < SIM_MAX_MS && !ft_encounter_over(&e); t += SIM_TICK_MS) {
        switch(e.phase) {
        case FT_PHASE_MENU: {
            /* A baseline player reads the screen: they pick something that
             * can actually land, preferring the wide hit while there is a
             * crowd and the strong one once there is not. Choosing on
             * availability alone would model someone swinging at a flyer for
             * the rest of the fight, which is not a balance number — every
             * module is selectable now, so reach is the question. */
            const uint8_t bcast = reachable(&e, FT_ACTION_BROADCAST);
            const uint8_t contact = reachable(&e, FT_ACTION_CONTACT);

            /* A player who has learned the stance arms it whenever it is
             * free to, which is the most generous reading of it: a full bar
             * always becomes a deflect. */
            if(g_use_deflect &&
               ft_encounter_action_block(&e, FT_ACTION_DEFLECT) == NULL) {
                e.menu_index = (uint8_t)FT_ACTION_DEFLECT;
                ft_encounter_press_ok(&e);
                g_arms++;
                strike_set = false;
                guard_set = false;
                turns++;
                break;
            }

            /* The baseline is unchanged from when these numbers were first
             * measured: wide while there is a crowd, strong once there is
             * one foe left. Only the test changed — every module is
             * selectable now, so the question is what it can reach, not
             * whether it is offered. */
            FtAction2 want =
                (ft_encounter_living(&e) > 1u) ? FT_ACTION_BROADCAST : FT_ACTION_CONTACT;

            /* Out of MP means the strong module is off the table, and the
             * sensible answer is to brace and get some back. */
            if(!ft_encounter_action_available(&e, FT_ACTION_CONTACT)) {
                want = (bcast > 0u) ? FT_ACTION_BROADCAST : FT_ACTION_DEFEND;

                e.menu_index = (uint8_t)want;
                ft_encounter_press_ok(&e);
                strike_set = false;
                guard_set = false;
                turns++;
                break;
            }

            const uint8_t first = (want == FT_ACTION_BROADCAST) ? bcast : contact;
            const uint8_t other = (want == FT_ACTION_BROADCAST) ? contact : bcast;

            if(first == 0u) {
                want = (want == FT_ACTION_BROADCAST) ? FT_ACTION_CONTACT :
                                                       FT_ACTION_BROADCAST;
                if(other == 0u) want = FT_ACTION_DEFEND;
            }

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

        case FT_PHASE_IMPACT:
            if(e.last_deflect_fired && !counted) {
                g_bounces++;
                if(e.last_deflect_damage > 0) {
                    const uint32_t dmg = (uint32_t)e.last_deflect_damage;
                    g_bounce_damage = g_bounce_damage + dmg;
                }
                counted = true;
            }
            break;

        case FT_PHASE_TELEGRAPH:
            counted = false;
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

    for(int mode = 0; mode < 2; mode++) {
    g_use_deflect = (mode == 1);
    g_arms = g_bounces = g_bounce_damage = 0;
    printf("\n=== %s ===\n", g_use_deflect ? "with DEFLECT" : "baseline (no deflect)");

    /* Every roster, not just the prologue's. An area fight that nobody has
     * measured is an area fight nobody knows is winnable. */
    for(uint8_t ri = 0; ri < FT_ROSTER_COUNT; ri++) {
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
    if(g_use_deflect) {
        printf("\n  armed %u times, bounced %u (%u%%), %u total damage, %u per arm\n",
               g_arms, g_bounces, g_arms ? (g_bounces * 100u / g_arms) : 0u,
               g_bounce_damage, g_arms ? (g_bounce_damage / g_arms) : 0u);
    }
    }

    printf("\n");
    return 0;
}
