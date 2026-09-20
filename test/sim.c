/* Headless balance simulator.
 *
 * Plays whole battles with a scripted policy so the damage numbers can be
 * tuned without a device. Deterministic: same seed, same battle.
 *
 * Known limitations: every battle starts from fresh level-1 stats, so later
 * enemies read harder here than they will in play, where the player arrives
 * levelled. Hard Mode's +50% XP upside is not modelled at all, only its costs. */
#include <stdio.h>

#include "ft_combat.h"
#include "ft_data.h"
#include "ft_priority.h"
#include "ft_progress.h"
#include "ft_rng.h"
#include "ft_roll.h"
#include "ft_signal.h"

#define SIM_BATTLES  2000
#define SIM_MAX_TURNS 40

typedef struct {
    uint32_t wins;
    uint32_t losses;
    uint32_t stalls;
    uint32_t total_turns;
    uint32_t captures;
    uint32_t brownouts_survived;
} SimResult;

/* Pick the module that is not locked out by this enemy's attributes. */
static const FtAttack* choose_attack(uint32_t enemy_attrs) {
    if(enemy_attrs & FT_ATTR_AIRBORNE) return &FT_MODULES[FT_MOD_SUBGHZ].attack;
    if(enemy_attrs & FT_ATTR_ENCRYPTED) return &FT_MODULES[FT_MOD_NFC].attack;

    /* Otherwise the contact burst is simply stronger. */
    return &FT_MODULES[FT_MOD_NFC].attack;
}

/* Model player execution as a skill percentage, split between a clean capture
 * and an ordinary jam. */
static FtGuard roll_guard(FtRng* rng, uint32_t skill_pct) {
    if(ft_rng_chance(rng, skill_pct / 3u)) return FT_GUARD_CAPTURE;
    if(ft_rng_chance(rng, skill_pct)) return FT_GUARD_JAM;
    return FT_GUARD_NONE;
}

static FtRating roll_rating(FtRng* rng, uint32_t skill_pct) {
    if(ft_rng_chance(rng, skill_pct)) {
        return ft_rng_chance(rng, 40) ? FT_RATING_EXCELLENT : FT_RATING_GREAT;
    }
    return ft_rng_chance(rng, 50) ? FT_RATING_GOOD : FT_RATING_MISS;
}

static void simulate(FtEnemyId enemy_id, const FtLoadout* lo, uint32_t skill_pct,
                     uint32_t seed, SimResult* out) {
    const FtEnemy* proto = &FT_ENEMIES[enemy_id];
    const FtLoadoutEffects fx = ft_loadout_effects(lo);

    FtRng rng;
    ft_rng_seed(&rng, seed);

    for(uint32_t battle = 0; battle < SIM_BATTLES; battle++) {
        FtStats stats;
        ft_stats_init(&stats);
        stats.charge_max = (int16_t)(stats.charge_max + fx.charge_max_bonus);
        stats.charge = stats.charge_max;

        FtRoll roll;
        ft_roll_init(&roll, stats.charge);

        FtSignal sig;
        ft_signal_init(&sig, 1);
        ft_signal_battle_start(&sig);

        FtSignalLibrary lib;
        ft_siglib_init(&lib);

        int16_t enemy_charge = proto->charge;
        bool counted_brownout = false;
        uint32_t turn = 0;

        for(; turn < SIM_MAX_TURNS; turn++) {
            /* --- player acts --- */
            const FtAttack* atk = choose_attack(proto->attrs);
            FtDefender edef = {proto->shielded, proto->attrs};
            FtHitParams pp = {fx.atk_up, 0, roll_rating(&rng, skill_pct), false,
                              FT_GUARD_NONE, 0};

            FtHitResult pr = ft_resolve_hit(atk, &edef, &pp);
            enemy_charge = (int16_t)(enemy_charge - pr.damage);

            if(pr.outcome == FT_HIT_OK && pr.damage > 0) {
                ft_signal_add(&sig, ft_signal_attack_gain(roll.current, stats.charge_max));
            }
            if(enemy_charge <= 0) break;

            /* --- enemy acts --- */
            const FtAttack* eatk =
                &proto->attacks[ft_rng_below(&rng, proto->attack_count)];

            FtDefender pdef = {0, 0};
            /* Hard Mode halves the guard window, which costs execution. */
            const uint32_t guard_skill = fx.hard_mode ? skill_pct / 2u : skill_pct;
            FtHitParams ep = {0, 0, FT_RATING_MISS, false, roll_guard(&rng, guard_skill),
                              fx.jam_reduction_pct};

            FtHitResult er = ft_resolve_hit(eatk, &pdef, &ep);
            if(er.captured && ft_siglib_capture(&lib, eatk->id)) out->captures++;

            ft_roll_apply_damage(&roll, (int16_t)(er.damage * (fx.hard_mode ? 2 : 1)));
            ft_signal_add(&sig, FT_SIGNAL_GAIN_ENEMY_TURN);

            /* Drain the queued damage. The thinking phase pauses the roll, so
             * one turn's worth of ticking is what actually lands. */
            const uint32_t interval = ft_roll_interval_ms(0, false, fx.hard_mode);
            ft_roll_tick(&roll, interval * 64u, interval);

            if(ft_roll_brownout(&roll) && !counted_brownout) {
                counted_brownout = true;
                out->brownouts_survived++;
            }
            if(ft_roll_down(&roll)) break;
        }

        out->total_turns += turn + 1;

        if(enemy_charge <= 0) {
            out->wins++;
        } else if(ft_roll_down(&roll)) {
            out->losses++;
        } else {
            out->stalls++;
        }
    }
}

static void report(const char* label, const SimResult* r) {
    const uint32_t total = r->wins + r->losses + r->stalls;
    if(total == 0u) return;

    printf("  %-22s win %3u%%  loss %3u%%  stall %3u%%  avg turns %2u.%u  captures %u\n",
           label,
           (r->wins * 100u) / total,
           (r->losses * 100u) / total,
           (r->stalls * 100u) / total,
           r->total_turns / total,
           ((r->total_turns * 10u) / total) % 10u,
           r->captures);
}

int main(void) {
    printf("\nFlipper Tales — balance simulation (%d battles per cell)\n", SIM_BATTLES);

    FtLoadout base;
    ft_loadout_init(&base);

    FtLoadout amped;
    ft_loadout_init(&amped);
    ft_loadout_add(&amped, FT_MOD_AMPLIFY);
    ft_loadout_add(&amped, FT_MOD_CHARGE_PLUS);

    FtLoadout hard;
    ft_loadout_init(&hard);
    ft_loadout_add(&hard, FT_MOD_HARD_MODE);

    const struct {
        const char* name;
        const FtLoadout* lo;
    } builds[] = {
        {"base", &base},
        {"amplify+charge", &amped},
        {"hard mode", &hard},
    };

    const uint32_t skills[] = {0, 35, 70, 95};

    /* A build is only meaningful if the player could afford its Flash. */
    printf("\nbuilds:\n");
    for(size_t b = 0; b < sizeof(builds) / sizeof(builds[0]); b++) {
        const FtLoadoutEffects fx = ft_loadout_effects(builds[b].lo);
        int16_t over = (int16_t)(fx.flash_used - FT_START_FLASH);
        if(over < 0) over = 0;
        const int min_level = 1 + (over + FT_LEVEL_UP_FLASH - 1) / FT_LEVEL_UP_FLASH;
        printf("  %-16s flash %2d  affordable from level %d\n", builds[b].name,
               fx.flash_used, min_level);
    }

    for(int e = 0; e < FT_ENEMY_COUNT; e++) {
        printf("\n%s (charge %d, shield %d)\n", FT_ENEMIES[e].name, FT_ENEMIES[e].charge,
               FT_ENEMIES[e].shielded);

        for(size_t b = 0; b < sizeof(builds) / sizeof(builds[0]); b++) {
            for(size_t s = 0; s < sizeof(skills) / sizeof(skills[0]); s++) {
                SimResult r = {0, 0, 0, 0, 0, 0};
                const uint32_t seed =
                    1000u + (uint32_t)e * 100u + (uint32_t)b * 10u + (uint32_t)s;
                simulate((FtEnemyId)e, builds[b].lo, skills[s], seed, &r);

                char label[40];
                snprintf(label, sizeof(label), "%s @ skill %u%%", builds[b].name, skills[s]);
                report(label, &r);
            }
        }
    }

    printf("\n");
    return 0;
}
