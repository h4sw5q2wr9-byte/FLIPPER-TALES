/* Host-side unit tests for src/core. Every assertion below corresponds to a
 * claim made in docs/DESIGN.md. */
#include <stdio.h>
#include <string.h>

#include "ft_combat.h"
#include "ft_data.h"
#include "ft_encounter.h"
#include "ft_practice.h"
#include "ft_save.h"
#include "ft_priority.h"
#include "ft_progress.h"
#include "ft_map.h"
#include "ft_rng.h"
#include "ft_tutorial.h"
#include "ft_world.h"
#include "ft_roll.h"
#include "ft_signal.h"

static int checks = 0;
static int failures = 0;

#define CHECK(cond, ...)                                 \
    do {                                                 \
        checks++;                                        \
        if(!(cond)) {                                    \
            failures++;                                  \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);\
            printf(__VA_ARGS__);                         \
            printf("\n");                                \
        }                                                \
    } while(0)

#define CHECK_EQ(actual, expected)                                        \
    do {                                                                  \
        long a_ = (long)(actual), e_ = (long)(expected);                  \
        checks++;                                                         \
        if(a_ != e_) {                                                    \
            failures++;                                                   \
            printf("  FAIL %s:%d: %s == %ld, expected %ld\n", __FILE__,   \
                   __LINE__, #actual, a_, e_);                            \
        }                                                                 \
    } while(0)

static void section(const char* name) {
    printf("%s\n", name);
}

/* Convenience: a plain contact attack with no shield-halving. */
static FtAttack mk_attack(int16_t power, FtDelivery delivery, FtAttackClass klass) {
    FtAttack a = {1, power, 0, false, delivery, klass, FT_PAYLOAD_NONE};
    return a;
}

static FtHitParams mk_params(FtGuard guard) {
    FtHitParams p = {0, 0, FT_RATING_MISS, false, guard, 0};
    return p;
}

/* ------------------------------------------------------------------ */

static void test_ratings(void) {
    section("ratings (DESIGN 4.3)");

    CHECK_EQ(ft_rating_pct(FT_RATING_MISS, false), 100);
    CHECK_EQ(ft_rating_pct(FT_RATING_NICE, false), 110);
    CHECK_EQ(ft_rating_pct(FT_RATING_GOOD, false), 125);
    CHECK_EQ(ft_rating_pct(FT_RATING_GREAT, false), 150);
    CHECK_EQ(ft_rating_pct(FT_RATING_AMAZING, false), 175);
    CHECK_EQ(ft_rating_pct(FT_RATING_EXCELLENT, false), 200);

    /* Ante Up turns a missed action command into no damage at all. */
    CHECK_EQ(ft_rating_pct(FT_RATING_MISS, true), 0);
    CHECK_EQ(ft_rating_pct(FT_RATING_GREAT, true), 150);
}

static void test_guard_clamp(void) {
    section("guard permissions (DESIGN 4.5)");

    CHECK_EQ(ft_guard_permitted(FT_CLASS_NORMAL, FT_GUARD_CAPTURE), FT_GUARD_CAPTURE);
    CHECK_EQ(ft_guard_permitted(FT_CLASS_NORMAL, FT_GUARD_JAM), FT_GUARD_JAM);

    /* GUARDED can be jammed but never captured. */
    CHECK_EQ(ft_guard_permitted(FT_CLASS_GUARDED, FT_GUARD_CAPTURE), FT_GUARD_JAM);
    CHECK_EQ(ft_guard_permitted(FT_CLASS_GUARDED, FT_GUARD_JAM), FT_GUARD_JAM);

    /* UNDODGEABLE refuses everything. */
    CHECK_EQ(ft_guard_permitted(FT_CLASS_UNDODGEABLE, FT_GUARD_CAPTURE), FT_GUARD_NONE);
    CHECK_EQ(ft_guard_permitted(FT_CLASS_UNDODGEABLE, FT_GUARD_JAM), FT_GUARD_NONE);
}

static void test_damage_basics(void) {
    section("damage resolution (DESIGN 4.2)");

    FtAttack atk = mk_attack(10, FT_DELIVERY_BROADCAST, FT_CLASS_NORMAL);
    FtDefender def = {0, 0};
    FtHitParams p = mk_params(FT_GUARD_NONE);

    FtHitResult r = ft_resolve_hit(&atk, &def, &p);
    CHECK_EQ(r.outcome, FT_HIT_OK);
    CHECK_EQ(r.damage, 10);

    /* Shield subtracts per hit. */
    def.shielded = 3;
    r = ft_resolve_hit(&atk, &def, &p);
    CHECK_EQ(r.damage, 7);

    /* Shield meeting or exceeding the power deflects entirely. */
    def.shielded = 10;
    r = ft_resolve_hit(&atk, &def, &p);
    CHECK_EQ(r.outcome, FT_HIT_DEFLECTED);
    CHECK_EQ(r.damage, 0);

    /* Rating multiplies before defence. */
    def.shielded = 0;
    p.rating = FT_RATING_GREAT;
    r = ft_resolve_hit(&atk, &def, &p);
    CHECK_EQ(r.damage, 15);

    /* ATK down cannot push raw power below the floor of 1. */
    p.rating = FT_RATING_MISS;
    p.atk_down = 999;
    r = ft_resolve_hit(&atk, &def, &p);
    CHECK_EQ(r.damage, FT_MIN_RAW_DAMAGE);

    /* A missed action command under Ante Up is a miss, not a deflection. */
    p.atk_down = 0;
    p.ante_up = true;
    r = ft_resolve_hit(&atk, &def, &p);
    CHECK_EQ(r.outcome, FT_HIT_MISSED);
    CHECK_EQ(r.damage, 0);
}

static void test_pierce(void) {
    section("piercing");

    FtDefender def = {5, 0};

    FtAttack plain = mk_attack(10, FT_DELIVERY_CONTACT, FT_CLASS_NORMAL);
    CHECK_EQ(ft_effective_pierce(&plain, &def), 0);

    /* NFC halves the shield, rounded up: 5 -> 3. */
    FtAttack nfc = plain;
    nfc.pierce_half = true;
    CHECK_EQ(ft_effective_pierce(&nfc, &def), 3);

    FtHitParams p = mk_params(FT_GUARD_NONE);
    FtHitResult r = ft_resolve_hit(&nfc, &def, &p);
    CHECK_EQ(r.damage, 8); /* 10 - (5 - 3) */

    /* Flat pierce stacks with the halving. */
    nfc.pierce = 1;
    CHECK_EQ(ft_effective_pierce(&nfc, &def), 4);

    /* Pierce beyond the shield does not become bonus damage. */
    nfc.pierce = 99;
    r = ft_resolve_hit(&nfc, &def, &p);
    CHECK_EQ(r.damage, 10);
}

static void test_attribute_locks(void) {
    section("attribute locks (DESIGN 4.8)");

    FtHitParams p = mk_params(FT_GUARD_NONE);

    FtAttack contact = mk_attack(10, FT_DELIVERY_CONTACT, FT_CLASS_NORMAL);
    FtAttack broadcast = mk_attack(10, FT_DELIVERY_BROADCAST, FT_CLASS_NORMAL);
    FtAttack directed = mk_attack(10, FT_DELIVERY_DIRECTED, FT_CLASS_NORMAL);

    FtDefender airborne = {0, FT_ATTR_AIRBORNE};
    FtHitResult r = ft_resolve_hit(&contact, &airborne, &p);
    CHECK_EQ(r.outcome, FT_HIT_LOCKED);
    CHECK_EQ(r.damage, 0);
    CHECK_EQ(r.ram_refund, 0); /* out of reach: the turn is simply spent */

    r = ft_resolve_hit(&broadcast, &airborne, &p);
    CHECK_EQ(r.outcome, FT_HIT_OK);
    CHECK_EQ(r.damage, 10);

    FtDefender encrypted = {0, FT_ATTR_ENCRYPTED};
    r = ft_resolve_hit(&broadcast, &encrypted, &p);
    CHECK_EQ(r.outcome, FT_HIT_LOCKED);
    CHECK_EQ(r.ram_refund, 1); /* refunded, so the turn degrades into a Pass */

    r = ft_resolve_hit(&contact, &encrypted, &p);
    CHECK_EQ(r.outcome, FT_HIT_OK);

    /* Directed delivery ignores both locks. */
    CHECK_EQ(ft_resolve_hit(&directed, &airborne, &p).outcome, FT_HIT_OK);
    CHECK_EQ(ft_resolve_hit(&directed, &encrypted, &p).outcome, FT_HIT_OK);
}

static void test_jam_and_capture(void) {
    section("jam and capture (DESIGN 4.5)");

    FtDefender def = {0, 0};

    FtAttack atk = mk_attack(11, FT_DELIVERY_BROADCAST, FT_CLASS_NORMAL);
    atk.payload = FT_PAYLOAD_CORRUPT;

    /* Unguarded: full damage and the payload lands. */
    FtHitParams p = mk_params(FT_GUARD_NONE);
    FtHitResult r = ft_resolve_hit(&atk, &def, &p);
    CHECK_EQ(r.damage, 11);
    CHECK(r.payload_applied, "payload should land when unguarded");

    /* Jam: halved, rounded down, payload nullified. */
    p.guard = FT_GUARD_JAM;
    r = ft_resolve_hit(&atk, &def, &p);
    CHECK_EQ(r.damage, 5);
    CHECK(!r.payload_applied, "jamming must nullify the payload");
    CHECK(!r.captured, "jamming must not capture");

    /* Faraday improves the reduction. */
    p.jam_reduction_pct = 70;
    r = ft_resolve_hit(&atk, &def, &p);
    CHECK_EQ(r.damage, 3); /* 11 * 30 / 100 */

    /* Capture: zero damage, signal captured. Broadcast is dodged, not countered. */
    p.jam_reduction_pct = 0;
    p.guard = FT_GUARD_CAPTURE;
    r = ft_resolve_hit(&atk, &def, &p);
    CHECK_EQ(r.damage, 0);
    CHECK(r.captured, "capture should yield a signal");
    CHECK(!r.countered, "broadcast attacks are dodged, not countered");
    CHECK(!r.payload_applied, "capture must nullify the payload");

    /* Contact attacks are countered on capture. */
    FtAttack melee = mk_attack(11, FT_DELIVERY_CONTACT, FT_CLASS_NORMAL);
    r = ft_resolve_hit(&melee, &def, &p);
    CHECK(r.countered, "contact attacks should counter on capture");

    /* GUARDED: attempting capture degrades to a jam, and nothing is captured. */
    FtAttack guarded = mk_attack(11, FT_DELIVERY_BROADCAST, FT_CLASS_GUARDED);
    r = ft_resolve_hit(&guarded, &def, &p);
    CHECK_EQ(r.damage, 5);
    CHECK(!r.captured, "GUARDED attacks keep their secrets");

    /* UNDODGEABLE: the guard is ignored outright. */
    FtAttack undodgeable = mk_attack(11, FT_DELIVERY_BROADCAST, FT_CLASS_UNDODGEABLE);
    undodgeable.payload = FT_PAYLOAD_CORRUPT;
    r = ft_resolve_hit(&undodgeable, &def, &p);
    CHECK_EQ(r.damage, 11);
    CHECK(!r.captured, "UNDODGEABLE attacks cannot be captured");
    CHECK(r.payload_applied, "UNDODGEABLE payloads always land");
}

static void test_roll(void) {
    section("rolling Charge (DESIGN 4.6)");

    CHECK_EQ(ft_roll_interval_ms(0, false, false), FT_ROLL_BASE_INTERVAL_MS);
    CHECK_EQ(ft_roll_interval_ms(2, false, false), 72);  /* +10% per shield point */
    CHECK_EQ(ft_roll_interval_ms(0, true, false), 240);  /* defending: -75% speed */
    CHECK_EQ(ft_roll_interval_ms(0, false, true), 30);   /* Hard Mode: 2x speed */
    CHECK(ft_roll_interval_ms(0, false, true) > 0, "interval must never be zero");

    FtRoll roll;
    ft_roll_init(&roll, 25);
    CHECK(!ft_roll_active(&roll), "a fresh roll is settled");

    ft_roll_apply_damage(&roll, 10);
    CHECK_EQ(roll.target, 15);
    CHECK_EQ(roll.current, 25); /* nothing has drained yet */
    CHECK(ft_roll_active(&roll), "roll should be running after damage");

    /* Five intervals drain five points. */
    ft_roll_tick(&roll, 5 * FT_ROLL_BASE_INTERVAL_MS, FT_ROLL_BASE_INTERVAL_MS);
    CHECK_EQ(roll.current, 20);

    /* Overshooting stops at the target, it does not go past it. */
    ft_roll_tick(&roll, 999 * FT_ROLL_BASE_INTERVAL_MS, FT_ROLL_BASE_INTERVAL_MS);
    CHECK_EQ(roll.current, 15);
    CHECK(!ft_roll_active(&roll), "roll should settle at the target");

    /* Brownout: the window in which the player can still act. */
    ft_roll_apply_damage(&roll, 15);
    CHECK_EQ(roll.target, 0);
    CHECK(ft_roll_brownout(&roll), "target 0 with Charge left is a brownout");
    CHECK(!ft_roll_down(&roll), "not down until Charge actually reaches 0");

    /* Healing out of a brownout. */
    ft_roll_heal(&roll, 8, 25);
    CHECK(!ft_roll_brownout(&roll), "healing should cancel the brownout");
    CHECK(roll.target > 0, "heal must raise the target");

    /* A hit at or above the threshold skips the roll entirely. */
    FtRoll big;
    ft_roll_init(&big, 125);
    ft_roll_apply_damage(&big, FT_INSTANT_DAMAGE_THRESHOLD);
    CHECK_EQ(big.current, 0);
    CHECK(ft_roll_down(&big), "an instant hit downs immediately");

    /* Lethal damage below the threshold still rolls, leaving the window. */
    FtRoll nearly;
    ft_roll_init(&nearly, 100);
    ft_roll_apply_damage(&nearly, 100);
    CHECK_EQ(nearly.target, 0);
    CHECK_EQ(nearly.current, 100); /* nothing drained yet */
    CHECK(ft_roll_brownout(&nearly), "sub-threshold lethal damage leaves a window");
    CHECK(!ft_roll_down(&nearly), "the player can still act during the window");
}

static void test_signal_meter(void) {
    section("Signal meter (DESIGN 4.7)");

    FtSignal sig;
    ft_signal_init(&sig, 2);
    CHECK_EQ(ft_signal_capacity(&sig), 200);

    ft_signal_battle_start(&sig);
    CHECK_EQ(sig.value, FT_SIGNAL_BATTLE_START);

    /* Gain scales with how close to death the player is. */
    CHECK_EQ(ft_signal_attack_gain(100, 100), FT_SIGNAL_GAIN_ATTACK);
    CHECK_EQ(ft_signal_attack_gain(26, 100), FT_SIGNAL_GAIN_ATTACK);
    CHECK_EQ(ft_signal_attack_gain(25, 100), FT_SIGNAL_GAIN_LOW_CHARGE); /* exactly 25% */
    CHECK_EQ(ft_signal_attack_gain(10, 100), FT_SIGNAL_GAIN_LOW_CHARGE);
    CHECK_EQ(ft_signal_attack_gain(1, 100), FT_SIGNAL_GAIN_LAST_CHARGE);

    CHECK_EQ(ft_signal_focus_gain(0), FT_SIGNAL_GAIN_FOCUS);
    CHECK_EQ(ft_signal_focus_gain(3), FT_SIGNAL_GAIN_FOCUS + 15);

    /* Capacity clamps. */
    ft_signal_add(&sig, 9999);
    CHECK_EQ(sig.value, 200);
    CHECK_EQ(ft_signal_bars(&sig), 2);

    CHECK(ft_signal_spend_bars(&sig, 2), "should afford two bars");
    CHECK_EQ(sig.value, 0);
    CHECK(!ft_signal_spend_bars(&sig, 1), "should not afford a bar when empty");

    /* A JAMMER locks the meter: no gain, no spend. */
    ft_signal_add(&sig, 100);
    sig.locked = true;
    ft_signal_add(&sig, 100);
    CHECK_EQ(sig.value, 100);
    CHECK(!ft_signal_spend_bars(&sig, 1), "a locked meter cannot be spent");
}

static void test_signal_library(void) {
    section("Signal Library (DESIGN 4.5)");

    FtSignalLibrary lib;
    ft_siglib_init(&lib);
    CHECK_EQ(lib.count, 0);

    CHECK(ft_siglib_capture(&lib, 10), "first capture is new");
    CHECK(ft_siglib_holds(&lib, 10), "library should hold it");

    /* Duplicates are rejected rather than wasting a slot. */
    CHECK(!ft_siglib_capture(&lib, 10), "duplicate capture returns false");
    CHECK_EQ(lib.count, 1);

    /* Capturing id 0 is meaningless. */
    CHECK(!ft_siglib_capture(&lib, 0), "id 0 is not capturable");

    ft_siglib_capture(&lib, 11);
    ft_siglib_capture(&lib, 12);
    ft_siglib_capture(&lib, 13);
    CHECK_EQ(lib.count, FT_SIGLIB_SLOTS);

    /* Full: the oldest entry is overwritten. */
    CHECK(ft_siglib_capture(&lib, 14), "capture when full still succeeds");
    CHECK_EQ(lib.count, FT_SIGLIB_SLOTS);
    CHECK(!ft_siglib_holds(&lib, 10), "oldest entry should be evicted");
    CHECK(ft_siglib_holds(&lib, 14), "newest entry should be held");
    CHECK(ft_siglib_holds(&lib, 11), "second-oldest should survive");

    /* A replayed copy is weaker than the original. */
    CHECK_EQ(ft_siglib_replay_power(100), 75);
    CHECK_EQ(ft_siglib_replay_power(4), 3);
    CHECK_EQ(ft_siglib_replay_power(1), 1); /* never rounds down to harmless */
    CHECK_EQ(ft_siglib_replay_power(0), 0);
}

static void test_priority(void) {
    section("turn priority (DESIGN 4.6)");

    CHECK_EQ(ft_priority_for_enemy(0), FT_PRIO_ENEMY);
    CHECK_EQ(ft_priority_for_enemy(FT_ATTR_FAST), FT_PRIO_FAST_ENEMY);

    /* A FAST enemy acts before the player; a normal one after. */
    FtAction acts[4] = {
        {0, FT_PRIO_PLAYER_ATTACK, 0},
        {1, FT_PRIO_ENEMY, 1},
        {2, FT_PRIO_FAST_ENEMY, 2},
        {3, FT_PRIO_HEAL, 3},
    };
    ft_priority_sort(acts, 4);
    CHECK_EQ(acts[0].actor, 3); /* heal */
    CHECK_EQ(acts[1].actor, 2); /* fast enemy */
    CHECK_EQ(acts[2].actor, 0); /* player attack */
    CHECK_EQ(acts[3].actor, 1); /* ordinary enemy */

    /* Equal priorities keep their original order. */
    FtAction tied[3] = {
        {7, FT_PRIO_ENEMY, 0},
        {8, FT_PRIO_ENEMY, 1},
        {9, FT_PRIO_ENEMY, 2},
    };
    ft_priority_sort(tied, 3);
    CHECK_EQ(tied[0].actor, 7);
    CHECK_EQ(tied[1].actor, 8);
    CHECK_EQ(tied[2].actor, 9);
}

static void test_progression(void) {
    section("levelling and XP (DESIGN 4.1)");

    CHECK_EQ(ft_level_cap(0), 4);
    CHECK_EQ(ft_level_cap(1), 8);
    CHECK_EQ(ft_level_cap(5), 24); /* the Chapter 5 cap */

    FtStats s;
    ft_stats_init(&s);
    CHECK_EQ(s.charge_max, FT_START_CHARGE);
    CHECK_EQ(s.flash_max, FT_START_FLASH);
    CHECK_EQ(s.level, 1);

    /* A level-up raises one stat and fully restores Charge and RAM. */
    s.charge = 1;
    s.ram = 0;
    CHECK(ft_level_apply(&s, FT_UP_CHARGE), "charge upgrade should apply");
    CHECK_EQ(s.charge_max, FT_START_CHARGE + FT_LEVEL_UP_CHARGE);
    CHECK_EQ(s.charge, s.charge_max);
    CHECK_EQ(s.ram, s.ram_max);

    CHECK(ft_level_apply(&s, FT_UP_FLASH), "flash upgrade should apply");
    CHECK_EQ(s.flash_max, FT_START_FLASH + FT_LEVEL_UP_FLASH);

    /* Capped stats become unavailable as choices. */
    s.flash_max = FT_CAP_FLASH;
    CHECK(!ft_level_choice_available(&s, FT_UP_FLASH), "capped Flash is unavailable");
    CHECK(!ft_level_apply(&s, FT_UP_FLASH), "capped Flash cannot be raised");

    /* Underlevelled enemies taper to nothing. */
    CHECK_EQ(ft_xp_award(5, 3, 30), 30); /* enemy above the player: full */
    CHECK_EQ(ft_xp_award(3, 3, 30), 30); /* level: full */
    CHECK_EQ(ft_xp_award(3, 4, 30), 20); /* one under: two thirds */
    CHECK_EQ(ft_xp_award(3, 5, 30), 10); /* two under: one third */
    CHECK_EQ(ft_xp_award(3, 6, 30), 0);  /* three under: nothing */

    /* XP banking, level-ups owed, and the per-battle cap. */
    FtStats p;
    ft_stats_init(&p);
    CHECK_EQ(ft_xp_gain(&p, 99, 10), 0);
    CHECK_EQ(p.xp, 99);
    CHECK_EQ(ft_xp_gain(&p, 1, 10), 1); /* crosses 100 */
    CHECK_EQ(p.xp, 0);

    ft_stats_init(&p);
    CHECK_EQ(ft_xp_gain(&p, 500, 10), 1); /* capped at 100 per battle */

    /* At the level cap nothing is earned. */
    ft_stats_init(&p);
    p.level = 4;
    CHECK_EQ(ft_xp_gain(&p, 100, 4), 0);
    CHECK_EQ(p.xp, 0);
}

static void test_flash_budget(void) {
    section("Flash budget");

    FtStats s;
    ft_stats_init(&s);
    s.flash_max = 4;

    CHECK(ft_flash_can_install(&s, 3), "3 fits in 4");
    ft_flash_install(&s, 3);
    CHECK_EQ(s.flash_used, 3);

    CHECK(!ft_flash_can_install(&s, 2), "2 more does not fit");
    CHECK(ft_flash_can_install(&s, 1), "1 more does fit");

    ft_flash_uninstall(&s, 3);
    CHECK_EQ(s.flash_used, 0);

    /* Uninstalling past zero must not underflow. */
    ft_flash_uninstall(&s, 99);
    CHECK_EQ(s.flash_used, 0);
}

static void test_loadout(void) {
    section("module loadout (DESIGN 7)");

    FtLoadout lo;
    ft_loadout_init(&lo);

    /* The two base modules are installed and free. */
    FtLoadoutEffects fx = ft_loadout_effects(&lo);
    CHECK_EQ(fx.flash_used, 0);
    CHECK_EQ(fx.jam_reduction_pct, FT_JAM_REDUCTION_PCT);
    CHECK(!fx.hard_mode, "Hard Mode is opt-in");

    /* Stacking raises both the effect and the Flash spent. */
    CHECK(ft_loadout_add(&lo, FT_MOD_AMPLIFY), "first Amplify installs");
    CHECK(ft_loadout_add(&lo, FT_MOD_AMPLIFY), "second Amplify installs");
    fx = ft_loadout_effects(&lo);
    CHECK_EQ(fx.atk_up, 4);       /* 2 per stack */
    CHECK_EQ(fx.flash_used, 4);   /* 2 per stack */

    /* And the RAM cost scales with the stack count. */
    CHECK_EQ(ft_module_ram_cost(FT_MOD_AMPLIFY, 2), 2);
    CHECK_EQ(ft_module_ram_cost(FT_MOD_PAYLOAD, 2), 4);

    /* Max stacks is enforced. */
    CHECK(ft_loadout_add(&lo, FT_MOD_AMPLIFY), "third Amplify installs");
    CHECK(!ft_loadout_add(&lo, FT_MOD_AMPLIFY), "fourth exceeds max stacks");

    ft_loadout_add(&lo, FT_MOD_FARADAY);
    ft_loadout_add(&lo, FT_MOD_CHARGE_PLUS);
    ft_loadout_add(&lo, FT_MOD_DEEP_FOCUS);
    ft_loadout_add(&lo, FT_MOD_HARD_MODE);

    fx = ft_loadout_effects(&lo);
    CHECK_EQ(fx.jam_reduction_pct, FT_JAM_REDUCTION_PCT + 10);
    CHECK_EQ(fx.charge_max_bonus, 5);
    CHECK_EQ(fx.deep_focus_stacks, 1);
    CHECK(fx.hard_mode, "Hard Mode should be active once installed");

    /* Deep Focus feeds straight into the Focus gain. */
    CHECK_EQ(ft_signal_focus_gain(fx.deep_focus_stacks), FT_SIGNAL_GAIN_FOCUS + 5);
}

static void test_enemy_table(void) {
    section("enemy table (DESIGN 7)");

    /* Each M1 enemy exists to prove one lock. */
    CHECK_EQ(FT_ENEMIES[FT_ENEMY_STRAY_PACKET].attrs, 0);
    CHECK(FT_ENEMIES[FT_ENEMY_DRIFT_BEACON].attrs & FT_ATTR_AIRBORNE, "beacon is airborne");
    CHECK(FT_ENEMIES[FT_ENEMY_SEALED_LOCK].attrs & FT_ATTR_ENCRYPTED, "lock is encrypted");

    /* All attack ids are nonzero, or the Signal Library cannot key on them. */
    for(int e = 0; e < FT_ENEMY_COUNT; e++) {
        const FtEnemy* en = &FT_ENEMIES[e];
        CHECK(en->attack_count > 0, "%s has no attacks", en->name);
        for(int a = 0; a < en->attack_count; a++) {
            CHECK(en->attacks[a].id != 0, "%s attack %d has id 0", en->name, a);
        }
    }

    /* The two starting modules cover each other's blind spot. */
    FtHitParams p = mk_params(FT_GUARD_NONE);
    const FtAttack* subghz = &FT_MODULES[FT_MOD_SUBGHZ].attack;
    const FtAttack* nfc = &FT_MODULES[FT_MOD_NFC].attack;

    FtDefender beacon = {FT_ENEMIES[FT_ENEMY_DRIFT_BEACON].shielded,
                         FT_ENEMIES[FT_ENEMY_DRIFT_BEACON].attrs};
    CHECK_EQ(ft_resolve_hit(nfc, &beacon, &p).outcome, FT_HIT_LOCKED);
    CHECK_EQ(ft_resolve_hit(subghz, &beacon, &p).outcome, FT_HIT_OK);

    FtDefender lock = {FT_ENEMIES[FT_ENEMY_SEALED_LOCK].shielded,
                       FT_ENEMIES[FT_ENEMY_SEALED_LOCK].attrs};
    CHECK_EQ(ft_resolve_hit(subghz, &lock, &p).outcome, FT_HIT_LOCKED);
    CHECK_EQ(ft_resolve_hit(nfc, &lock, &p).outcome, FT_HIT_OK);
}

static void test_world_tour(void) {
    section("the chain is walkable");

    /* Every room must be reachable from the start by following exits, or a
     * concept slice is a room nobody can get to. */
    bool seen[32];
    for(uint8_t i = 0; i < 32u; i++) seen[i] = false;

    CHECK(ft_room_count() <= 32u, "the walk fits its bookkeeping");

    uint8_t stack[32];
    uint8_t top = 0;
    stack[top++] = 0;
    seen[0] = true;

    while(top > 0u) {
        const uint8_t r = stack[--top];
        const FtRoom* room = ft_room(r);

        for(uint8_t e = 0; e < room->exit_count; e++) {
            const uint8_t dest = room->exits[e].dest_room;
            if(dest >= ft_room_count() || seen[dest]) continue;

            seen[dest] = true;
            stack[top++] = dest;
        }
    }

    for(uint8_t r = 0; r < ft_room_count(); r++) {
        CHECK(seen[r], "room %u (%s) is reachable", r, ft_room(r)->map->name);
    }

    /* Every entity must stand on ground, and on ground the player can reach
     * from a door. A foe sealed inside a pocket is a fight nobody can start
     * and an encounter marker that never clears. */
    for(uint8_t r = 0; r < ft_room_count(); r++) {
        const FtRoom* room = ft_room(r);

        for(uint8_t i = 0; i < room->ent_count; i++) {
            const FtEntity* ent = &room->ents[i];
            CHECK(!ft_tile_solid(ft_map_tile(room->map, ent->tx, ent->ty)),
                  "room %u entity %u stands on open ground", r, i);

            /* Flood from the room's first door and require the entity to be
             * in the same region. */
            const FtMap* m = room->map;
            static bool reach[64 * 32];
            const uint32_t cells = (uint32_t)m->w * m->h;
            CHECK(cells <= sizeof(reach) / sizeof(reach[0]), "the flood fits");

            for(uint32_t k = 0; k < cells; k++) reach[k] = false;

            static uint16_t queue[64 * 32];
            uint32_t head = 0, tail = 0;

            const uint16_t sx = room->exits[0].tx, sy = room->exits[0].ty;
            reach[(uint32_t)sy * m->w + sx] = true;
            queue[tail++] = (uint16_t)((uint32_t)sy * m->w + sx);

            while(head < tail) {
                const uint32_t cell = queue[head++];
                const int32_t cx = (int32_t)(cell % m->w);
                const int32_t cy = (int32_t)(cell / m->w);

                static const int8_t STEP[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                for(uint8_t d = 0; d < 4u; d++) {
                    const int32_t nx = cx + STEP[d][0], ny = cy + STEP[d][1];
                    if(nx < 0 || ny < 0 || nx >= (int32_t)m->w || ny >= (int32_t)m->h) {
                        continue;
                    }
                    const uint32_t n = (uint32_t)ny * m->w + (uint32_t)nx;
                    if(reach[n] || ft_tile_solid(ft_map_tile(m, nx, ny))) continue;

                    reach[n] = true;
                    queue[tail++] = (uint16_t)n;
                }
            }

            CHECK(reach[(uint32_t)ent->ty * m->w + ent->tx],
                  "room %u entity %u can be walked to", r, i);

            /* And so can every exit, or the chain is broken inside a room. */
            for(uint8_t x = 0; x < room->exit_count; x++) {
                const uint32_t cell =
                    (uint32_t)room->exits[x].ty * m->w + room->exits[x].tx;
                CHECK(reach[cell], "room %u exit %u can be walked to", r, x);
            }
        }
    }

    /* And every room can be left again, or it is a trap. */
    for(uint8_t r = 0; r < ft_room_count(); r++) {
        CHECK(ft_room(r)->exit_count > 0, "room %u has a way out", r);
    }

    /* Each area slice should actually look like somewhere: at least one tile
     * the prologue does not use. A room built only from corridor tiles is a
     * corridor with a different name over the door. */
    static const FtTile FLAVOUR[] = {
        FT_TILE_SCRAP, FT_TILE_FROST, FT_TILE_PYLON, FT_TILE_STATIC};

    int flavoured = 0;
    for(uint8_t r = 4; r < ft_room_count(); r++) {
        const FtMap* m = ft_room(r)->map;
        bool has = false;

        for(uint16_t y = 0; y < m->h; y++) {
            for(uint16_t x = 0; x < m->w; x++) {
                const FtTile t = ft_map_tile(m, x, y);
                for(size_t k = 0; k < sizeof(FLAVOUR) / sizeof(FLAVOUR[0]); k++) {
                    if(t == FLAVOUR[k]) has = true;
                }
            }
        }
        if(has) flavoured++;

        /* The Turnstile is the exception: its identity is the locked ports,
         * which are an existing tile. */
        CHECK(has || m->name[0] == 'T', "room %u (%s) has a look of its own",
              r, m->name);
    }
    CHECK(flavoured >= 4, "at least four areas have their own ground");

    /* Every area slice carries the gate for the chapter after it. */
    for(uint8_t r = 4; r < ft_room_count(); r++) {
        const FtMap* m = ft_room(r)->map;
        int locks = 0;

        for(uint16_t y = 0; y < m->h; y++) {
            for(uint16_t x = 0; x < m->w; x++) {
                if(ft_map_tile(m, x, y) == FT_TILE_LOCK) locks++;
            }
        }
        CHECK(locks > 0, "room %u (%s) shows a gate", r, m->name);
    }
}

static void test_rng(void) {
    section("deterministic RNG");

    FtRng a, b;
    ft_rng_seed(&a, 12345);
    ft_rng_seed(&b, 12345);
    for(int i = 0; i < 64; i++) {
        CHECK_EQ(ft_rng_next(&a), ft_rng_next(&b));
    }

    /* A zero seed must not lock the generator at zero. */
    FtRng z;
    ft_rng_seed(&z, 0);
    CHECK(ft_rng_next(&z) != 0, "zero seed must still produce values");

    /* Bounds hold. */
    FtRng r;
    ft_rng_seed(&r, 99);
    for(int i = 0; i < 1000; i++) {
        CHECK_EQ(ft_rng_below(&r, 6) < 6, 1);
    }
    CHECK_EQ(ft_rng_below(&r, 0), 0);
    CHECK(!ft_rng_chance(&r, 0), "0%% never fires");
    CHECK(ft_rng_chance(&r, 100), "100%% always fires");
}


static void test_guard_timing(void) {
    section("guard windows (DESIGN 4.4)");

    /* Innermost 50 ms captures. */
    CHECK_EQ(ft_guard_from_timing(0, false, FT_CLASS_NORMAL), FT_GUARD_CAPTURE);
    CHECK_EQ(ft_guard_from_timing(50, false, FT_CLASS_NORMAL), FT_GUARD_CAPTURE);

    /* Out to 150 ms jams. */
    CHECK_EQ(ft_guard_from_timing(51, false, FT_CLASS_NORMAL), FT_GUARD_JAM);
    CHECK_EQ(ft_guard_from_timing(150, false, FT_CLASS_NORMAL), FT_GUARD_JAM);

    /* Earlier than that has lapsed by the time the hit lands. */
    CHECK_EQ(ft_guard_from_timing(151, false, FT_CLASS_NORMAL), FT_GUARD_NONE);
    CHECK_EQ(ft_guard_from_timing(5000, false, FT_CLASS_NORMAL), FT_GUARD_NONE);

    /* A press after impact is late, not a guard. */
    CHECK_EQ(ft_guard_from_timing(-1, false, FT_CLASS_NORMAL), FT_GUARD_NONE);

    /* Hard Mode halves both windows. */
    CHECK_EQ(ft_guard_from_timing(25, true, FT_CLASS_NORMAL), FT_GUARD_CAPTURE);
    CHECK_EQ(ft_guard_from_timing(26, true, FT_CLASS_NORMAL), FT_GUARD_JAM);
    CHECK_EQ(ft_guard_from_timing(75, true, FT_CLASS_NORMAL), FT_GUARD_JAM);
    CHECK_EQ(ft_guard_from_timing(76, true, FT_CLASS_NORMAL), FT_GUARD_NONE);

    /* What was a capture on normal is only a jam on Hard Mode. */
    CHECK_EQ(ft_guard_from_timing(40, false, FT_CLASS_NORMAL), FT_GUARD_CAPTURE);
    CHECK_EQ(ft_guard_from_timing(40, true, FT_CLASS_NORMAL), FT_GUARD_JAM);
}

static void test_rating_timing(void) {
    section("action command bands");

    CHECK_EQ(ft_rating_from_timing(0), FT_RATING_EXCELLENT);
    CHECK_EQ(ft_rating_from_timing(FT_BAND_EXCELLENT_MS), FT_RATING_EXCELLENT);
    CHECK_EQ(ft_rating_from_timing(FT_BAND_EXCELLENT_MS + 1), FT_RATING_AMAZING);
    CHECK_EQ(ft_rating_from_timing(FT_BAND_GREAT_MS), FT_RATING_AMAZING);
    CHECK_EQ(ft_rating_from_timing(FT_BAND_GOOD_MS), FT_RATING_GREAT);
    CHECK_EQ(ft_rating_from_timing(FT_BAND_NICE_MS), FT_RATING_GOOD);
    CHECK_EQ(ft_rating_from_timing(FT_BAND_NICE_MS + 1), FT_RATING_MISS);

    /* Each band must be wide enough to render as a visible block on a 120px
     * track, or the skill check cannot be read (see ft_render.c). */
    CHECK((FT_BAND_EXCELLENT_MS * 2 * 120) / FT_ACTION_WINDOW_MS >= 10,
          "EXCELLENT band must be at least 10px wide");

    /* Early and late are punished identically. */
    for(int32_t d = 0; d <= 300; d += 7) {
        CHECK_EQ(ft_rating_from_timing(d), ft_rating_from_timing(-d));
    }
}

static void test_guarded_window(void) {
    section("GUARDED tightens the jam window");

    /* A GUARDED attack cannot be captured, so its jam window shrinks to the
     * capture window's width: losing the reward should cost precision rather
     * than just removing an option. */
    CHECK_EQ(ft_jam_window_ms(false, FT_CLASS_NORMAL), FT_JAM_WINDOW_MS);
    CHECK_EQ(ft_jam_window_ms(false, FT_CLASS_GUARDED), FT_CAPTURE_WINDOW_MS);
    CHECK(ft_jam_window_ms(false, FT_CLASS_GUARDED) < ft_jam_window_ms(false, FT_CLASS_NORMAL),
          "guarded is the tighter of the two");

    /* Hard Mode halves both. */
    CHECK_EQ(ft_jam_window_ms(true, FT_CLASS_NORMAL), FT_JAM_WINDOW_MS / 2);
    CHECK_EQ(ft_jam_window_ms(true, FT_CLASS_GUARDED), FT_CAPTURE_WINDOW_MS / 2);

    /* A press that would jam a normal attack misses a guarded one entirely. */
    const int32_t mid = (FT_CAPTURE_WINDOW_MS + FT_JAM_WINDOW_MS) / 2;
    CHECK_EQ(ft_guard_from_timing(mid, false, FT_CLASS_NORMAL), FT_GUARD_JAM);
    CHECK_EQ(ft_guard_from_timing(mid, false, FT_CLASS_GUARDED), FT_GUARD_NONE);

    /* Inside the tight window it still jams, but never captures. */
    CHECK_EQ(ft_guard_from_timing(10, false, FT_CLASS_GUARDED), FT_GUARD_JAM);
    CHECK_EQ(ft_guard_from_timing(10, false, FT_CLASS_NORMAL), FT_GUARD_CAPTURE);

    /* Undodgeable refuses everything at any timing. */
    for(int32_t t = 0; t < 400; t += 10) {
        CHECK_EQ(ft_guard_from_timing(t, false, FT_CLASS_UNDODGEABLE), FT_GUARD_NONE);
    }
}

static void test_turn_economy(void) {
    section("solo turn economy");

    /* One player action against three foe actions deletes a level-one
     * character; the simulator measured 0% wins. The player gets two turns to
     * the enemy round, as the reference does for a lone player. */
    CHECK(FT_PLAYER_TURNS_PER_ROUND >= 2, "a lone player acts more than once per round");

    FtLoadout lo;
    ft_loadout_init(&lo);

    const FtEnemyId group[3] = {
        FT_ENEMY_STRAY_PACKET, FT_ENEMY_DRIFT_BEACON, FT_ENEMY_SEALED_LOCK};

    FtEncounter e;
    ft_encounter_init(&e, group, 3, &lo, 5);
    CHECK_EQ(e.player_turns, 0);

    /* Take one action and run out the result hold: it should be the player's
     * go again, not the enemies'. */
    e.menu_index = FT_ACTION_DEFEND;
    ft_encounter_press_ok(&e);
    CHECK_EQ(e.player_turns, 1);
    CHECK_EQ(e.phase, FT_PHASE_RESULT);

    ft_encounter_tick(&e, FT_IMPACT_HOLD_MS + 10);
    CHECK_EQ(e.phase, FT_PHASE_MENU);

    /* The second action hands the round over. */
    e.menu_index = FT_ACTION_DEFEND;
    ft_encounter_press_ok(&e);
    CHECK_EQ(e.player_turns, 2);

    ft_encounter_tick(&e, FT_IMPACT_HOLD_MS + 10);
    CHECK_EQ(e.phase, FT_PHASE_TELEGRAPH);
}

static void test_death_timing(void) {
    section("foes die when the attack lands");

    FtLoadout lo;
    ft_loadout_init(&lo);

    FtEncounter e;
    ft_encounter_init_single(&e, FT_ENEMY_STRAY_PACKET, &lo, 7);

    /* Kill it outright, then check it is still drawn until the strike frame:
     * a foe that drops dead before the attack visibly reaches it looks broken. */
    e.phase = FT_PHASE_RESULT;
    e.phase_ms = 0;
    e.foe_charge_before[0] = 8;
    e.foes[0].charge = 0;

    CHECK(ft_encounter_in_anim(&e), "the attack is still travelling");
    CHECK(ft_encounter_foe_visible(&e, 0), "and the foe is still on screen");
    CHECK_EQ(ft_encounter_foe_shown_charge(&e, 0), 8);

    /* Past the strike, it is gone and the bar reads empty. */
    e.phase_ms = FT_ANIM_MS;
    CHECK(!ft_encounter_foe_visible(&e, 0), "now it is down");
    CHECK_EQ(ft_encounter_foe_shown_charge(&e, 0), 0);

    /* A survivor is shown throughout, at its pre-hit value while in flight. */
    e.phase_ms = 0;
    e.foe_charge_before[0] = 8;
    e.foes[0].charge = 3;
    CHECK(ft_encounter_foe_visible(&e, 0), "a survivor stays visible");
    CHECK_EQ(ft_encounter_foe_shown_charge(&e, 0), 8);

    e.phase_ms = FT_ANIM_MS;
    CHECK_EQ(ft_encounter_foe_shown_charge(&e, 0), 3);
}

static void test_ready_beat(void) {
    section("ready beat");

    FtLoadout lo;
    ft_loadout_init(&lo);

    FtEncounter e;
    ft_encounter_init_single(&e, FT_ENEMY_STRAY_PACKET, &lo, 2);
    e.menu_index = FT_ACTION_CONTACT;
    ft_encounter_press_ok(&e);
    CHECK_EQ(e.phase, FT_PHASE_PLAYER_ACT);

    /* The cursor is held still for the whole beat. */
    CHECK(ft_encounter_in_ready(&e), "a fresh sweep starts in the ready beat");
    CHECK_EQ(ft_encounter_sweep_ms(&e), 0);

    ft_encounter_tick(&e, FT_READY_MS - 1);
    CHECK(ft_encounter_in_ready(&e), "still ready one ms before the release");
    CHECK_EQ(ft_encounter_sweep_ms(&e), 0);

    ft_encounter_tick(&e, 2);
    CHECK(!ft_encounter_in_ready(&e), "released once the beat elapses");
    CHECK_EQ(ft_encounter_sweep_ms(&e), 1);

    /* The sweep clock saturates at the window rather than running past it. */
    ft_encounter_tick(&e, FT_ACTION_WINDOW_MS * 4);
    CHECK(ft_encounter_sweep_ms(&e) <= FT_ACTION_WINDOW_MS,
          "sweep clock must not exceed its window");

    /* The guard sweep gets the same lead-in, and a press during it is ignored
     * rather than counting as a wildly early guard. */
    FtEncounter g;
    ft_encounter_init_single(&g, FT_ENEMY_STRAY_PACKET, &lo, 4);
    g.phase = FT_PHASE_TELEGRAPH;
    g.phase_ms = 0;
    CHECK(ft_encounter_in_ready(&g), "guard sweep also starts with a beat");

    ft_encounter_press_ok(&g);
    CHECK(!g.guard_pressed, "ready-beat guard presses must be ignored");

    /* Phases without a timing bar never report a ready beat. */
    FtEncounter m;
    ft_encounter_init_single(&m, FT_ENEMY_STRAY_PACKET, &lo, 6);
    CHECK(!ft_encounter_in_ready(&m), "the menu is not a ready beat");
    CHECK_EQ(ft_encounter_sweep_window(&m), 0);
}

static void test_encounter(void) {
    section("encounter state machine");

    FtLoadout lo;
    ft_loadout_init(&lo);

    FtEncounter e;
    ft_encounter_init_single(&e, FT_ENEMY_STRAY_PACKET, &lo, 7);

    CHECK_EQ(e.phase, FT_PHASE_MENU);
    CHECK_EQ(e.foes[0].charge, FT_ENEMIES[FT_ENEMY_STRAY_PACKET].charge);
    CHECK_EQ(e.foe_count, 1);
    CHECK_EQ(ft_encounter_living(&e), 1);
    CHECK(!ft_encounter_over(&e), "a fresh encounter is not over");
    CHECK(ft_encounter_incoming(&e) == NULL, "nothing incoming during the menu");

    /* The root bar is three entries wide and wraps both ways. menu_index is
     * no longer the cursor: it is the action the cursors currently resolve
     * to, so every rule written against it still applies. */
    CHECK_EQ(FT_ACTION_COUNT, 5);
    CHECK_EQ(FT_ROOT_COUNT, 3);
    CHECK_EQ(e.menu_level, FT_MENU_ROOT);
    CHECK_EQ(e.root_index, FT_ROOT_ATTACK);
    ft_encounter_menu_move(&e, -1);
    CHECK_EQ(e.root_index, FT_ROOT_COUNT - 1);
    CHECK_EQ(e.menu_index, FT_ACTION_FOCUS);
    ft_encounter_menu_move(&e, 1);
    CHECK_EQ(e.root_index, FT_ROOT_ATTACK);
    CHECK_EQ(e.menu_index, (uint8_t)FT_ATTACK_ITEMS[0]);

    /* Attack drills into the module panel instead of spending the turn. */
    ft_encounter_menu_confirm(&e);
    CHECK_EQ(e.menu_level, FT_MENU_ATTACK);
    CHECK_EQ(e.phase, FT_PHASE_MENU);
    ft_encounter_menu_move(&e, -1);
    CHECK_EQ(e.attack_index, FT_ATTACK_COUNT - 1);
    CHECK_EQ(e.menu_index, (uint8_t)FT_ATTACK_ITEMS[FT_ATTACK_COUNT - 1]);

    /* Back closes the panel and keeps the module it left selected, so the
     * root row previews what Attack would fire. */
    CHECK(ft_encounter_menu_back(&e), "Back leaves the attack panel");
    CHECK_EQ(e.menu_level, FT_MENU_ROOT);
    CHECK_EQ(e.menu_index, (uint8_t)FT_ATTACK_ITEMS[FT_ATTACK_COUNT - 1]);
    CHECK(!ft_encounter_menu_back(&e), "Back at the root is the caller's to handle");

    /* Defend and Focus commit on a single press, as they always did. */
    ft_encounter_menu_move(&e, 1);
    CHECK_EQ(e.menu_index, FT_ACTION_DEFEND);
    e.attack_index = 0;
    ft_encounter_menu_move(&e, -1);
    CHECK_EQ(e.menu_index, (uint8_t)FT_ATTACK_ITEMS[0]);

    /* Thinking must never cost Charge: the roll is paused in the menu. */
    e.roll.target = 0;
    const int16_t before = e.roll.current;
    ft_encounter_tick(&e, 10000);
    CHECK_EQ(e.roll.current, before);
    CHECK_EQ(e.phase, FT_PHASE_MENU);

    /* Attributes never take a module off the list: every attack is always
     * selectable, and reach decides who it lands on. */
    FtEncounter beacon;
    ft_encounter_init_single(&beacon, FT_ENEMY_DRIFT_BEACON, &lo, 1);
    CHECK(ft_encounter_action_available(&beacon, FT_ACTION_BROADCAST), "broadcast is offered");
    CHECK(ft_encounter_action_available(&beacon, FT_ACTION_CONTACT), "so is contact");
    CHECK(ft_encounter_action_available(&beacon, FT_ACTION_DEFEND), "Defend is always available");
    CHECK(!ft_encounter_can_reach(&beacon, FT_ACTION_CONTACT, 0), "but contact cannot reach it");

    FtEncounter lock;
    ft_encounter_init_single(&lock, FT_ENEMY_SEALED_LOCK, &lo, 1);
    CHECK(ft_encounter_action_available(&lock, FT_ACTION_BROADCAST), "broadcast is offered");
    CHECK(!ft_encounter_can_reach(&lock, FT_ACTION_BROADCAST, 0), "but bounces off ENCRYPTED");
    CHECK(ft_encounter_can_reach(&lock, FT_ACTION_CONTACT, 0), "contact opens ENCRYPTED");

    /* A resource you do not have still stops the press — that is the only
     * thing left that can. */
    FtEncounter empty;
    ft_encounter_init_single(&empty, FT_ENEMY_STRAY_PACKET, &lo, 1);
    CHECK(!ft_encounter_action_available(&empty, FT_ACTION_SIGNAL), "no capture, no replay");
    empty.menu_index = FT_ACTION_SIGNAL;
    ft_encounter_press_ok(&empty);
    CHECK_EQ(empty.phase, FT_PHASE_MENU);

    /* A perfectly timed action command earns the top rating. */
    FtEncounter fight;
    ft_encounter_init_single(&fight, FT_ENEMY_STRAY_PACKET, &lo, 3);
    fight.menu_index = FT_ACTION_CONTACT;
    ft_encounter_press_ok(&fight);
    CHECK_EQ(fight.phase, FT_PHASE_PLAYER_ACT);

    /* Presses during the ready beat are ignored outright. */
    ft_encounter_tick(&fight, FT_READY_MS / 2);
    CHECK(ft_encounter_in_ready(&fight), "the sweep has not started yet");
    ft_encounter_press_ok(&fight);
    CHECK(!fight.action_pressed, "ready-beat presses must be ignored");
    CHECK_EQ(ft_encounter_sweep_ms(&fight), 0);

    /* Land the press at the middle of the sweep, which is the perfect moment. */
    ft_encounter_tick(&fight, (FT_READY_MS / 2) + FT_ACTION_WINDOW_MS / 2);
    CHECK(!ft_encounter_in_ready(&fight), "the sweep should have started");
    ft_encounter_press_ok(&fight);

    /* Mashing must not improve on the first press. */
    const uint32_t recorded = fight.action_press_ms;
    ft_encounter_tick(&fight, 50);
    ft_encounter_press_ok(&fight);
    CHECK_EQ(fight.action_press_ms, recorded);

    ft_encounter_tick(&fight, FT_ACTION_WINDOW_MS);
    CHECK_EQ(fight.phase, FT_PHASE_RESULT);
    CHECK_EQ(fight.last_rating, FT_RATING_EXCELLENT);
    CHECK(fight.last_player_hit.damage > 0, "a perfect contact hit should land damage");

    /* Focus feeds the Signal meter without an action command. */
    FtEncounter focus;
    ft_encounter_init_single(&focus, FT_ENEMY_STRAY_PACKET, &lo, 5);
    const int16_t sig_before = focus.signal.value;
    focus.menu_index = FT_ACTION_FOCUS;
    ft_encounter_press_ok(&focus);
    CHECK_EQ(focus.phase, FT_PHASE_RESULT); /* skips the sweep entirely */
    CHECK_EQ(focus.signal.value, sig_before + ft_signal_focus_gain(0));

    /* A perfect hit one-shots the tutorial enemy (4 power at 200%), so the
     * full-battle run uses the toughest M1 enemy to guarantee the player is
     * actually attacked. */
    FtEncounter quick;
    ft_encounter_init_single(&quick, FT_ENEMY_STRAY_PACKET, &lo, 3);
    CHECK(FT_ENEMIES[FT_ENEMY_STRAY_PACKET].charge <= 8, "tutorial enemy stays one-shottable");

    /* A whole battle terminates rather than spinning forever. */
    FtEncounter run;
    ft_encounter_init_single(&run, FT_ENEMY_SEALED_LOCK, &lo, 11);
    int guard_ticks = 0;
    for(int i = 0; i < 20000 && !ft_encounter_over(&run); i++) {
        if(run.phase == FT_PHASE_MENU) {
            run.menu_index = FT_ACTION_CONTACT;
            ft_encounter_press_ok(&run);
        } else if(
            run.phase == FT_PHASE_TELEGRAPH &&
            run.phase_ms >= FT_READY_MS + FT_TELEGRAPH_MS - 20) {
            ft_encounter_press_ok(&run);
            guard_ticks++;
        }
        ft_encounter_tick(&run, 10);
    }
    CHECK(ft_encounter_over(&run), "a played-out battle must terminate");
    CHECK_EQ(run.phase, FT_PHASE_WIN);
    CHECK(guard_ticks > 0, "the run should have faced at least one attack");

    /* Capture, end to end. Stray Packet's only attack is NORMAL class, so it
     * is capturable. The player broadcasts and never presses the action
     * command, so damage stays low enough that the foe survives to take a
     * turn — with two player turns per enemy round, a timed contact hit kills
     * it before it ever acts. */
    FtEncounter cap;
    ft_encounter_init_single(&cap, FT_ENEMY_STRAY_PACKET, &lo, 11);
    bool faced_attack = false;
    for(int i = 0; i < 20000 && !ft_encounter_over(&cap); i++) {
        if(cap.phase == FT_PHASE_MENU) {
            cap.menu_index = FT_ACTION_BROADCAST;
            ft_encounter_press_ok(&cap);
        } else if(
            cap.phase == FT_PHASE_TELEGRAPH &&
            cap.phase_ms >= FT_READY_MS + FT_TELEGRAPH_MS - 20) {
            ft_encounter_press_ok(&cap);
            faced_attack = true;
        }
        ft_encounter_tick(&cap, 10);
    }
    CHECK(faced_attack, "the capture run should have faced an attack");
    CHECK_EQ(cap.last_guard, FT_GUARD_CAPTURE);
    CHECK(cap.lib.count > 0, "a frame-perfect guard should capture the signal");
    CHECK(ft_siglib_holds(&cap.lib, FT_ENEMIES[FT_ENEMY_STRAY_PACKET].attacks[0].id),
          "the captured id should be the attack that was guarded");

    /* An UNDODGEABLE attack can never be captured, however well timed. */
    FtEncounter undo;
    ft_encounter_init_single(&undo, FT_ENEMY_SEALED_LOCK, &lo, 11);
    undo.phase = FT_PHASE_TELEGRAPH;
    undo.foes[0].attack_index = 1; /* Seal: UNDODGEABLE */
    undo.guard_pressed = true;
    undo.guard_press_ms = FT_TELEGRAPH_MS; /* frame perfect on the sweep clock */
    ft_encounter_tick(&undo, 0);
    CHECK_EQ(FT_ENEMIES[FT_ENEMY_SEALED_LOCK].attacks[1].klass, FT_CLASS_UNDODGEABLE);
}


static void test_tutorial(void) {
    section("contextual coaching");

    FtLoadout lo;
    ft_loadout_init(&lo);

    FtEncounter e;
    ft_encounter_init_single(&e, FT_ENEMY_STRAY_PACKET, &lo, 3);

    /* On by default, and silent the moment it is turned off. */
    CHECK(e.coach, "coaching starts on");
    CHECK(ft_tutorial_hint(&e) != NULL, "the menu should be coached");

    e.coach = false;
    CHECK(ft_tutorial_hint(&e) == NULL, "coaching off means silence");
    e.coach = true;

    /* Only a missing resource refuses an action now, and it still explains
     * itself in the menu's description row. */
    FtEncounter beacon;
    ft_encounter_init_single(&beacon, FT_ENEMY_DRIFT_BEACON, &lo, 3);
    CHECK(ft_encounter_action_block(&beacon, FT_ACTION_CONTACT) == NULL,
          "an attribute is not a refusal");
    CHECK(ft_encounter_action_block(&beacon, FT_ACTION_BROADCAST) == NULL,
          "nor is any other attribute");

    const char* need = ft_encounter_action_block(&beacon, FT_ACTION_SIGNAL);
    CHECK(need != NULL, "a replay with nothing captured still refuses");
    CHECK(strlen(need) <= FT_TUTORIAL_MAX_CHARS, "and says so in one line");

    /* The line tracks the phase, including the ready beat. */
    e.phase = FT_PHASE_PLAYER_ACT;
    e.phase_ms = 100;
    CHECK(ft_encounter_in_ready(&e), "still in the lead-in");
    const char* wait = ft_tutorial_hint(&e);
    CHECK(wait && strstr(wait, "Wait"), "the ready beat should say to wait");

    e.phase_ms = FT_READY_MS + 100;
    const char* now = ft_tutorial_hint(&e);
    CHECK(now && strstr(now, "OK"), "the sweep should say to tap");
    CHECK(now != wait, "the line must change when the cursor is released");

    /* An UNDODGEABLE attack is called out as unguardable. */
    FtEncounter undo;
    ft_encounter_init_single(&undo, FT_ENEMY_SEALED_LOCK, &lo, 3);
    undo.phase = FT_PHASE_TELEGRAPH;
    undo.foes[0].attack_index = 1;
    CHECK_EQ(FT_ENEMIES[FT_ENEMY_SEALED_LOCK].attacks[1].klass, FT_CLASS_UNDODGEABLE);
    /* It names the thing that actually does not work (the timed jam) and the
     * thing that does (PROTECT). "Undodgeable" read as "nothing helps", which
     * is not true: the Defend shield still blunts it, and the coach has
     * always said to brace. */
    const char* brace = ft_tutorial_hint(&undo);
    CHECK(brace && strstr(brace, "No jam"), "an unjammable attack should say so");
    CHECK(brace && strstr(brace, "PROTECT"), "and should name what does work");

    /* And that is the mechanic, not just the wording: bracing reduces it. */
    const FtAttack* seal = &FT_ENEMIES[FT_ENEMY_SEALED_LOCK].attacks[1];
    const FtHitParams bare = {0, 0, FT_RATING_MISS, false, FT_GUARD_NONE, 0};
    const FtDefender open_guard = {0, 0};
    const FtDefender braced = {FT_DEFEND_SHIELD, 0};

    const FtHitResult hit_open = ft_resolve_hit(seal, &open_guard, &bare);
    const FtHitResult hit_braced = ft_resolve_hit(seal, &braced, &bare);
    CHECK(hit_braced.damage < hit_open.damage,
          "PROTECT blunts an unjammable hit (%d vs %d)",
          (int)hit_braced.damage, (int)hit_open.damage);

    /* A timed press on it, however, is worth nothing at all. */
    CHECK_EQ(ft_guard_from_timing(0, false, FT_CLASS_UNDODGEABLE), FT_GUARD_NONE);
    CHECK_EQ(ft_guard_permitted(FT_CLASS_UNDODGEABLE, FT_GUARD_CAPTURE), FT_GUARD_NONE);

    /* The Lock's *other* attack is an ordinary one, which is why it can be
     * jammed: a foe whose whole moveset is unguardable is just damage. */
    CHECK_EQ(FT_ENEMIES[FT_ENEMY_SEALED_LOCK].attack_count, 2);
    CHECK_EQ(FT_ENEMIES[FT_ENEMY_SEALED_LOCK].attacks[0].klass, FT_CLASS_NORMAL);

    /* Feedback after a guard distinguishes a jam from a capture. */
    FtEncounter jam;
    ft_encounter_init_single(&jam, FT_ENEMY_DRIFT_BEACON, &lo, 3);
    jam.phase = FT_PHASE_IMPACT;
    jam.last_guard = FT_GUARD_JAM;
    jam.last_enemy_hit.damage = 3;
    const char* jam_line = ft_tutorial_hint(&jam);
    CHECK(jam_line && strstr(jam_line, "later"), "a jam should point at the capture zone");

    jam.last_enemy_hit.captured = true;
    const char* cap_line = ft_tutorial_hint(&jam);
    CHECK(cap_line && strstr(cap_line, "Kept"), "a capture should be celebrated");

    /* Outcome screens stay quiet: they have their own copy. */
    FtEncounter done;
    ft_encounter_init_single(&done, FT_ENEMY_STRAY_PACKET, &lo, 3);
    done.phase = FT_PHASE_WIN;
    CHECK(ft_tutorial_hint(&done) == NULL, "the win screen is not coached");

    /* Every line the coach can produce must fit the panel. Walk the reachable
     * states rather than trusting the literals by eye. */
    int checked = 0;
    for(int enemy = 0; enemy < FT_ENEMY_COUNT; enemy++) {
        for(int phase = 0; phase <= FT_PHASE_LOSE; phase++) {
            for(int menu = 0; menu < FT_ACTION_COUNT; menu++) {
                for(int atk = 0; atk < FT_ENEMY_MAX_ATTACKS; atk++) {
                    for(int ready = 0; ready < 2; ready++) {
                        FtEncounter w;
                        ft_encounter_init_single(&w, (FtEnemyId)enemy, &lo, 1);
                        w.phase = (FtPhase)phase;
                        w.menu_index = (uint8_t)menu;
                        w.foes[0].attack_index =
                            (uint8_t)(atk % FT_ENEMIES[enemy].attack_count);
                        w.phase_ms = ready ? 0u : (FT_READY_MS + 50u);

                        const char* line = ft_tutorial_hint(&w);
                        if(line) {
                            checked++;
                            CHECK(strlen(line) <= FT_TUTORIAL_MAX_CHARS,
                                  "hint too long (%zu): \"%s\"", strlen(line), line);
                        }
                    }
                }
            }
        }
    }
    CHECK(checked > 50, "the sweep should have exercised many states");
}

static void test_anim(void) {
    section("action animation");

    FtLoadout lo;
    ft_loadout_init(&lo);

    FtEncounter e;
    ft_encounter_init_single(&e, FT_ENEMY_STRAY_PACKET, &lo, 5);

    /* Phases without a resolved action never animate. */
    CHECK(!ft_encounter_in_anim(&e), "the menu does not animate");
    CHECK_EQ(ft_encounter_anim_progress(&e), 255);

    e.phase = FT_PHASE_RESULT;
    e.phase_ms = 0;
    CHECK(ft_encounter_in_anim(&e), "a fresh result starts animating");
    CHECK_EQ(ft_encounter_anim_progress(&e), 0);

    e.phase_ms = FT_ANIM_MS / 2;
    const uint8_t mid = ft_encounter_anim_progress(&e);
    CHECK(mid > 100 && mid < 160, "progress should be about half way");

    /* Once the animation is done the popup gets the arena, and progress
     * saturates rather than wrapping. */
    e.phase_ms = FT_ANIM_MS;
    CHECK(!ft_encounter_in_anim(&e), "animation ends on time");
    CHECK_EQ(ft_encounter_anim_progress(&e), 255);

    e.phase_ms = FT_ANIM_MS * 10;
    CHECK_EQ(ft_encounter_anim_progress(&e), 255);

    /* The popup must still get a decent read after the animation. */
    CHECK(FT_IMPACT_HOLD_MS - FT_ANIM_MS >= 300,
          "the popup needs at least 300ms on screen");
}


static void test_map(void) {
    section("overworld map");

    /* A tiny hand-built map: a walled box with a void and a crate inside. */
    static const uint8_t TILES[6 * 4] = {
        1, 1, 1, 1, 1, 1,
        1, 0, 0, 2, 0, 1,
        1, 0, 8, 0, 3, 1,
        1, 1, 1, 1, 1, 1,
    };
    const FtMap m = {TILES, 6, 4, "Test", 0};

    CHECK_EQ(ft_map_tile(&m, 0, 0), FT_TILE_WALL);
    CHECK_EQ(ft_map_tile(&m, 1, 1), FT_TILE_FLOOR);
    CHECK_EQ(ft_map_tile(&m, 3, 1), FT_TILE_VOID);
    CHECK_EQ(ft_map_tile(&m, 4, 2), FT_TILE_GRASS);

    /* Off-map reads as wall, so the world has edges without every caller
     * bounds-checking. */
    CHECK_EQ(ft_map_tile(&m, -1, 0), FT_TILE_WALL);
    CHECK_EQ(ft_map_tile(&m, 99, 0), FT_TILE_WALL);
    CHECK_EQ(ft_map_tile(&m, 0, -5), FT_TILE_WALL);
    CHECK_EQ(ft_map_tile(NULL, 0, 0), FT_TILE_WALL);

    /* What stops you, and what does not. */
    CHECK(ft_tile_solid(FT_TILE_WALL), "walls are solid");
    CHECK(ft_tile_solid(FT_TILE_VOID), "voids are solid");
    CHECK(ft_tile_solid(FT_TILE_CRATE), "crates are solid");
    CHECK(ft_tile_solid(FT_TILE_LOCK), "locked ports are solid until opened");
    CHECK(!ft_tile_solid(FT_TILE_FLOOR), "floor is walkable");
    CHECK(!ft_tile_solid(FT_TILE_GRASS), "grass is walkable");
    CHECK(!ft_tile_solid(FT_TILE_DOOR), "doors are walkable");
    CHECK(!ft_tile_solid(FT_TILE_TERM), "terminals are walkable");

    CHECK(ft_tile_interactive(FT_TILE_DOOR), "doors do something");
    CHECK(ft_tile_interactive(FT_TILE_TERM), "terminals do something");
    CHECK(!ft_tile_interactive(FT_TILE_FLOOR), "plain floor does not");

    /* Only the avatar's feet collide, so a head may overlap scenery above. */
    const FtPos clear = {8, 8};
    CHECK(!ft_map_blocked(&m, clear), "open floor is not blocked");

    const FtPos in_wall = {0, 0};
    CHECK(ft_map_blocked(&m, in_wall), "the wall row blocks");

    /* Walking into a wall stops that axis but not the other: a diagonal into
     * a wall should slide along it rather than sticking. */
    const FtPos from = {8, 8};
    const FtPos slid = ft_map_move(&m, from, -8, 0);
    CHECK_EQ(slid.x, from.x); /* left is a wall */

    const FtPos diag = ft_map_move(&m, from, -8, 4);
    CHECK_EQ(diag.x, from.x);      /* blocked horizontally */
    CHECK_EQ(diag.y, from.y + 4);  /* but still slid down */

    /* Free movement actually moves. */
    const FtPos moved = ft_map_move(&m, from, 0, 4);
    CHECK_EQ(moved.y, from.y + 4);

    /* Camera never shows outside the map, however far the focus runs. */
    static const uint8_t BIG[40 * 20] = {0};
    const FtMap big = {BIG, 40, 20, "Big", 0};
    const int32_t max_x = 40 * FT_TILE_PX - FT_VIEW_W * FT_TILE_PX;
    const int32_t max_y = 20 * FT_TILE_PX - FT_VIEW_H * FT_TILE_PX;

    const FtPos far_pos = {9999, 9999};
    const FtPos c1 = ft_map_camera(&big, far_pos);
    CHECK_EQ(c1.x, max_x);
    CHECK_EQ(c1.y, max_y);

    const FtPos neg = {-9999, -9999};
    const FtPos c2 = ft_map_camera(&big, neg);
    CHECK_EQ(c2.x, 0);
    CHECK_EQ(c2.y, 0);

    /* Mid-map, the focus is centred. */
    const FtPos mid = {160, 80};
    const FtPos c3 = ft_map_camera(&big, mid);
    CHECK_EQ(c3.x, 160 + FT_AVATAR_W / 2 - (FT_VIEW_W * FT_TILE_PX) / 2);
    CHECK_EQ(c3.y, 80 + FT_AVATAR_H / 2 - (FT_VIEW_H * FT_TILE_PX) / 2);

    /* A map smaller than the viewport pins to the origin instead of going
     * negative and revealing a band of off-map wall. */
    const FtPos c4 = ft_map_camera(&m, mid);
    CHECK_EQ(c4.x, 0);
    CHECK_EQ(c4.y, 0);
}

static void test_tile_orientation(void) {
    section("tile orientation");

    /* Column 2 is a vertical wall with a door in it at (2,2); row 4 is a
     * horizontal wall with a door in it at (1,4).
     *
     *   # # # # #
     *   # . # . #
     *   # . D . #     <- passage runs left-right: side-on
     *   # . # . #
     *   # D # # #     <- passage runs up-down: front-on
     *   # . # . #
     */
    static const uint8_t TILES[5 * 6] = {
        1, 1, 1, 1, 1,
        1, 0, 1, 0, 1,
        1, 0, 5, 0, 1,
        1, 0, 1, 0, 1,
        1, 5, 1, 1, 1,
        1, 0, 1, 0, 1,
    };
    const FtMap m = {TILES, 5, 6, "Orient", 0};

    /* Wall above and below means you pass through sideways. */
    CHECK(ft_map_side_passage(&m, 2, 2), "a door in a vertical wall is side-on");
    CHECK_EQ(ft_map_art_index(&m, 2, 2), FT_TILE_ART_DOOR_SIDE);

    /* Wall left and right means you pass through vertically: front-facing. */
    CHECK(!ft_map_side_passage(&m, 1, 4), "a door in a horizontal wall faces you");
    CHECK_EQ(ft_map_art_index(&m, 1, 4), FT_TILE_DOOR);

    /* Ordinary tiles are unaffected. */
    CHECK_EQ(ft_map_art_index(&m, 1, 1), FT_TILE_FLOOR);

    /* (0,0) is a wall with more wall beneath it, so it caps rather than
     * showing a face — see the wall-depth checks below. */
    CHECK_EQ(ft_map_art_index(&m, 0, 0), FT_TILE_ART_WALL_TOP);

    /* Locked ports orient the same way, since they are doors that are shut. */
    static const uint8_t LOCKED[5 * 3] = {
        1, 1, 1, 1, 1,
        1, 0, 7, 0, 1,
        1, 1, 1, 1, 1,
    };
    const FtMap lk = {LOCKED, 5, 3, "Lock", 0};
    /* Walls above and below, floor either side: side-on. */
    CHECK_EQ(ft_map_art_index(&lk, 2, 1), FT_TILE_ART_LOCK_SIDE);

    static const uint8_t LOCKED_H[3 * 3] = {
        1, 0, 1,
        1, 7, 1,
        1, 0, 1,
    };
    const FtMap lh = {LOCKED_H, 3, 3, "LockH", 0};
    /* Walls either side, floor above and below: front-on. */
    CHECK_EQ(ft_map_art_index(&lh, 1, 1), FT_TILE_LOCK);

    /* Conduit follows its own run rather than the walls. */
    static const uint8_t CABLES[4 * 4] = {
        0, 4, 0, 0,
        0, 4, 0, 0,
        0, 4, 0, 0,
        0, 0, 4, 4,
    };
    const FtMap cb = {CABLES, 4, 4, "Cable", 0};
    CHECK_EQ(ft_map_art_index(&cb, 1, 1), FT_TILE_ART_CABLE_V);
    CHECK_EQ(ft_map_art_index(&cb, 2, 3), FT_TILE_CABLE);

    /* A lone conduit tile stays horizontal rather than picking at random. */
    static const uint8_t LONE[3 * 3] = {
        0, 0, 0,
        0, 4, 0,
        0, 0, 0,
    };
    const FtMap ln = {LONE, 3, 3, "Lone", 0};
    CHECK_EQ(ft_map_art_index(&ln, 1, 1), FT_TILE_CABLE);

    /* Walls show a face where floor lies below and a cap where the wall
     * carries on, which is what gives a run its apparent height.
     *
     *   # # #
     *   # # #   <- (1,1) has wall below: cap
     *   # . #   <- (1,2) has floor below: face
     */
    static const uint8_t WALLS[3 * 4] = {
        1, 1, 1,
        1, 1, 1,
        1, 1, 1,
        1, 0, 1,
    };
    const FtMap wl = {WALLS, 3, 4, "Walls", 0};

    CHECK_EQ(ft_map_art_index(&wl, 1, 1), FT_TILE_ART_WALL_TOP);
    CHECK_EQ(ft_map_art_index(&wl, 1, 2), FT_TILE_WALL);

    /* A wall on the bottom row caps, because off-map reads as wall and the
     * player never sees past the edge. */
    CHECK_EQ(ft_map_art_index(&wl, 0, 3), FT_TILE_ART_WALL_TOP);

    /* Shadow falls on the walkable tile under a wall, and nowhere else. */
    CHECK(ft_map_has_shadow(&wl, 1, 3), "floor under a wall is shadowed");
    CHECK(!ft_map_has_shadow(&wl, 1, 1), "a wall does not shadow itself");

    static const uint8_t OPEN[3 * 3] = {
        0, 0, 0,
        0, 0, 0,
        0, 0, 0,
    };
    const FtMap op = {OPEN, 3, 3, "Open", 0};
    CHECK(!ft_map_has_shadow(&op, 1, 1), "open floor casts no shadow");

    /* The top row has off-map wall above it, so it is shadowed too — the
     * world's edge behaves like any other wall. */
    CHECK(ft_map_has_shadow(&op, 1, 0), "the map edge shadows like a wall");

    /* Scattered greenery: procedural, so it must be deterministic and must
     * only ever land on bare floor. */
    static const uint8_t GROUND[4 * 4] = {
        0, 0, 8, 0,
        0, 1, 0, 0,
        0, 0, 0, 3,
        0, 5, 0, 0,
    };
    FtMap gr = {GROUND, 4, 4, "Ground", 0};

    /* Off by default: indoors grows nothing. */
    for(int32_t y = 0; y < 4; y++) {
        for(int32_t x = 0; x < 4; x++) {
            CHECK(!ft_map_scatter(&gr, x, y), "scatter 0 grows nothing");
        }
    }

    /* Turned all the way up, it still refuses anything that is not floor:
     * weeds through a crate or a doorway read as a bug, not as nature. */
    gr.scatter = 255;
    CHECK(!ft_map_scatter(&gr, 2, 0), "not on a crate");
    CHECK(!ft_map_scatter(&gr, 1, 1), "not on a wall");
    CHECK(!ft_map_scatter(&gr, 3, 2), "not on existing grass");
    CHECK(!ft_map_scatter(&gr, 1, 3), "not in a doorway");
    CHECK(ft_map_scatter(&gr, 0, 0), "but yes on bare floor");

    /* Deterministic: the camera scrolling must not reseed the world. */
    gr.scatter = 128;
    for(int32_t y = 0; y < 4; y++) {
        for(int32_t x = 0; x < 4; x++) {
            const bool first = ft_map_scatter(&gr, x, y);
            for(int i = 0; i < 8; i++) {
                CHECK_EQ(ft_map_scatter(&gr, x, y), first);
            }
        }
    }

    /* And the density roughly tracks the setting, rather than clustering into
     * a hedge or vanishing. Sampled over open ground. */
    static const uint8_t BARE[64 * 64] = {0};
    FtMap field = {BARE, 64, 64, "Field", 26};
    int grown = 0;
    for(int32_t y = 0; y < 64; y++) {
        for(int32_t x = 0; x < 64; x++) {
            if(ft_map_scatter(&field, x, y)) grown++;
        }
    }
    const int expected = (64 * 64 * 26) / 256;
    CHECK(grown > expected / 2 && grown < expected * 2,
          "scatter density %d should be near %d", grown, expected);

    /* Every index the renderer can be handed must be a real art entry. */
    for(int32_t y = 0; y < 6; y++) {
        for(int32_t x = 0; x < 5; x++) {
            CHECK(ft_map_art_index(&m, x, y) < FT_TILE_ART_COUNT,
                  "art index out of range at %d,%d", x, y);
        }
    }
}


static void test_world(void) {
    section("overworld world state");

    FtWorld w;
    ft_world_init(&w);

    CHECK_EQ(w.room, 0);
    CHECK(ft_world_map(&w) != NULL, "the first room has a map");
    CHECK(w.stats.charge > 0, "the player starts with charge");

    /* Movement is grid-based: a press starts a step to the next tile, and
     * that step runs to completion whatever the input does meanwhile. */
    const uint8_t start_tx = w.mv.tx;
    ft_world_update(&w, 1, 0, 20);
    CHECK_EQ(w.facing, FT_FACE_RIGHT);
    CHECK(ft_world_moving(&w), "a step is under way");
    CHECK_EQ(w.mv.tx, start_tx); /* not there yet */

    /* Releasing mid-step does not abandon it — that is what keeps the player
     * tile-aligned and doorways easy to hit. */
    ft_world_update(&w, 0, 0, FT_STEP_MS);
    CHECK(!ft_world_moving(&w), "the step finished");
    CHECK_EQ(w.mv.tx, start_tx + 1);
    CHECK(w.arrived, "arrival is reported for one update");

    ft_world_update(&w, 0, 0, 10);
    CHECK(!w.arrived, "and only for one");
    CHECK_EQ(w.facing, FT_FACE_RIGHT); /* facing persists */

    /* One tile per FT_STEP_MS, whatever the tick size. The old pixel-based
     * walk ran at 100 px/s instead of 44 because a 10ms tick floored to zero
     * pixels and got rounded up; stepping cannot drift that way. */
    for(int tick = 1; tick <= 40; tick++) {
        FtWorld s2;
        ft_world_init(&s2);
        ft_world_enter(&s2, 1, 2, 6);

        const uint8_t x0 = s2.mv.tx;
        uint32_t elapsed = 0;
        while(elapsed < FT_STEP_MS * 4u) {
            ft_world_update(&s2, 1, 0, (uint32_t)tick);
            elapsed += (uint32_t)tick;
        }

        const int moved = (int)s2.mv.tx - (int)x0;
        CHECK(moved >= 3 && moved <= 4, "tick %dms moved %d tiles in 4 steps", tick, moved);
    }

    /* A wall refuses the step but still turns you, which is what lets you
     * strike a foe you cannot walk into. */
    ft_world_enter(&w, 0, 1, 1);
    for(int i = 0; i < 40; i++) ft_world_update(&w, -1, 0, 50);
    CHECK_EQ(w.facing, FT_FACE_LEFT);
    CHECK_EQ(w.mv.tx, 1); /* the wall held */

    /* Entity clearing is per room, so beating a foe in one room must not
     * silently remove one in another. */
    ft_world_enter(&w, 1, 2, 2);
    CHECK(!ft_world_entity_gone(&w, 0), "foes start alive");
    ft_world_clear_entity(&w, 0);
    CHECK(ft_world_entity_gone(&w, 0), "and stay cleared");

    ft_world_enter(&w, 2, 2, 2);
    CHECK(!ft_world_entity_gone(&w, 0), "a different room is unaffected");

    ft_world_enter(&w, 1, 2, 2);
    CHECK(ft_world_entity_gone(&w, 0), "and the first room remembers");
}

static int32_t abs_i32(int32_t v) { return v < 0 ? -v : v; }

static void test_foe_ai(void) {
    section("foe patrol and pursuit");

    /* Room 3 (Cold Gate) is the marker that fights as three, which is the
     * case the player reported twice: first as one sprite meaning three, then
     * as three sprites welded into one moving object. */
    const uint8_t ROOM = 3;
    const FtRoom* room = ft_room(ROOM);
    CHECK(room->ent_count >= 1, "the AI test needs a foe marker");

    FtWorld w;
    ft_world_init(&w);
    ft_world_enter(&w, ROOM, room->exits[0].tx, room->exits[0].ty);

    const FtRoster* roster = ft_roster(room->ents[0].roster);
    CHECK_EQ(w.foes[0].count, roster->count);
    CHECK(w.foes[0].count >= 2, "this marker walks as a group");

    /* Each walker is its own actor on its own tile. Three of them stacked on
     * one tile is the bug this replaced. */
    for(uint8_t m = 0; m < w.foes[0].count; m++) {
        for(uint8_t n = (uint8_t)(m + 1u); n < w.foes[0].count; n++) {
            CHECK(w.foes[0].w[m].mv.tx != w.foes[0].w[n].mv.tx ||
                      w.foes[0].w[m].mv.ty != w.foes[0].w[n].mv.ty,
                  "walkers %u and %u start on different tiles", m, n);
        }
        CHECK(!ft_tile_solid(ft_map_tile(room->map, w.foes[0].w[m].mv.tx,
                                         w.foes[0].w[m].mv.ty)),
              "walker %u starts on open ground", m);
    }

    /* Idling: they wander on their own seeds rather than in step. */
    uint8_t sx[FT_MAX_ENEMIES], sy[FT_MAX_ENEMIES];
    for(uint8_t m = 0; m < w.foes[0].count; m++) {
        sx[m] = w.foes[0].w[m].mv.tx;
        sy[m] = w.foes[0].w[m].mv.ty;
    }

    for(int t = 0; t < 400; t++) ft_world_update(&w, 0, 0, 20);

    int moved = 0, in_lockstep = 1;
    int32_t d0x = 0, d0y = 0;
    for(uint8_t m = 0; m < w.foes[0].count; m++) {
        const int32_t dx = (int32_t)w.foes[0].w[m].mv.tx - (int32_t)sx[m];
        const int32_t dy = (int32_t)w.foes[0].w[m].mv.ty - (int32_t)sy[m];
        if(dx || dy) moved++;
        if(m == 0) { d0x = dx; d0y = dy; }
        else if(dx != d0x || dy != d0y) in_lockstep = 0;
    }
    CHECK(moved > 0, "an idle group is not frozen");
    CHECK(!in_lockstep, "walkers drift independently, not as one block");

    /* And they never converge onto one tile. Three wanderers with nothing
     * keeping them apart end up stacked, which is the welded look again. */
    for(int t = 0; t < 1200; t++) {
        ft_world_update(&w, 0, 0, 20);

        for(uint8_t m = 0; m < w.foes[0].count; m++) {
            for(uint8_t n = (uint8_t)(m + 1u); n < w.foes[0].count; n++) {
                CHECK(w.foes[0].w[m].mv.tx != w.foes[0].w[n].mv.tx ||
                          w.foes[0].w[m].mv.ty != w.foes[0].w[n].mv.ty,
                      "walkers %u and %u never stack", m, n);
            }
        }
    }

    /* The leash, per walker: however long they wander, none abandons its
     * post, so the group stays a group without being welded together. */
    for(int t = 0; t < 2000; t++) {
        ft_world_update(&w, 0, 0, 20);
        if(w.foes[0].alert) continue; /* only the idle rule leashes */

        for(uint8_t m = 0; m < w.foes[0].count; m++) {
            const FtFoeWalker* k = &w.foes[0].w[m];
            const int32_t hx = (int32_t)k->mv.tx - (int32_t)k->home_tx;
            const int32_t hy = (int32_t)k->mv.ty - (int32_t)k->home_ty;
            /* One step of overshoot past the leash is the turnaround itself. */
            CHECK(abs_i32(hx) + abs_i32(hy) <= FT_FOE_LEASH + 1,
                  "walker %u stays in its region", m);
        }
    }

    /* Aggro is shared across the room, walkers included. */
    FtWorld c;
    ft_world_init(&c);
    ft_world_enter(&c, ROOM, room->ents[0].tx, (uint8_t)(room->ents[0].ty + 1u));
    ft_world_update(&c, 0, 0, 1);
    CHECK(c.foes[0].alert, "walking into the group alerts it");

    /* An alerted group closes the distance instead of milling about, and
     * every walker comes, not just the one that saw you. */
    for(uint8_t m = 0; m < c.foes[0].count; m++) {
        FtWorld chase;
        ft_world_init(&chase);
        ft_world_enter(&chase, ROOM, room->exits[0].tx, room->exits[0].ty);
        chase.foes[0].alert = true;

        const int32_t before =
            abs_i32((int32_t)chase.foes[0].w[m].mv.tx - (int32_t)chase.mv.tx) +
            abs_i32((int32_t)chase.foes[0].w[m].mv.ty - (int32_t)chase.mv.ty);

        for(int t = 0; t < 80; t++) ft_world_update(&chase, 0, 0, 20);

        const int32_t after =
            abs_i32((int32_t)chase.foes[0].w[m].mv.tx - (int32_t)chase.mv.tx) +
            abs_i32((int32_t)chase.foes[0].w[m].mv.ty - (int32_t)chase.mv.ty);

        CHECK(after < before, "walker %u closes in (%d -> %d)",
              m, (int)before, (int)after);
    }

    /* Touching any walker starts the marker's fight — the group is one
     * encounter however spread out it happens to be standing. */
    for(uint8_t m = 0; m < c.foes[0].count; m++) {
        FtWorld touch;
        ft_world_init(&touch);
        ft_world_enter(&touch, ROOM, room->exits[0].tx, room->exits[0].ty);

        touch.mv.tx = touch.foes[0].w[m].mv.tx;
        touch.mv.ty = touch.foes[0].w[m].mv.ty;
        CHECK_EQ(ft_world_foe_contact(&touch), 0);
    }

    /* Beating the marker removes the whole group, not just the one touched. */
    FtWorld done;
    ft_world_init(&done);
    ft_world_enter(&done, ROOM, room->exits[0].tx, room->exits[0].ty);
    ft_world_clear_entity(&done, 0);
    CHECK(!done.foes[0].alive, "clearing the marker clears the group");
    CHECK_EQ(ft_world_foe_contact(&done), -1);

    /* Chasing is faster than patrolling: the walker steps every think tick
     * instead of idling through most of them. */
    CHECK(FT_FOE_STEP_MS <= FT_STEP_MS + 40,
          "a chase can nearly keep pace with the player");
}

static void test_hit_fx(void) {
    section("hit flinch");

    FtLoadout lo;
    ft_loadout_init(&lo);

    FtEncounter e;
    ft_encounter_init_single(&e, FT_ENEMY_STRAY_PACKET, &lo, 4);

    /* Nothing outside an impact that actually hurt. */
    CHECK_EQ(ft_encounter_hit_fx(&e).stage, FT_HIT_FX_NONE);

    e.phase = FT_PHASE_IMPACT;
    e.last_enemy_hit.damage = 0;
    e.phase_ms = 1000;
    CHECK_EQ(ft_encounter_hit_fx(&e).stage, FT_HIT_FX_NONE);

    /* A landed hit gets a strobe and nothing else. The iris that used to run
     * here took the fight off the screen for a second every time a foe
     * connected, three times in a three-foe round. */
    e.last_enemy_hit.damage = 3;

    const uint32_t start = ((uint32_t)FT_ANIM_MS * FT_ANIM_STRIKE) / 255u;
    e.phase_ms = start - 1;
    CHECK_EQ(ft_encounter_hit_fx(&e).stage, FT_HIT_FX_NONE);

    int on = 0, off = 0;
    for(uint32_t t = start; t < start + FT_FLICKER_MS; t += 5) {
        e.phase_ms = t;
        const FtHitFx fx = ft_encounter_hit_fx(&e);

        CHECK_EQ(fx.stage, FT_HIT_FX_FLICKER);
        if(fx.strobe) on++;
        else off++;
    }
    CHECK(on > 0 && off > 0, "the flinch alternates (%d on, %d off)", on, off);

    /* And it is over well before the impact hold is, so the fight is visible
     * again long before the turn moves on. */
    e.phase_ms = start + FT_FLICKER_MS;
    CHECK_EQ(ft_encounter_hit_fx(&e).stage, FT_HIT_FX_NONE);
    CHECK(start + FT_FLICKER_MS < ft_encounter_impact_hold(&e),
          "the flinch fits inside the hold");

    /* A landed hit no longer buys extra hold time, because there is no
     * transition left to cover. */
    CHECK_EQ(ft_encounter_impact_hold(&e), FT_IMPACT_HOLD_MS);
    e.last_enemy_hit.damage = 0;
    CHECK_EQ(ft_encounter_impact_hold(&e), FT_IMPACT_HOLD_MS);
}

static void test_reach(void) {
    section("reach and retargeting");

    FtLoadout lo;
    ft_loadout_init(&lo);

    /* Nothing an enemy *is* can take a module off the list. Every attack is
     * selectable against every board; attributes only move the caret. */
    const FtEnemyId air[FT_MAX_ENEMIES] = {
        FT_ENEMY_DRIFT_BEACON, FT_ENEMY_DRIFT_BEACON, FT_ENEMY_DRIFT_BEACON};
    FtEncounter flyers;
    ft_encounter_init(&flyers, air, 3, &lo, 3);

    CHECK(ft_encounter_action_available(&flyers, FT_ACTION_CONTACT),
          "contact stays selectable against a sky full of flyers");
    CHECK(ft_encounter_action_available(&flyers, FT_ACTION_BROADCAST),
          "so does broadcast");

    FtEncounter locks;
    ft_encounter_init_single(&locks, FT_ENEMY_SEALED_LOCK, &lo, 3);
    CHECK(ft_encounter_action_available(&locks, FT_ACTION_BROADCAST),
          "broadcast stays selectable against encrypted");

    /* Reach is still real: it decides who gets hit. */
    CHECK(!ft_encounter_can_reach(&flyers, FT_ACTION_CONTACT, 0), "contact misses air");
    CHECK(ft_encounter_can_reach(&flyers, FT_ACTION_BROADCAST, 0), "broadcast reaches air");
    CHECK(!ft_encounter_can_reach(&locks, FT_ACTION_BROADCAST, 0), "broadcast bounces off ENC");
    CHECK(ft_encounter_can_reach(&locks, FT_ACTION_CONTACT, 0), "contact opens ENC");

    /* The headline fix: a grounded attack walks past the flyers to whoever is
     * standing there, instead of refusing to happen. There is no cursor to
     * set — targeting is automatic, nearest reachable first. */
    const FtEnemyId mixed[FT_MAX_ENEMIES] = {
        FT_ENEMY_DRIFT_BEACON, FT_ENEMY_DRIFT_BEACON, FT_ENEMY_STRAY_PACKET};
    FtEncounter e;
    ft_encounter_init(&e, mixed, 3, &lo, 3);

    CHECK_EQ(ft_encounter_effective_target(&e, FT_ACTION_CONTACT), 2);
    CHECK_EQ(ft_encounter_effective_target(&e, FT_ACTION_BROADCAST), 0);

    /* Nearest first when several are reachable. */
    const FtEnemyId wrap[FT_MAX_ENEMIES] = {
        FT_ENEMY_STRAY_PACKET, FT_ENEMY_DRIFT_BEACON, FT_ENEMY_STRAY_PACKET};
    FtEncounter wr;
    ft_encounter_init(&wr, wrap, 3, &lo, 3);
    CHECK_EQ(ft_encounter_effective_target(&wr, FT_ACTION_CONTACT), 0);

    /* A dead foe is skipped, so killing the front one moves the aim along
     * without the player doing anything. */
    wr.foes[0].charge = 0;
    CHECK(!ft_encounter_can_reach(&wr, FT_ACTION_CONTACT, 0), "a corpse is not a target");
    CHECK_EQ(ft_encounter_effective_target(&wr, FT_ACTION_CONTACT), 2);

    /* Nothing reachable at all: the swing lands on the nearest living foe and
     * is refused there, rather than indexing off the end of the row. */
    const uint8_t t = ft_encounter_effective_target(&flyers, FT_ACTION_CONTACT);
    CHECK(t < flyers.foe_count, "a whiff still names a real foe");
    CHECK(ft_encounter_foe_alive(&flyers, t), "and a living one");

    /* And it resolves that way end to end: swinging with contact against two
     * flyers and a grounded foe damages the grounded one. */
    FtEncounter fight;
    ft_encounter_init(&fight, mixed, 3, &lo, 3);
    fight.menu_index = FT_ACTION_CONTACT;

    const int16_t before = fight.foes[2].charge;
    ft_encounter_press_ok(&fight);
    ft_encounter_tick(&fight, FT_READY_MS + FT_ACTION_WINDOW_MS / 2);
    ft_encounter_press_ok(&fight);
    ft_encounter_tick(&fight, FT_ACTION_WINDOW_MS);

    CHECK_EQ(fight.phase, FT_PHASE_RESULT);
    CHECK(fight.foes[2].charge < before, "the grounded foe took the hit");
    CHECK_EQ(fight.foes[0].charge, FT_ENEMIES[FT_ENEMY_DRIFT_BEACON].charge);
}

static void test_practice(void) {
    section("practice arena");

    FtPractice p;
    ft_practice_init(&p, 12345u);

    CHECK_EQ(p.row, FT_PRACTICE_FOES);
    CHECK_EQ(p.group, FT_FOES_RANDOM);
    CHECK_EQ(p.level, 1);
    CHECK_EQ(p.kit, FT_KIT_BASIC);

    /* The cursor wraps both ways and never leaves the list. */
    ft_practice_move(&p, -1);
    CHECK_EQ(p.row, FT_PRACTICE_ROWS - 1);
    ft_practice_move(&p, 1);
    CHECK_EQ(p.row, FT_PRACTICE_FOES);

    for(int i = 0; i < 40; i++) {
        ft_practice_move(&p, (i % 3) ? 1 : -1);
        CHECK(p.row < FT_PRACTICE_ROWS, "the cursor stays on a row");
    }

    /* Every row's label and value fit the panel and are never empty where a
     * value is meant to be. */
    p.row = FT_PRACTICE_FOES;
    for(uint8_t r = 0; r < FT_PRACTICE_ROWS; r++) {
        CHECK(strlen(ft_practice_row_name(r)) <= FT_TUTORIAL_MAX_CHARS,
              "row %u has a short name", r);

        for(int i = 0; i < FT_PRACTICE_GROUPS + FT_KIT_COUNT + 12; i++) {
            p.row = r;
            ft_practice_adjust(&p, 1);

            const char* v = ft_practice_value(&p, r);
            CHECK(v != NULL, "row %u always has a value", r);
            CHECK(strlen(v) <= FT_TUTORIAL_MAX_CHARS, "row %u value fits: \"%s\"", r, v);

            const char* h = ft_practice_help(&p);
            CHECK(h && strlen(h) <= FT_TUTORIAL_MAX_CHARS, "row %u help fits", r);
        }
    }

    /* Settings wrap rather than sticking at the end. */
    p.row = FT_PRACTICE_LEVEL;
    p.level = 1;
    ft_practice_adjust(&p, -1);
    CHECK_EQ(p.level, FT_PRACTICE_MAX_LEVEL);
    ft_practice_adjust(&p, 1);
    CHECK_EQ(p.level, 1);

    p.row = FT_PRACTICE_KIT;
    p.kit = 0;
    ft_practice_adjust(&p, -1);
    CHECK_EQ(p.kit, FT_KIT_COUNT - 1);

    p.row = FT_PRACTICE_GROUPS ? FT_PRACTICE_FOES : FT_PRACTICE_FOES;
    p.group = 0;
    ft_practice_adjust(&p, -1);
    CHECK_EQ(p.group, FT_PRACTICE_GROUPS - 1);

    /* FIGHT is a button: it has no value to cycle and adjusting it is inert. */
    p.row = FT_PRACTICE_FIGHT;
    const FtPractice before = p;
    ft_practice_adjust(&p, 1);
    CHECK_EQ(p.group, before.group);
    CHECK_EQ(p.level, before.level);
    CHECK_EQ(p.kit, before.kit);

    /* Every fixed line-up builds a legal encounter. */
    for(uint8_t g = 1; g < FT_PRACTICE_GROUPS; g++) {
        FtPractice s2;
        ft_practice_init(&s2, 99u);
        s2.group = g;

        FtEncounter e;
        ft_practice_start(&s2, &e);

        CHECK(e.foe_count >= 1 && e.foe_count <= FT_MAX_ENEMIES,
              "group %u fields 1..3 foes (got %u)", g, e.foe_count);
        CHECK_EQ(ft_encounter_living(&e), e.foe_count);
        CHECK_EQ(e.phase, FT_PHASE_MENU);
        CHECK(!e.coach, "the arena does not nag");
        CHECK(e.roll.current == e.stats.charge_max, "you start full");
    }

    /* Random gives different fights on repeated presses rather than the same
     * one over and over — that is the whole point of the Random setting. */
    FtPractice r;
    ft_practice_init(&r, 7u);
    r.group = FT_FOES_RANDOM;

    uint32_t shapes = 0;
    uint8_t first_count = 0;
    bool varied = false;

    for(int i = 0; i < 24; i++) {
        FtEncounter e;
        ft_practice_start(&r, &e);

        CHECK(e.foe_count >= 1 && e.foe_count <= FT_MAX_ENEMIES,
              "a rolled group fields 1..3 foes");
        for(uint8_t k = 0; k < e.foe_count; k++) {
            CHECK(e.foes[k].id < FT_ENEMY_COUNT, "a rolled foe is a real enemy");
            CHECK(e.foes[k].charge > 0, "a rolled foe starts alive");
        }

        if(i == 0) first_count = e.foe_count;
        else if(e.foe_count != first_count) varied = true;

        shapes |= 1u << e.foe_count;
    }
    CHECK(varied, "Random does not serve the same size every time");
    CHECK(shapes != 0, "Random produced something");

    /* Levels raise the ceiling, and the kits actually hand you more. */
    FtPractice lo1;
    ft_practice_init(&lo1, 4u);
    lo1.group = 1;
    lo1.level = 1;

    FtEncounter low;
    ft_practice_start(&lo1, &low);

    FtPractice hi;
    ft_practice_init(&hi, 4u);
    hi.group = 1;
    hi.level = FT_PRACTICE_MAX_LEVEL;

    FtEncounter high;
    ft_practice_start(&hi, &high);

    CHECK(high.stats.charge_max > low.stats.charge_max,
          "level 10 has more Charge than level 1 (%d vs %d)",
          (int)high.stats.charge_max, (int)low.stats.charge_max);
    CHECK(high.stats.ram_max >= low.stats.ram_max, "and no less RAM");

    /* A kit you cannot use is not a kit: the loaded sets can replay at once. */
    for(uint8_t k = 0; k < FT_KIT_COUNT; k++) {
        FtPractice kp;
        ft_practice_init(&kp, 4u);
        kp.group = 1;
        kp.kit = k;

        FtEncounter ke;
        ft_practice_start(&kp, &ke);

        if(k == FT_KIT_BASIC) {
            CHECK(!ft_encounter_action_available(&ke, FT_ACTION_SIGNAL),
                  "the basic kit starts with nothing captured");
        } else {
            CHECK(ft_encounter_action_available(&ke, FT_ACTION_SIGNAL),
                  "kit %u can replay from the first turn", k);
        }

        /* Hard Mode is never switched on behind your back. */
        CHECK(!ke.fx.hard_mode, "kit %u leaves Hard Mode alone", k);

        /* And the installed kit is legal: never over the Flash budget. */
        CHECK(ke.stats.flash_used <= ke.stats.flash_max ||
                  k != FT_KIT_BASIC,
              "kit %u fits the budget", k);
    }

    FtLoadout basic, loaded, maxed;
    ft_practice_loadout(FT_KIT_BASIC, &basic);
    ft_practice_loadout(FT_KIT_LOADED, &loaded);
    ft_practice_loadout(FT_KIT_MAX, &maxed);

    int nb = 0, nl = 0, nm = 0;
    for(uint8_t i = 0; i < FT_MODULE_COUNT; i++) {
        nb += basic.stacks[i];
        nl += loaded.stacks[i];
        nm += maxed.stacks[i];
    }
    CHECK(nl > nb, "Loaded installs more than Basic (%d vs %d)", nl, nb);
    CHECK(nm >= nl, "Max installs at least as much as Loaded (%d vs %d)", nm, nl);

    /* A practice match is playable end to end and terminates. */
    FtPractice play;
    ft_practice_init(&play, 21u);
    play.group = FT_PRACTICE_GROUPS - 1; /* the trio */
    play.level = 6;
    play.kit = FT_KIT_LOADED;

    FtEncounter run;
    ft_practice_start(&play, &run);

    for(int i = 0; i < 40000 && !ft_encounter_over(&run); i++) {
        if(run.phase == FT_PHASE_MENU) {
            run.menu_index = FT_ACTION_CONTACT;
            ft_encounter_press_ok(&run);
        }
        ft_encounter_tick(&run, 10);
    }
    CHECK(ft_encounter_over(&run), "an arena match terminates");
}

static void test_defeat(void) {
    section("foes are seen to die");

    FtLoadout lo;
    ft_loadout_init(&lo);

    const FtEnemyId three[FT_MAX_ENEMIES] = {
        FT_ENEMY_STRAY_PACKET, FT_ENEMY_STRAY_PACKET, FT_ENEMY_STRAY_PACKET};

    FtEncounter e;
    ft_encounter_init(&e, three, 3, &lo, 5);

    /* Standing foes are not falling over. */
    for(uint8_t i = 0; i < 3; i++) {
        CHECK_EQ(ft_encounter_foe_defeat(&e, i), 0);
        CHECK(ft_encounter_foe_visible(&e, i), "a living foe is on screen");
    }

    /* Kill the row with a broadcast, mid-animation. */
    e.phase = FT_PHASE_RESULT;
    e.menu_index = FT_ACTION_BROADCAST;
    for(uint8_t i = 0; i < 3; i++) {
        e.foe_charge_before[i] = e.foes[i].charge_max;
        e.foes[i].charge = 0;
        e.foe_hit_valid[i] = true;
        e.foe_hits[i].damage = e.foes[i].charge_max;
    }

    /* Before the wave reaches a foe it is untouched and still standing. */
    e.phase_ms = 0;
    for(uint8_t i = 0; i < 3; i++) {
        CHECK_EQ(ft_encounter_foe_defeat(&e, i), 0);
        CHECK(ft_encounter_foe_visible(&e, i), "foe %u waits for the wave", i);
    }

    /* Through the animation each one starts falling only once it is struck,
     * never runs backwards, and is gone by the end. */
    uint8_t prev[FT_MAX_ENEMIES] = {0, 0, 0};
    bool started[FT_MAX_ENEMIES] = {false, false, false};

    for(uint32_t ms = 0; ms <= FT_ANIM_MS; ms += 10) {
        e.phase_ms = ms;
        const uint8_t now = ft_encounter_anim_progress(&e);

        for(uint8_t i = 0; i < 3; i++) {
            const uint8_t d = ft_encounter_foe_defeat(&e, i);
            const uint8_t at = ft_encounter_foe_hit_at(&e, i);

            if(now <= at) {
                CHECK_EQ(d, 0);
                CHECK(ft_encounter_foe_visible(&e, i), "foe %u stands until hit", i);
            } else {
                if(d > 0u) started[i] = true;
                CHECK(d >= prev[i], "foe %u never un-dies (%u -> %u)", i, prev[i], d);
            }
            prev[i] = d;
        }
    }

    for(uint8_t i = 0; i < 3; i++) {
        CHECK(started[i], "foe %u was seen to fall", i);
        CHECK_EQ(prev[i], 255);
        CHECK(!ft_encounter_foe_visible(&e, i), "and is gone by the end");
    }

    /* The fold is not instant: it occupies real frames rather than one. The
     * whole complaint about the old behaviour was that a foe blinked out
     * between two frames the moment its bar emptied. */
    int frames = 0;
    for(uint32_t ms = 0; ms <= FT_ANIM_MS; ms += 33) { /* one frame at 30fps */
        e.phase_ms = ms;
        const uint8_t d = ft_encounter_foe_defeat(&e, 2);
        if(d > 0u && d < 255u) frames++;
    }
    CHECK(frames >= 2, "the fall lasts more than one frame (%d)", frames);

    /* A foe that was already down before this turn does not fall again. */
    FtEncounter old;
    ft_encounter_init(&old, three, 3, &lo, 5);
    old.phase = FT_PHASE_RESULT;
    old.phase_ms = FT_ANIM_MS / 2;
    old.foes[1].charge = 0;
    old.foe_charge_before[1] = 0;
    CHECK_EQ(ft_encounter_foe_defeat(&old, 1), 0);
    CHECK(!ft_encounter_foe_visible(&old, 1), "yesterday's corpse is not drawn");

    /* And nothing falls over during the menu. */
    old.phase = FT_PHASE_MENU;
    CHECK_EQ(ft_encounter_foe_defeat(&old, 1), 0);
}

static void fill_save(FtSaveData* d) {
    /* Deliberately not defaults: a round trip that only ever sees zeroes
     * proves nothing about whether a field is written at all. */
    d->stats.charge = 17;
    d->stats.charge_max = 34;
    d->stats.ram = 3;
    d->stats.ram_max = 11;
    d->stats.flash_used = 5;
    d->stats.flash_max = 9;
    d->stats.level = 6;
    d->stats.xp = 73;

    for(uint8_t i = 0; i < FT_MODULE_COUNT; i++) d->loadout.stacks[i] = (uint8_t)(i + 1u);

    for(uint8_t i = 0; i < FT_SIGLIB_SLOTS; i++) {
        d->lib.ids[i] = (uint16_t)(1000u + i);
    }
    d->lib.count = 3;
    d->lib.next = 2;

    d->room = 2;
    d->tx = 13;
    d->ty = 7;
    d->save_room = 1;
    d->save_tx = 4;
    d->save_ty = 5;

    for(uint8_t i = 0; i < FT_CLEARED_BYTES; i++) d->cleared[i] = (uint8_t)(0xA5u ^ i);
    d->coach = true;
}

static bool save_eq(const FtSaveData* a, const FtSaveData* b) {
    if(a->stats.charge != b->stats.charge) return false;
    if(a->stats.charge_max != b->stats.charge_max) return false;
    if(a->stats.ram != b->stats.ram) return false;
    if(a->stats.ram_max != b->stats.ram_max) return false;
    if(a->stats.flash_used != b->stats.flash_used) return false;
    if(a->stats.flash_max != b->stats.flash_max) return false;
    if(a->stats.level != b->stats.level) return false;
    if(a->stats.xp != b->stats.xp) return false;

    for(uint8_t i = 0; i < FT_MODULE_COUNT; i++) {
        if(a->loadout.stacks[i] != b->loadout.stacks[i]) return false;
    }
    for(uint8_t i = 0; i < FT_SIGLIB_SLOTS; i++) {
        if(a->lib.ids[i] != b->lib.ids[i]) return false;
    }
    if(a->lib.count != b->lib.count || a->lib.next != b->lib.next) return false;

    if(a->room != b->room || a->tx != b->tx || a->ty != b->ty) return false;
    if(a->save_room != b->save_room) return false;
    if(a->save_tx != b->save_tx || a->save_ty != b->save_ty) return false;

    for(uint8_t i = 0; i < FT_CLEARED_BYTES; i++) {
        if(a->cleared[i] != b->cleared[i]) return false;
    }
    return a->coach == b->coach;
}

static void test_save(void) {
    section("save format");

    FtSaveData src;
    fill_save(&src);

    uint8_t buf[FT_SAVE_MAX_BYTES];
    const uint8_t len = ft_save_encode(&src, buf, sizeof(buf));

    CHECK(len > 0, "a save encodes");
    CHECK(len <= FT_SAVE_MAX_BYTES, "and fits the declared budget (%u)", len);

    /* Every field survives the trip. A save that loses one field is worse
     * than no save at all, because it looks like it worked. */
    FtSaveData back;
    CHECK(ft_save_decode(buf, len, &back), "and decodes");
    CHECK(save_eq(&src, &back), "every field round trips");

    /* A buffer too small refuses rather than writing part of a file. */
    uint8_t tiny[4];
    CHECK_EQ(ft_save_encode(&src, tiny, sizeof(tiny)), 0);

    /* Someone else's file, or a version we do not know, is refused. */
    uint8_t alien[FT_SAVE_MAX_BYTES];
    for(uint8_t i = 0; i < len; i++) alien[i] = buf[i];
    alien[0] = 'X';
    CHECK(!ft_save_decode(alien, len, &back), "a foreign file is refused");

    for(uint8_t i = 0; i < len; i++) alien[i] = buf[i];
    alien[4] = FT_SAVE_VERSION + 1u;
    CHECK(!ft_save_decode(alien, len, &back), "a future version is refused");

    for(uint8_t i = 0; i < len; i++) alien[i] = buf[i];
    alien[5] = (uint8_t)(alien[5] + 1u);
    CHECK(!ft_save_decode(alien, len, &back), "a wrong declared length is refused");

    /* Truncation at every length, which is what a card pulled mid-write
     * actually looks like. */
    for(uint8_t cut = 0; cut < len; cut++) {
        CHECK(!ft_save_decode(buf, cut, &back), "a file cut to %u bytes is refused", cut);
    }

    /* Every single-bit flip in the file is caught. This is the whole reason
     * the checksum covers the header as well as the payload. */
    int missed = 0;
    for(uint8_t i = 0; i < len; i++) {
        for(uint8_t bit = 0; bit < 8u; bit++) {
            uint8_t bad[FT_SAVE_MAX_BYTES];
            for(uint8_t k = 0; k < len; k++) bad[k] = buf[k];
            bad[i] = (uint8_t)(bad[i] ^ (uint8_t)(1u << bit));

            if(ft_save_decode(bad, len, &back)) missed++;
        }
    }
    CHECK_EQ(missed, 0);

    /* Nulls are refused rather than dereferenced. */
    CHECK(!ft_save_decode(NULL, len, &back), "a null buffer is refused");
    CHECK(!ft_save_decode(buf, len, NULL), "a null destination is refused");
    CHECK_EQ(ft_save_encode(&src, NULL, sizeof(buf)), 0);

    /* Encoding is deterministic, so an unchanged run does not rewrite a
     * different file every time. */
    uint8_t again[FT_SAVE_MAX_BYTES];
    CHECK_EQ(ft_save_encode(&src, again, sizeof(again)), len);
    for(uint8_t i = 0; i < len; i++) CHECK_EQ(again[i], buf[i]);
}

static void test_save_world(void) {
    section("saving a run");

    FtWorld w;
    ft_world_init(&w);

    /* Play a little: move rooms, beat something, level up, capture a signal. */
    ft_world_enter(&w, 2, 3, 4);
    ft_world_clear_entity(&w, 0);
    ft_siglib_capture(&w.lib, 4242u);
    ft_level_apply(&w.stats, FT_UP_RAM);
    ft_loadout_add(&w.loadout, FT_MOD_AMPLIFY);

    w.save_room = 2;
    w.save_tx = 3;
    w.save_ty = 4;

    FtSaveData d;
    ft_save_from_world(&w, false, &d);

    uint8_t buf[FT_SAVE_MAX_BYTES];
    const uint8_t len = ft_save_encode(&d, buf, sizeof(buf));
    CHECK(len > 0, "the run encodes");

    FtSaveData back;
    CHECK(ft_save_decode(buf, len, &back), "and decodes");

    FtWorld loaded;
    bool coach = true;
    ft_save_to_world(&back, &loaded, &coach);

    CHECK_EQ(loaded.room, 2);
    CHECK_EQ(loaded.mv.tx, 3);
    CHECK_EQ(loaded.mv.ty, 4);
    CHECK_EQ(loaded.save_room, 2);
    CHECK_EQ(loaded.stats.ram_max, w.stats.ram_max);
    CHECK_EQ(loaded.stats.level, w.stats.level);
    CHECK(ft_siglib_holds(&loaded.lib, 4242u), "captures survive a save");
    CHECK_EQ(loaded.loadout.stacks[FT_MOD_AMPLIFY], 1);
    CHECK(!coach, "the tips setting survives too");

    /* The one that matters: a foe you already beat must not be standing
     * there again. Restoring the flags after entering the room would spawn
     * it and only then mark it dead. */
    CHECK(ft_world_entity_gone(&loaded, 0), "a beaten foe stays beaten");
    CHECK(!loaded.foes[0].alive, "and is not spawned by the load");

    /* Loading is a clean slate otherwise: no stepping half-finished, nothing
     * carried over from whatever the struct held before. */
    CHECK_EQ(loaded.mv.dx, 0);
    CHECK_EQ(loaded.mv.dy, 0);
    CHECK(!loaded.arrived, "a fresh load has not just arrived anywhere");

    /* And a saved position in another room really is another room. */
    FtSaveData other = back;
    other.room = 0;
    other.tx = 3;
    other.ty = 4;

    FtWorld elsewhere;
    ft_save_to_world(&other, &elsewhere, NULL);
    CHECK_EQ(elsewhere.room, 0);
    CHECK(ft_world_entity_gone(&elsewhere, 0) == false ||
              ft_room(0)->ent_count == 0,
          "clearing is per room, not global");
}

static void test_levelup(void) {
    section("levelling up");

    FtStats s;
    ft_stats_init(&s);
    CHECK_EQ(s.level, 1);

    /* Applying a choice is what actually levels you. Before this, level never
     * moved, so the cap never bit and every enemy paid full XP forever. */
    const int16_t charge_before = s.charge_max;
    CHECK(ft_level_apply(&s, FT_UP_CHARGE), "the level is spent");
    CHECK_EQ(s.level, 2);
    CHECK_EQ(s.charge_max, charge_before + FT_LEVEL_UP_CHARGE);

    /* A refused choice costs nothing: the level stays owed. */
    FtStats capped;
    ft_stats_init(&capped);
    capped.flash_max = FT_CAP_FLASH;
    const int16_t lv = capped.level;
    CHECK(!ft_level_apply(&capped, FT_UP_FLASH), "a capped stat refuses");
    CHECK_EQ(capped.level, lv);

    /* XP banks into levels, and stops at the cap for the chapters done. */
    FtStats p;
    ft_stats_init(&p);
    const int16_t cap = ft_level_cap(0);
    CHECK(cap > 1, "the prologue allows at least one level");

    CHECK_EQ(ft_xp_gain(&p, FT_XP_PER_LEVEL - 1, cap), 0);
    CHECK_EQ(ft_xp_gain(&p, 1, cap), 1);

    /* One battle can never be worth more than FT_XP_BATTLE_CAP, so no single
     * fight hands over two levels however overtuned it is. */
    CHECK_EQ(ft_xp_gain(&p, FT_XP_PER_LEVEL * 5, cap), 1);

    /* Spending them raises the level once each. */
    CHECK(ft_level_apply(&p, FT_UP_CHARGE), "first");
    CHECK(ft_level_apply(&p, FT_UP_RAM), "second");
    CHECK_EQ(p.level, 3);

    /* At the cap, XP stops being awarded at all rather than banking up for a
     * chapter that has not been unlocked. */
    FtStats maxed;
    ft_stats_init(&maxed);
    maxed.level = cap;
    CHECK_EQ(ft_xp_gain(&maxed, FT_XP_PER_LEVEL * 5, cap), 0);
    CHECK_EQ(maxed.xp, 0);

    /* A won fight is worth the sum of its foes, tapered individually. */
    FtLoadout lo;
    ft_loadout_init(&lo);

    const FtEnemyId trio[FT_MAX_ENEMIES] = {
        FT_ENEMY_STRAY_PACKET, FT_ENEMY_DRIFT_BEACON, FT_ENEMY_SEALED_LOCK};
    FtEncounter e;
    ft_encounter_init(&e, trio, 3, &lo, 1);

    /* Not won yet: nothing owed. */
    CHECK_EQ(ft_encounter_xp(&e), 0);

    e.phase = FT_PHASE_WIN;
    e.stats.level = 1;

    int16_t expect = 0;
    for(uint8_t i = 0; i < 3u; i++) {
        const FtEnemy* proto = &FT_ENEMIES[trio[i]];
        expect = (int16_t)(expect + ft_xp_award(proto->level, 1, proto->xp));
    }
    CHECK_EQ(ft_encounter_xp(&e), expect);
    CHECK(expect > 0, "the prologue's last fight is worth something");

    /* Overlevelled, the same fight pays nothing — which is the anti-farming
     * rule actually taking effect now that levels move. */
    e.stats.level = 20;
    CHECK_EQ(ft_encounter_xp(&e), 0);

    /* A full run of the prologue should be able to level at least once, or
     * the whole system is decoration. */
    FtStats run;
    ft_stats_init(&run);

    int16_t owed = 0;
    for(uint8_t r = 0; r < ft_room_count(); r++) {
        const FtRoom* room = ft_room(r);
        for(uint8_t i = 0; i < room->ent_count; i++) {
            if(room->ents[i].kind != FT_ENT_FOE) continue;

            const FtRoster* roster = ft_roster(room->ents[i].roster);
            int16_t fight = 0;
            for(uint8_t m = 0; m < roster->count; m++) {
                const FtEnemy* proto = &FT_ENEMIES[roster->foes[m]];
                fight = (int16_t)(fight + ft_xp_award(proto->level, run.level, proto->xp));
            }
            owed = (int16_t)(owed + ft_xp_gain(&run, fight, ft_level_cap(0)));

            while(owed > 0) {
                ft_level_apply(&run, FT_UP_CHARGE);
                owed--;
            }
        }
    }
    CHECK(run.level > 1, "clearing the prologue levels you (reached %d)", (int)run.level);
    CHECK(run.level <= ft_level_cap(0), "but never past the chapter's cap");
}

static void test_scene_wipe(void) {
    section("scene wipe");

    /* Closes, then opens, then gets out of the way — and the swap point is
     * inside the closed part, so the scene never changes in plain sight. */
    CHECK_EQ(ft_wipe_at(0).stage, FT_WIPE_CLOSING);
    CHECK_EQ(ft_wipe_at(0).amount, 0);

    CHECK_EQ(ft_wipe_at(FT_WIPE_CLOSE_MS - 1).stage, FT_WIPE_CLOSING);
    CHECK(ft_wipe_at(FT_WIPE_CLOSE_MS - 1).amount > 245,
          "the close finishes shut (%u)", ft_wipe_at(FT_WIPE_CLOSE_MS - 1).amount);

    CHECK_EQ(ft_wipe_at(FT_WIPE_SWAP).stage, FT_WIPE_OPENING);
    CHECK_EQ(ft_wipe_at(FT_WIPE_SWAP).amount, 255);

    CHECK_EQ(ft_wipe_at(FT_WIPE_MS).stage, FT_WIPE_NONE);
    CHECK_EQ(ft_wipe_at(FT_WIPE_MS + 5000u).stage, FT_WIPE_NONE);

    /* Monotone: shut on the way in, clear on the way out, no jump back. */
    uint8_t prev = 0;
    for(uint32_t t = 0; t < FT_WIPE_CLOSE_MS; t += 5) {
        const uint8_t a = ft_wipe_at(t).amount;
        CHECK(a >= prev, "closing never reopens (%u -> %u)", prev, a);
        prev = a;
    }

    prev = 255;
    for(uint32_t t = FT_WIPE_SWAP; t < FT_WIPE_MS; t += 5) {
        const uint8_t a = ft_wipe_at(t).amount;
        CHECK(a <= prev, "opening never recloses (%u -> %u)", prev, a);
        prev = a;
    }
    CHECK(prev < 20, "the open reaches nearly clear (%u)", prev);

    /* The swap happens at full black, which is the whole point of the wipe:
     * the player never sees the overworld replaced by a battle. */
    CHECK_EQ(ft_wipe_at(FT_WIPE_SWAP - 1).amount, 254);
}

static void test_broadcast_sweep(void) {
    section("broadcast reaches foes in order");

    FtLoadout lo;
    ft_loadout_init(&lo);

    const FtEnemyId group[FT_MAX_ENEMIES] = {
        FT_ENEMY_STRAY_PACKET, FT_ENEMY_STRAY_PACKET, FT_ENEMY_STRAY_PACKET};

    FtEncounter e;
    ft_encounter_init(&e, group, 3, &lo, 9);

    /* A single-target attack lands on one frame, on everybody it touches. */
    e.menu_index = FT_ACTION_CONTACT;
    for(uint8_t i = 0; i < e.foe_count; i++) {
        CHECK_EQ(ft_encounter_foe_hit_at(&e, i), FT_ANIM_STRIKE);
    }

    /* A broadcast is a wave crossing the arena, so the nearest foe is struck
     * first and the far one last. Before this, a three-foe group died all at
     * once while the signal was still leaving the player. */
    e.menu_index = FT_ACTION_BROADCAST;
    uint8_t prev = 0;
    for(uint8_t i = 0; i < e.foe_count; i++) {
        const uint8_t at = ft_encounter_foe_hit_at(&e, i);
        CHECK(at >= FT_ANIM_EMIT, "foe %u is hit after the signal leaves", i);
        CHECK(at < FT_ANIM_RECOVER, "foe %u is hit before the recovery", i);
        if(i) CHECK(at > prev, "foe %u is reached after foe %u", i, i - 1u);
        prev = at;
    }

    /* A duel has nothing to sweep across. */
    FtEncounter duel;
    ft_encounter_init_single(&duel, FT_ENEMY_STRAY_PACKET, &lo, 9);
    duel.menu_index = FT_ACTION_BROADCAST;
    CHECK_EQ(ft_encounter_foe_hit_at(&duel, 0), FT_ANIM_STRIKE);
}

static void test_world_links(void) {
    section("room links");

    /* Every entity in every room needs a bit in the cleared bitfield. At 8
     * bytes this was down to its last ten, and the room past that would have
     * silently stopped being recorded. */
    uint16_t bits = 0;
    for(uint8_t r = 0; r < ft_room_count(); r++) {
        bits = (uint16_t)(bits + ft_room(r)->ent_count);
    }
    CHECK(FT_CLEARED_BYTES * 8u >= (uint32_t)ft_room_count() * FT_MAX_ROOM_ENTS,
          "the cleared bitfield covers every room (%u rooms, %u bits)",
          ft_room_count(), FT_CLEARED_BYTES * 8u);
    CHECK(bits > 0, "some rooms have entities");

    /* Clearing the very last entity of the very last room must stick. */
    FtWorld last;
    ft_world_init(&last);

    const uint8_t r_last = (uint8_t)(ft_room_count() - 1u);
    ft_world_enter(&last, r_last, ft_room(r_last)->exits[0].tx,
                   ft_room(r_last)->exits[0].ty);

    if(ft_room(r_last)->ent_count > 0u) {
        const uint8_t e_last = (uint8_t)(ft_room(r_last)->ent_count - 1u);
        ft_world_clear_entity(&last, e_last);
        CHECK(ft_world_entity_gone(&last, e_last),
              "the last entity of the last room stays cleared");
    }

    /* Every exit must sit on a real door, and every destination must be
     * somewhere the player can actually stand. A wrong coordinate here is an
     * unreachable exit or a spawn inside a wall — found on hardware, minutes
     * wasted, so it is checked here instead. */
    for(uint8_t r = 0; r < ft_room_count(); r++) {
        const FtRoom* room = ft_room(r);
        CHECK(room->map != NULL, "room %u has a map", r);

        for(uint8_t e = 0; e < room->exit_count; e++) {
            const FtExit* x = &room->exits[e];

            const FtTile here = ft_map_tile(room->map, x->tx, x->ty);
            CHECK(here == FT_TILE_DOOR, "room %u exit %u stands on a door (got %d)",
                  r, e, (int)here);

            CHECK(x->dest_room < ft_room_count(), "room %u exit %u leads somewhere",
                  r, e);

            /* The landing tile must be standable, or the exit drops the
             * player inside a wall. */
            const FtRoom* dest = ft_room(x->dest_room);
            const FtTile landing = ft_map_tile(dest->map, x->dest_tx, x->dest_ty);

            CHECK(!ft_tile_solid(landing), "room %u exit %u lands on open ground", r, e);
        }

        /* Foes must stand on walkable ground, or they can never be reached. */
        for(uint8_t i = 0; i < room->ent_count; i++) {
            const FtEntity* ent = &room->ents[i];
            const FtTile t = ft_map_tile(room->map, ent->tx, ent->ty);
            CHECK(!ft_tile_solid(t), "room %u entity %u stands on open ground", r, i);

            CHECK(ft_roster(ent->roster)->count > 0, "room %u entity %u has a group",
                  r, i);
        }
    }

    /* The chain must be connected both ways: every room reachable from the
     * first, and no exit leading into a room that cannot get back. */
    bool seen[16] = {false};
    uint8_t stack[16];
    uint8_t top = 0;
    stack[top++] = 0;
    seen[0] = true;

    while(top > 0) {
        const uint8_t r = stack[--top];
        const FtRoom* room = ft_room(r);
        for(uint8_t e = 0; e < room->exit_count; e++) {
            const uint8_t d = room->exits[e].dest_room;
            if(!seen[d]) {
                seen[d] = true;
                stack[top++] = d;
            }
        }
    }
    for(uint8_t r = 0; r < ft_room_count(); r++) {
        CHECK(seen[r], "room %u is reachable from the start", r);
    }
}

int main(void) {
    printf("\nFlipper Tales — core tests\n\n");

    test_ratings();
    test_guard_clamp();
    test_damage_basics();
    test_pierce();
    test_attribute_locks();
    test_jam_and_capture();
    test_roll();
    test_signal_meter();
    test_signal_library();
    test_priority();
    test_progression();
    test_flash_budget();
    test_loadout();
    test_enemy_table();
    test_guard_timing();
    test_rating_timing();
    test_guarded_window();
    test_turn_economy();
    test_death_timing();
    test_ready_beat();
    test_encounter();
    test_tutorial();
    test_anim();
    test_map();
    test_tile_orientation();
    test_world();
    test_foe_ai();
    test_hit_fx();
    test_reach();
    test_practice();
    test_defeat();
    test_save();
    test_save_world();
    test_levelup();
    test_scene_wipe();
    test_broadcast_sweep();
    test_world_links();
    test_world_tour();
    test_rng();

    printf("\n%d checks, %d failures\n\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
