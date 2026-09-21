/* Host-side unit tests for src/core. Every assertion below corresponds to a
 * claim made in docs/DESIGN.md. */
#include <stdio.h>
#include <string.h>

#include "ft_combat.h"
#include "ft_data.h"
#include "ft_encounter.h"
#include "ft_priority.h"
#include "ft_progress.h"
#include "ft_rng.h"
#include "ft_tutorial.h"
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
    CHECK_EQ(ft_guard_from_timing(0, false), FT_GUARD_CAPTURE);
    CHECK_EQ(ft_guard_from_timing(50, false), FT_GUARD_CAPTURE);

    /* Out to 150 ms jams. */
    CHECK_EQ(ft_guard_from_timing(51, false), FT_GUARD_JAM);
    CHECK_EQ(ft_guard_from_timing(150, false), FT_GUARD_JAM);

    /* Earlier than that has lapsed by the time the hit lands. */
    CHECK_EQ(ft_guard_from_timing(151, false), FT_GUARD_NONE);
    CHECK_EQ(ft_guard_from_timing(5000, false), FT_GUARD_NONE);

    /* A press after impact is late, not a guard. */
    CHECK_EQ(ft_guard_from_timing(-1, false), FT_GUARD_NONE);

    /* Hard Mode halves both windows. */
    CHECK_EQ(ft_guard_from_timing(25, true), FT_GUARD_CAPTURE);
    CHECK_EQ(ft_guard_from_timing(26, true), FT_GUARD_JAM);
    CHECK_EQ(ft_guard_from_timing(75, true), FT_GUARD_JAM);
    CHECK_EQ(ft_guard_from_timing(76, true), FT_GUARD_NONE);

    /* What was a capture on normal is only a jam on Hard Mode. */
    CHECK_EQ(ft_guard_from_timing(40, false), FT_GUARD_CAPTURE);
    CHECK_EQ(ft_guard_from_timing(40, true), FT_GUARD_JAM);
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

static void test_ready_beat(void) {
    section("ready beat");

    FtLoadout lo;
    ft_loadout_init(&lo);

    FtEncounter e;
    ft_encounter_init(&e, FT_ENEMY_STRAY_PACKET, &lo, 2);
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
    ft_encounter_init(&g, FT_ENEMY_STRAY_PACKET, &lo, 4);
    g.phase = FT_PHASE_TELEGRAPH;
    g.phase_ms = 0;
    CHECK(ft_encounter_in_ready(&g), "guard sweep also starts with a beat");

    ft_encounter_press_ok(&g);
    CHECK(!g.guard_pressed, "ready-beat guard presses must be ignored");

    /* Phases without a timing bar never report a ready beat. */
    FtEncounter m;
    ft_encounter_init(&m, FT_ENEMY_STRAY_PACKET, &lo, 6);
    CHECK(!ft_encounter_in_ready(&m), "the menu is not a ready beat");
    CHECK_EQ(ft_encounter_sweep_window(&m), 0);
}

static void test_encounter(void) {
    section("encounter state machine");

    FtLoadout lo;
    ft_loadout_init(&lo);

    FtEncounter e;
    ft_encounter_init(&e, FT_ENEMY_STRAY_PACKET, &lo, 7);

    CHECK_EQ(e.phase, FT_PHASE_MENU);
    CHECK_EQ(e.enemy_charge, FT_ENEMIES[FT_ENEMY_STRAY_PACKET].charge);
    CHECK(!ft_encounter_over(&e), "a fresh encounter is not over");
    CHECK(ft_encounter_incoming(&e) == NULL, "nothing incoming during the menu");

    /* The menu wraps in both directions. */
    ft_encounter_menu_move(&e, -1);
    CHECK_EQ(e.menu_index, FT_ACTION_COUNT - 1);
    ft_encounter_menu_move(&e, 1);
    CHECK_EQ(e.menu_index, 0);

    /* Thinking must never cost Charge: the roll is paused in the menu. */
    e.roll.target = 0;
    const int16_t before = e.roll.current;
    ft_encounter_tick(&e, 10000);
    CHECK_EQ(e.roll.current, before);
    CHECK_EQ(e.phase, FT_PHASE_MENU);

    /* Attribute locks are surfaced as unavailable menu entries. */
    FtEncounter beacon;
    ft_encounter_init(&beacon, FT_ENEMY_DRIFT_BEACON, &lo, 1);
    CHECK(ft_encounter_action_available(&beacon, FT_ACTION_BROADCAST), "broadcast reaches AIRBORNE");
    CHECK(!ft_encounter_action_available(&beacon, FT_ACTION_CONTACT), "contact cannot reach AIRBORNE");
    CHECK(ft_encounter_action_available(&beacon, FT_ACTION_DEFEND), "Defend is always available");

    FtEncounter lock;
    ft_encounter_init(&lock, FT_ENEMY_SEALED_LOCK, &lo, 1);
    CHECK(!ft_encounter_action_available(&lock, FT_ACTION_BROADCAST), "broadcast is refused by ENCRYPTED");
    CHECK(ft_encounter_action_available(&lock, FT_ACTION_CONTACT), "contact works on ENCRYPTED");

    /* Confirming an unavailable action does nothing at all. */
    beacon.menu_index = FT_ACTION_CONTACT;
    ft_encounter_press_ok(&beacon);
    CHECK_EQ(beacon.phase, FT_PHASE_MENU);

    /* A perfectly timed action command earns the top rating. */
    FtEncounter fight;
    ft_encounter_init(&fight, FT_ENEMY_STRAY_PACKET, &lo, 3);
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
    ft_encounter_init(&focus, FT_ENEMY_STRAY_PACKET, &lo, 5);
    const int16_t sig_before = focus.signal.value;
    focus.menu_index = FT_ACTION_FOCUS;
    ft_encounter_press_ok(&focus);
    CHECK_EQ(focus.phase, FT_PHASE_RESULT); /* skips the sweep entirely */
    CHECK_EQ(focus.signal.value, sig_before + ft_signal_focus_gain(0));

    /* A perfect hit one-shots the tutorial enemy (4 power at 200%), so the
     * full-battle run uses the toughest M1 enemy to guarantee the player is
     * actually attacked. */
    FtEncounter quick;
    ft_encounter_init(&quick, FT_ENEMY_STRAY_PACKET, &lo, 3);
    CHECK(FT_ENEMIES[FT_ENEMY_STRAY_PACKET].charge <= 8, "tutorial enemy stays one-shottable");

    /* A whole battle terminates rather than spinning forever. */
    FtEncounter run;
    ft_encounter_init(&run, FT_ENEMY_SEALED_LOCK, &lo, 11);
    int guard_ticks = 0;
    for(int i = 0; i < 20000 && !ft_encounter_over(&run); i++) {
        if(run.phase == FT_PHASE_MENU) {
            run.menu_index = FT_ACTION_CONTACT;
            ft_encounter_press_ok(&run);
        } else if(
            run.phase == FT_PHASE_PLAYER_ACT &&
            run.phase_ms >= FT_READY_MS + FT_ACTION_WINDOW_MS / 2) {
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
     * is capturable; skipping the action command keeps the player's damage low
     * enough that the enemy actually gets a turn. */
    FtEncounter cap;
    ft_encounter_init(&cap, FT_ENEMY_STRAY_PACKET, &lo, 11);
    bool faced_attack = false;
    for(int i = 0; i < 20000 && !ft_encounter_over(&cap); i++) {
        if(cap.phase == FT_PHASE_MENU) {
            cap.menu_index = FT_ACTION_CONTACT;
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
    ft_encounter_init(&undo, FT_ENEMY_SEALED_LOCK, &lo, 11);
    undo.phase = FT_PHASE_TELEGRAPH;
    undo.enemy_attack_index = 1; /* Seal: UNDODGEABLE */
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
    ft_encounter_init(&e, FT_ENEMY_STRAY_PACKET, &lo, 3);

    /* On by default, and silent the moment it is turned off. */
    CHECK(e.coach, "coaching starts on");
    CHECK(ft_tutorial_hint(&e) != NULL, "the menu should be coached");

    e.coach = false;
    CHECK(ft_tutorial_hint(&e) == NULL, "coaching off means silence");
    e.coach = true;

    /* A locked module explains itself rather than just being struck through. */
    FtEncounter beacon;
    ft_encounter_init(&beacon, FT_ENEMY_DRIFT_BEACON, &lo, 3);
    beacon.menu_index = FT_ACTION_CONTACT;
    const char* flies = ft_tutorial_hint(&beacon);
    CHECK(flies && strstr(flies, "SUBGHZ"), "an airborne lock should name the fix");

    FtEncounter lock;
    ft_encounter_init(&lock, FT_ENEMY_SEALED_LOCK, &lo, 3);
    lock.menu_index = FT_ACTION_BROADCAST;
    const char* enc = ft_tutorial_hint(&lock);
    CHECK(enc && strstr(enc, "NFC"), "an encrypted lock should name the fix");

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
    ft_encounter_init(&undo, FT_ENEMY_SEALED_LOCK, &lo, 3);
    undo.phase = FT_PHASE_TELEGRAPH;
    undo.enemy_attack_index = 1;
    CHECK_EQ(FT_ENEMIES[FT_ENEMY_SEALED_LOCK].attacks[1].klass, FT_CLASS_UNDODGEABLE);
    const char* brace = ft_tutorial_hint(&undo);
    CHECK(brace && strstr(brace, "No guard"), "undodgeable should say so");

    /* Feedback after a guard distinguishes a jam from a capture. */
    FtEncounter jam;
    ft_encounter_init(&jam, FT_ENEMY_DRIFT_BEACON, &lo, 3);
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
    ft_encounter_init(&done, FT_ENEMY_STRAY_PACKET, &lo, 3);
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
                        ft_encounter_init(&w, (FtEnemyId)enemy, &lo, 1);
                        w.phase = (FtPhase)phase;
                        w.menu_index = (uint8_t)menu;
                        w.enemy_attack_index =
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
    ft_encounter_init(&e, FT_ENEMY_STRAY_PACKET, &lo, 5);

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
    test_ready_beat();
    test_encounter();
    test_tutorial();
    test_anim();
    test_rng();

    printf("\n%d checks, %d failures\n\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
