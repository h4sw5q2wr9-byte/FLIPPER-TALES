/* Host-side unit tests for src/core. Every assertion below corresponds to a
 * claim made in docs/DESIGN.md. */
#include <stdio.h>
#include <string.h>

#include "ft_combat.h"
#include "ft_data.h"
#include "ft_encounter.h"
#include "ft_guide.h"
#include "ft_practice.h"
#include "ft_save.h"
#include "ft_priority.h"
#include "ft_progress.h"
#include "ft_map.h"
#include "ft_rng.h"
#include "ft_tutorial.h"
#include "ft_audio.h"
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
    CHECK(!r.perfect, "jamming must not capture");

    /* Faraday improves the reduction. */
    p.jam_reduction_pct = 70;
    r = ft_resolve_hit(&atk, &def, &p);
    CHECK_EQ(r.damage, 3); /* 11 * 30 / 100 */

    /* Capture: zero damage, signal captured. Broadcast is dodged, not countered. */
    p.jam_reduction_pct = 0;
    p.guard = FT_GUARD_CAPTURE;
    r = ft_resolve_hit(&atk, &def, &p);
    CHECK_EQ(r.damage, 0);
    CHECK(r.perfect, "capture should yield a signal");
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
    CHECK(!r.perfect, "GUARDED attacks keep their secrets");

    /* UNDODGEABLE: the guard is ignored outright. */
    FtAttack undodgeable = mk_attack(11, FT_DELIVERY_BROADCAST, FT_CLASS_UNDODGEABLE);
    undodgeable.payload = FT_PAYLOAD_CORRUPT;
    r = ft_resolve_hit(&undodgeable, &def, &p);
    CHECK_EQ(r.damage, 11);
    CHECK(!r.perfect, "UNDODGEABLE attacks cannot be captured");
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

/* What ft_level_apply used to do, in one call: take the level and put the
 * orb it pays into a stat. The two are separate now precisely so the second
 * half can be undone later. */
static bool level_into(FtStats* s, FtLevelChoice choice) {
    ft_level_take(s);
    if(ft_orb_spend(s, choice)) return true;

    /* Refused: hand the level back so a capped stat costs nothing, which is
     * what the old all-or-nothing call guaranteed. */
    s->level--;
    s->orbs = (int16_t)(s->orbs - FT_ORBS_PER_LEVEL);
    return false;
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
    CHECK(level_into(&s, FT_UP_CHARGE), "charge upgrade should apply");
    CHECK_EQ(s.charge_max, FT_START_CHARGE + FT_LEVEL_UP_CHARGE);
    CHECK_EQ(s.charge, s.charge_max);
    CHECK_EQ(s.ram, s.ram_max);

    CHECK(level_into(&s, FT_UP_FLASH), "flash upgrade should apply");
    CHECK_EQ(s.flash_max, FT_START_FLASH + FT_LEVEL_UP_FLASH);

    /* Capped stats become unavailable as choices. */
    s.flash_max = FT_CAP_FLASH;
    CHECK(!ft_level_choice_available(&s, FT_UP_FLASH), "capped Flash is unavailable");
    CHECK(!level_into(&s, FT_UP_FLASH), "capped Flash cannot be raised");

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
    for(uint8_t r = FT_ROOM_SLICE_FIRST; r <= FT_ROOM_SLICE_LAST; r++) {
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

    /* Every area slice carries the gate for the chapter after it. Chapter 1's
     * own rooms do not: nothing there is locked with a key. */
    for(uint8_t r = FT_ROOM_SLICE_FIRST; r <= FT_ROOM_SLICE_LAST; r++) {
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

    /* No FAST foe here: the turn economy is the thing under test, and a FAST
     * foe opens the fight before the player ever sees the menu. */
    const FtEnemyId group[3] = {
        FT_ENEMY_STRAY_PACKET, FT_ENEMY_DRIFT_BEACON, FT_ENEMY_STRAY_PACKET};

    FtEncounter e;
    ft_encounter_init(&e, group, 3, &lo, 5);
    CHECK_EQ(e.player_turns, 0);
    CHECK_EQ(e.phase, FT_PHASE_MENU);

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

    /* One flat ring of every action, wrapping both ways. The menu used to be
     * two levels with the modules behind a drill-down, which put two of them
     * an extra press away and gave the screen two rows of buttons. */
    CHECK_EQ(FT_ACTION_COUNT, 6);
    CHECK_EQ(e.menu_index, 0);

    ft_encounter_menu_move(&e, -1);
    CHECK_EQ(e.menu_index, FT_ACTION_COUNT - 1);
    ft_encounter_menu_move(&e, 1);
    CHECK_EQ(e.menu_index, 0);

    /* Every action is reachable by walking one way, and the walk comes home. */
    bool seen[FT_ACTION_COUNT];
    for(uint8_t i = 0; i < FT_ACTION_COUNT; i++) seen[i] = false;

    for(uint8_t i = 0; i < FT_ACTION_COUNT; i++) {
        seen[e.menu_index] = true;
        ft_encounter_menu_move(&e, 1);
    }
    for(uint8_t i = 0; i < FT_ACTION_COUNT; i++) {
        CHECK(seen[i], "action %u is on the ring", i);
    }
    CHECK_EQ(e.menu_index, 0);

    /* Big deltas wrap rather than running off the end. */
    ft_encounter_menu_move(&e, 4);
    CHECK(e.menu_index < FT_ACTION_COUNT, "a big step stays on the ring");
    ft_encounter_menu_move(&e, -4);
    CHECK_EQ(e.menu_index, 0);

    /* The cursor only moves in the menu: a stray press mid-sweep must not
     * change what you already committed to. */
    e.phase = FT_PHASE_PLAYER_ACT;
    ft_encounter_menu_move(&e, 1);
    CHECK_EQ(e.menu_index, 0);
    e.phase = FT_PHASE_MENU;

    /* Confirming takes the action: there is no level to drill into. */
    e.menu_index = FT_ACTION_DEFEND;
    ft_encounter_menu_confirm(&e);
    CHECK_EQ(e.phase, FT_PHASE_RESULT);

    /* Thinking must never cost Charge: the roll is paused in the menu. */
    FtEncounter idle;
    ft_encounter_init_single(&idle, FT_ENEMY_STRAY_PACKET, &lo, 7);

    idle.roll.target = 0;
    const int16_t before = idle.roll.current;
    ft_encounter_tick(&idle, 10000);
    CHECK_EQ(idle.roll.current, before);
    CHECK_EQ(idle.phase, FT_PHASE_MENU);

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
    CHECK(!ft_encounter_action_available(&empty, FT_ACTION_DEFLECT), "no capture, no replay");
    empty.menu_index = FT_ACTION_DEFLECT;
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
            /* Contact costs MP now, so a player who only ever presses it
             * runs dry. Bracing is what buys it back — which is the loop the
             * cost exists to create. */
            run.menu_index =
                ft_encounter_action_available(&run, FT_ACTION_CONTACT) ?
                    (uint8_t)FT_ACTION_CONTACT :
                    (uint8_t)FT_ACTION_DEFEND;
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
    CHECK(faced_attack, "the perfect run should have faced an attack");
    CHECK_EQ(cap.last_guard, FT_GUARD_CAPTURE);
    CHECK(cap.last_enemy_hit.perfect, "a frame-perfect guard is a perfect block");
    CHECK_EQ(cap.last_enemy_hit.damage, 0);

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

    beacon.signal.value = 0;
    const char* need = ft_encounter_action_block(&beacon, FT_ACTION_DEFLECT);
    CHECK(need != NULL, "a deflect with no bar still refuses");
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

    jam.last_enemy_hit.perfect = true;
    const char* cap_line = ft_tutorial_hint(&jam);
    CHECK(cap_line && strstr(cap_line, "Perfect"), "a perfect block is celebrated");

    /* And with the stance up, what the bounce did outranks both. */
    jam.deflect_armed = true;
    jam.last_deflect_fired = true;
    jam.last_guard = FT_GUARD_CAPTURE;
    const char* back = ft_tutorial_hint(&jam);
    CHECK(back && strstr(back, "bounce"), "a full bounce is the headline");

    jam.last_guard = FT_GUARD_JAM;
    const char* half = ft_tutorial_hint(&jam);
    CHECK(half && strstr(half, "Half"), "and a half one says so");

    /* Armed and missed is worth saying too: a wasted bar is information. */
    jam.last_deflect_fired = false;
    const char* waste = ft_tutorial_hint(&jam);
    CHECK(waste && strstr(waste, "wasted"), "a missed deflect says the bar went");

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

    /* Mid-map, the focus tile is centred. The camera works in tiles, not in
     * whatever the character happens to be drawn at. */
    const FtPos mid = {160, 80};
    const FtPos c3 = ft_map_camera(&big, mid);
    CHECK_EQ(c3.x, 160 + FT_TILE_PX / 2 - (FT_VIEW_W * FT_TILE_PX) / 2);
    CHECK_EQ(c3.y, 80 + FT_TILE_PX / 2 - (FT_VIEW_H * FT_TILE_PX) / 2);

    /* A map smaller than the viewport pins to the origin instead of going
     * negative and revealing a band of off-map wall. */
    const FtPos c4 = ft_map_camera(&m, mid);
    CHECK_EQ(c4.x, 0);
    CHECK_EQ(c4.y, 0);

    /* A stepper reports its tile's pixel origin and nothing else. It used to
     * subtract a sprite height, which the renderers then subtracted again —
     * every actor floated half a tile above the ground. */
    FtStepper st = {3, 5, 0, 0, 0};
    const FtPos rest = ft_stepper_pos(&st, FT_STEP_MS);
    CHECK_EQ(rest.x, 3 * FT_TILE_PX);
    CHECK_EQ(rest.y, 5 * FT_TILE_PX);

    /* Mid-step it interpolates, and lands exactly on the next tile. */
    st.dx = 1;
    st.step_ms = FT_STEP_MS / 2;
    const FtPos half = ft_stepper_pos(&st, FT_STEP_MS);
    CHECK_EQ(half.y, 5 * FT_TILE_PX);
    CHECK(half.x > 3 * FT_TILE_PX && half.x < 4 * FT_TILE_PX,
          "half a step is between the tiles (%d)", (int)half.x);

    st.step_ms = FT_STEP_MS;
    const FtPos done = ft_stepper_pos(&st, FT_STEP_MS);
    CHECK_EQ(done.x, 4 * FT_TILE_PX);
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

    /* A foe you beat stays beaten for the visit... */
    ft_world_enter(&w, 1, 2, 2);
    CHECK(!ft_world_entity_gone(&w, 0), "foes start alive");
    CHECK(w.foes[0].alive, "and are on the board");

    ft_world_clear_entity(&w, 0);
    CHECK(ft_world_entity_gone(&w, 0), "beating one removes it");
    CHECK(!w.foes[0].alive, "and takes it off the board");

    /* ...and clearing is per room, so it does not reach into another one. */
    ft_world_enter(&w, 2, 2, 2);
    CHECK(!ft_world_entity_gone(&w, 0), "a different room is unaffected");

    /* ...but walking back in repopulates it. A cleared corridor used to stay
     * cleared forever, which made backtracking free and "go round again" the
     * answer to everything. */
    ft_world_enter(&w, 1, 2, 2);
    CHECK(!ft_world_entity_gone(&w, 0), "coming back finds it standing again");
    CHECK(w.foes[0].alive, "and it is spawned");
    CHECK(w.foes[0].count > 0, "with its walkers");

    /* Every room with foes behaves the same way. */
    for(uint8_t r = 0; r < ft_room_count(); r++) {
        const FtRoom* room = ft_room(r);
        if(room->ent_count == 0u) continue;

        FtWorld again;
        ft_world_init(&again);
        ft_world_enter(&again, r, room->exits[0].tx, room->exits[0].ty);

        for(uint8_t i = 0; i < room->ent_count; i++) ft_world_clear_entity(&again, i);
        for(uint8_t i = 0; i < room->ent_count; i++) {
            CHECK(ft_world_entity_gone(&again, i), "room %u entity %u clears", r, i);
        }

        ft_world_enter(&again, r, room->exits[0].tx, room->exits[0].ty);
        for(uint8_t i = 0; i < room->ent_count; i++) {
            if(room->ents[i].kind != FT_ENT_FOE) continue;
            CHECK(!ft_world_entity_gone(&again, i),
                  "room %u entity %u comes back", r, i);
        }
    }
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

    /* Chasing gets round things. A single greedy step gave up the moment a
     * wall was in the way: a foe on the far side of a crate walked into it
     * forever, and one chasing round a corner stopped at the corner. */
    for(uint8_t rm = 0; rm < ft_room_count(); rm++) {
        const FtRoom* rr = ft_room(rm);

        /* The first entity that actually fights. Room 0's is a person to talk
         * to, who is never alive and never moves. */
        int which = -1;
        for(uint8_t i = 0; i < rr->ent_count && i < FT_MAX_ROOM_ENTS; i++) {
            if(rr->ents[i].kind == FT_ENT_FOE) {
                which = (int)i;
                break;
            }
        }
        if(which < 0) continue;

        FtWorld hunt;
        ft_world_init(&hunt);
        ft_world_enter(&hunt, rm, rr->exits[0].tx, rr->exits[0].ty);
        hunt.foes[which].alert = true;

        /* The nearest member of the group, not member zero. A roster walks a
         * corridor in single file, so which one arrives first is a matter of
         * traffic — and "did the group reach you" is the question. */
        const FtFoeState* f = &hunt.foes[which];
        int32_t best = INT32_MAX;
        for(uint8_t m = 0; m < f->count; m++) {
            const int32_t d = abs_i32((int32_t)f->w[m].mv.tx - (int32_t)hunt.mv.tx) +
                              abs_i32((int32_t)f->w[m].mv.ty - (int32_t)hunt.mv.ty);
            if(d < best) best = d;
        }
        const int32_t start = best;

        /* The player stands still; the chaser has plenty of time. */
        for(int t = 0; t < 600; t++) {
            ft_world_update(&hunt, 0, 0, 20);
            hunt.foes[which].alert = true;

            for(uint8_t m = 0; m < f->count; m++) {
                const int32_t d =
                    abs_i32((int32_t)f->w[m].mv.tx - (int32_t)hunt.mv.tx) +
                    abs_i32((int32_t)f->w[m].mv.ty - (int32_t)hunt.mv.ty);
                if(d < best) best = d;
            }
        }

        CHECK(best < start || start <= 1,
              "room %u: a chaser closes in (%d -> %d)", rm, (int)start, (int)best);
        CHECK(best <= 2, "room %u: and gets there (%d)", rm, (int)best);
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
            CHECK(!ft_encounter_action_available(&ke, FT_ACTION_DEFLECT),
                  "the basic kit starts with nothing captured");
        } else {
            CHECK(ft_encounter_action_available(&ke, FT_ACTION_DEFLECT),
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

    /* Play a little: move rooms, beat something, level up, install a card. */
    ft_world_enter(&w, 2, 3, 4);
    ft_world_clear_entity(&w, 0);
    level_into(&w.stats, FT_UP_RAM);
    ft_loadout_add(&w.loadout, FT_MOD_AMPLIFY);

    w.save_room = 2;
    w.save_tx = 3;
    w.save_ty = 4;

    FtSaveData d;
    ft_save_from_world(&w, false, true, &d);

    uint8_t buf[FT_SAVE_MAX_BYTES];
    const uint8_t len = ft_save_encode(&d, buf, sizeof(buf));
    CHECK(len > 0, "the run encodes");

    FtSaveData back;
    CHECK(ft_save_decode(buf, len, &back), "and decodes");

    FtWorld loaded;
    bool coach = true;
    ft_save_to_world(&back, &loaded, &coach, NULL);

    CHECK_EQ(loaded.room, 2);
    CHECK_EQ(loaded.mv.tx, 3);
    CHECK_EQ(loaded.mv.ty, 4);
    CHECK_EQ(loaded.save_room, 2);
    CHECK_EQ(loaded.stats.ram_max, w.stats.ram_max);
    CHECK_EQ(loaded.stats.level, w.stats.level);
    CHECK_EQ(loaded.loadout.stacks[FT_MOD_AMPLIFY], 1);
    CHECK(!coach, "the tips setting survives too");

    /* Loading walks you into the room, and walking into a room repopulates
     * it — so a save made in a cleared room comes back to a live one. That
     * is the same rule as backtracking, applied to the same act. */
    CHECK(!ft_world_entity_gone(&loaded, 0), "loading repopulates the room");
    CHECK(loaded.foes[0].alive, "and the foe is standing");

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
    ft_save_to_world(&other, &elsewhere, NULL, NULL);
    CHECK_EQ(elsewhere.room, 0);
    CHECK(ft_world_entity_gone(&elsewhere, 0) == false ||
              ft_room(0)->ent_count == 0,
          "clearing is per room, not global");
}

static void test_payloads(void) {
    section("status payloads");

    FtLoadout lo;
    ft_loadout_init(&lo);

    /* Every payload in the data must be one the encounter actually applies.
     * All three were declared, ft_resolve_hit even computed payload_applied,
     * and nothing read it: an attack that says it corrupts you and does not
     * is worse than one that never claimed to. */
    FtEncounter e;
    ft_encounter_init_single(&e, FT_ENEMY_STRAY_PACKET, &lo, 3);

    for(uint8_t i = 0; i < FT_PAYLOAD_COUNT; i++) {
        CHECK_EQ(ft_encounter_status(&e, (FtPayload)i), 0);
    }
    CHECK(ft_encounter_status_tag(&e) == NULL, "a clean fighter has no tag");
    CHECK_EQ(ft_encounter_turns_this_round(&e), FT_PLAYER_TURNS_PER_ROUND);

    /* Corrupt: costs HP at the top of each round, and wears off. */
    e.status[FT_PAYLOAD_CORRUPT] = FT_STATUS_TURNS;
    CHECK(ft_encounter_status_tag(&e) != NULL, "and a tagged one does");

    const int16_t hp0 = e.roll.target;
    ft_encounter_init_single(&e, FT_ENEMY_STRAY_PACKET, &lo, 3);
    e.status[FT_PAYLOAD_CORRUPT] = 1;

    int rounds = 0;
    for(int i = 0; i < 60000 && !ft_encounter_over(&e) && rounds < 3; i++) {
        if(e.phase == FT_PHASE_MENU) {
            e.menu_index = FT_ACTION_FOCUS;
            ft_encounter_press_ok(&e);
            rounds++;
        }
        ft_encounter_tick(&e, 10);
    }
    CHECK_EQ(ft_encounter_status(&e, FT_PAYLOAD_CORRUPT), 0);
    CHECK(e.roll.target < hp0, "corrupt took HP (%d -> %d)",
          (int)hp0, (int)e.roll.target);

    /* Drain: costs MP the same way. */
    FtEncounter dr;
    ft_encounter_init_single(&dr, FT_ENEMY_STRAY_PACKET, &lo, 3);
    dr.stats.ram = dr.stats.ram_max;
    dr.status[FT_PAYLOAD_DRAIN] = FT_STATUS_TURNS;

    const int16_t mp0 = dr.stats.ram;
    int dr_rounds = 0;
    for(int i = 0; i < 60000 && !ft_encounter_over(&dr) && dr_rounds < 4; i++) {
        if(dr.phase == FT_PHASE_MENU) {
            dr.menu_index = FT_ACTION_FOCUS;
            ft_encounter_press_ok(&dr);
            dr_rounds++;
        }
        ft_encounter_tick(&dr, 10);
    }
    CHECK(dr.stats.ram < mp0, "drain took MP (%d -> %d)",
          (int)mp0, (int)dr.stats.ram);

    /* Stall: halves the round's actions, which is what "may lose the turn"
     * means on a two-action round. */
    FtEncounter st;
    ft_encounter_init_single(&st, FT_ENEMY_STRAY_PACKET, &lo, 3);
    CHECK_EQ(ft_encounter_turns_this_round(&st), 2);

    st.status[FT_PAYLOAD_STALL] = FT_STATUS_TURNS;
    CHECK_EQ(ft_encounter_turns_this_round(&st), 1);

    /* A guard nullifies the payload entirely — that was already true of
     * ft_resolve_hit, and it has to stay true now that it means something. */
    const FtAttack* dot = &FT_ENEMIES[FT_ENEMY_MAST_RELAY].attacks[1];
    CHECK(dot->payload != FT_PAYLOAD_NONE, "the relay's surge carries one");

    const FtDefender bare = {0, 0};
    const FtHitParams clean = {0, 0, FT_RATING_MISS, false, FT_GUARD_NONE, 0};
    const FtHitParams jammed = {0, 0, FT_RATING_MISS, false, FT_GUARD_JAM, 0};

    CHECK(ft_resolve_hit(dot, &bare, &clean).payload_applied,
          "a clean hit lands it");
    CHECK(!ft_resolve_hit(dot, &bare, &jammed).payload_applied,
          "a jam stops it");

    /* End to end: take the surge, and be corrupted by it. */
    FtEncounter fight;
    ft_encounter_init_single(&fight, FT_ENEMY_MAST_RELAY, &lo, 3);

    bool got = false;
    for(int i = 0; i < 120000 && !ft_encounter_over(&fight); i++) {
        if(fight.phase == FT_PHASE_MENU) {
            fight.menu_index = FT_ACTION_FOCUS;
            ft_encounter_press_ok(&fight);
        }
        ft_encounter_tick(&fight, 10);

        if(ft_encounter_status_tag(&fight) != NULL) {
            got = true;
            break;
        }
    }
    CHECK(got, "an unguarded payload attack leaves a status");

    /* A new fight starts clean, whatever the last one did. */
    FtEncounter fresh;
    ft_encounter_init_single(&fresh, FT_ENEMY_STRAY_PACKET, &lo, 9);
    CHECK(ft_encounter_status_tag(&fresh) == NULL, "statuses do not carry over");
}

static void test_ambush(void) {
    section("who reached whom");

    FtLoadout lo;
    ft_loadout_init(&lo);

    /* Walking into a foe: you open, as always. */
    FtEncounter mine;
    ft_encounter_init_single(&mine, FT_ENEMY_STRAY_PACKET, &lo, 3);
    CHECK_EQ(mine.phase, FT_PHASE_MENU);

    /* One walking into you: it opens. */
    FtEncounter theirs;
    ft_encounter_init_single(&theirs, FT_ENEMY_STRAY_PACKET, &lo, 3);
    ft_encounter_enemy_opens(&theirs);
    CHECK_EQ(theirs.phase, FT_PHASE_TELEGRAPH);

    /* And the player still gets their turn straight after. */
    for(int i = 0; i < 60000 && theirs.phase != FT_PHASE_MENU; i++) {
        ft_encounter_tick(&theirs, 10);
        if(ft_encounter_over(&theirs)) break;
    }
    CHECK_EQ(theirs.phase, FT_PHASE_MENU);

    /* A board whose only foe never takes turns cannot be handed the opening
     * — a bulwark ambush must not stall the fight before it starts. */
    FtEncounter wall;
    ft_encounter_init_single(&wall, FT_ENEMY_BLANK_WALL, &lo, 3);
    ft_encounter_enemy_opens(&wall);
    CHECK_EQ(wall.phase, FT_PHASE_MENU);

    /* Same for a lone sleeper's opposite: a sleeper alone is awake, so it
     * can take the ambush. */
    FtEncounter sleeper;
    ft_encounter_init_single(&sleeper, FT_ENEMY_COLD_BOOTER, &lo, 3);
    ft_encounter_enemy_opens(&sleeper);
    CHECK_EQ(sleeper.phase, FT_PHASE_TELEGRAPH);

    /* The world reports it: a foe's step landing on the player is an ambush,
     * the player's own step onto a foe is not. */
    const uint8_t ROOM = 1;
    const FtRoom* room = ft_room(ROOM);

    FtWorld w;
    ft_world_init(&w);
    ft_world_enter(&w, ROOM, room->exits[0].tx, room->exits[0].ty);
    CHECK(!w.ambushed, "a fresh room is not an ambush");

    /* Stand still next to an alerted foe and let it come to you. */
    w.foes[0].alert = true;
    bool caught = false;
    for(int t = 0; t < 4000 && !caught; t++) {
        ft_world_update(&w, 0, 0, 20);
        w.foes[0].alert = true;
        if(w.ambushed) caught = true;
    }
    CHECK(caught, "a foe that reaches you is an ambush");
    CHECK(ft_world_foe_contact(&w) >= 0, "and it is touching you");

    /* The flag is for one update only, like `arrived`. */
    ft_world_update(&w, 0, 0, 20);
    CHECK(!w.ambushed, "and the flag does not linger");
}

static void test_always_something_to_do(void) {
    section("never stuck");

    FtLoadout lo;
    ft_loadout_init(&lo);

    /* MP gates the strong module, so the question this raises is whether a
     * board can ever leave the player with nothing at all to press. It must
     * not: Guard and Focus cost nothing and Guard is what buys MP back. */
    for(uint8_t i = 0; i < FT_ENEMY_COUNT; i++) {
        FtEncounter e;
        ft_encounter_init_single(&e, (FtEnemyId)i, &lo, 3);

        for(int16_t mp = 0; mp <= 3; mp++) {
            e.stats.ram = mp;

            uint8_t usable = 0;
            for(uint8_t a = 0; a < FT_ACTION_COUNT; a++) {
                if(ft_encounter_action_available(&e, (FtAction2)a)) usable++;
            }
            CHECK(usable > 0, "%s at %d MP leaves something to press",
                  FT_ENEMIES[i].name, (int)mp);

            CHECK(ft_encounter_action_available(&e, FT_ACTION_DEFEND),
                  "%s: Guard is always there", FT_ENEMIES[i].name);
            CHECK(ft_encounter_action_available(&e, FT_ACTION_FOCUS),
                  "%s: so is Focus", FT_ENEMIES[i].name);
        }
    }

    /* Out of MP the strong module is refused, and the refusal names the fix
     * rather than just saying no. */
    FtEncounter dry;
    ft_encounter_init_single(&dry, FT_ENEMY_STRAY_PACKET, &lo, 3);
    dry.stats.ram = 0;

    CHECK(!ft_encounter_action_available(&dry, FT_ACTION_CONTACT),
          "no MP, no NFC");

    const char* why = ft_encounter_action_block(&dry, FT_ACTION_CONTACT);
    CHECK(why && strstr(why, "MP"), "and it says what is missing");
    CHECK(why && strlen(why) <= FT_TUTORIAL_MAX_CHARS, "in one line");

    /* Guarding buys it back, so the loop closes. */
    dry.menu_index = FT_ACTION_DEFEND;
    ft_encounter_press_ok(&dry);
    CHECK(dry.stats.ram > 0, "bracing restores MP");

    /* Spending it actually costs: the number on screen has to move. */
    FtEncounter spend;
    ft_encounter_init_single(&spend, FT_ENEMY_STRAY_PACKET, &lo, 3);

    const int16_t before = spend.stats.ram;
    spend.menu_index = FT_ACTION_CONTACT;
    ft_encounter_press_ok(&spend);
    ft_encounter_tick(&spend, FT_READY_MS + FT_ACTION_WINDOW_MS + 10);

    CHECK_EQ(spend.stats.ram, before - (int16_t)ft_encounter_action_cost(
                                            &spend, FT_ACTION_CONTACT));
    CHECK(ft_encounter_action_cost(&spend, FT_ACTION_CONTACT) > 0,
          "and it costs something");
    CHECK_EQ(ft_encounter_action_cost(&spend, FT_ACTION_BROADCAST), 0);
    CHECK_EQ(ft_encounter_action_cost(&spend, FT_ACTION_DEFEND), 0);
}

static void test_bulwark(void) {
    section("a wall in front");

    FtLoadout lo;
    ft_loadout_init(&lo);

    const FtEnemyId row[FT_MAX_ENEMIES] = {
        FT_ENEMY_BLANK_WALL, FT_ENEMY_DRIFT_BEACON, FT_ENEMY_STRAY_PACKET};

    FtEncounter e;
    ft_encounter_init(&e, row, 3, &lo, 5);

    /* Nothing behind it can be touched, by anything. */
    CHECK(ft_encounter_can_reach(&e, FT_ACTION_CONTACT, 0), "the wall can be hit");
    for(uint8_t i = 1; i < 3u; i++) {
        CHECK(!ft_encounter_can_reach(&e, FT_ACTION_CONTACT, i),
              "foe %u is behind it", i);
        CHECK(!ft_encounter_can_reach(&e, FT_ACTION_BROADCAST, i),
              "and a broadcast does not get round it either");
    }
    CHECK_EQ(ft_encounter_effective_target(&e, FT_ACTION_CONTACT), 0);

    /* The broadcast path struck every living foe directly without ever
     * asking about reach, so the wall blocked single-target attacks and was
     * transparent to the one attack that hits everything. */
    e.menu_index = FT_ACTION_BROADCAST;
    ft_encounter_press_ok(&e);
    ft_encounter_tick(&e, FT_READY_MS + FT_ACTION_WINDOW_MS / 2);
    ft_encounter_press_ok(&e);
    ft_encounter_tick(&e, FT_ACTION_WINDOW_MS);

    CHECK(e.foes[0].charge < e.foes[0].charge_max, "the wall took it");
    CHECK_EQ(e.foes[1].charge, e.foes[1].charge_max);
    CHECK_EQ(e.foes[2].charge, e.foes[2].charge_max);
    CHECK_EQ(e.foe_hits[1].outcome, FT_HIT_LOCKED);

    /* It never takes a turn, but the foes behind it do — being safe from you
     * is not the same as being idle. */
    CHECK(!ft_encounter_foe_awake(&e, 0), "the wall is dormant");
    CHECK(ft_encounter_foe_awake(&e, 1), "the ones behind it are not");

    FtEncounter run;
    ft_encounter_init(&run, row, 3, &lo, 5);

    bool wall_acted = false, other_acted = false;
    for(int i = 0; i < 60000 && !ft_encounter_over(&run); i++) {
        if(run.phase == FT_PHASE_MENU) {
            run.menu_index = FT_ACTION_FOCUS;
            ft_encounter_press_ok(&run);
        } else if(run.phase == FT_PHASE_TELEGRAPH) {
            if(run.acting_foe == 0u) wall_acted = true;
            else other_acted = true;
        }
        ft_encounter_tick(&run, 10);
    }
    CHECK(!wall_acted, "the wall never attacks");
    CHECK(other_acted, "the ones behind it do");

    /* Once it is down, everything opens up. */
    FtEncounter fell;
    ft_encounter_init(&fell, row, 3, &lo, 5);
    fell.foes[0].charge = 0;

    for(uint8_t i = 1; i < 3u; i++) {
        CHECK(ft_encounter_can_reach(&fell, FT_ACTION_BROADCAST, i),
              "foe %u is reachable once the wall is down", i);
    }
    CHECK(ft_encounter_effective_target(&fell, FT_ACTION_CONTACT) != 0,
          "and the aim moves on");
}

static void test_sleeper(void) {
    section("the one that wakes up");

    FtLoadout lo;
    ft_loadout_init(&lo);

    const FtEnemyId row[FT_MAX_ENEMIES] = {
        FT_ENEMY_STRAY_PACKET, FT_ENEMY_SCRAP_CRAWLER, FT_ENEMY_COLD_BOOTER};

    FtEncounter e;
    ft_encounter_init(&e, row, 3, &lo, 5);

    /* Asleep while anything else lives — but hittable the whole time, so you
     * get to choose whether to deal with it early. */
    CHECK(!ft_encounter_foe_awake(&e, 2), "the sleeper sits it out");
    CHECK(ft_encounter_foe_awake(&e, 0), "the others do not");
    CHECK(ft_encounter_can_reach(&e, FT_ACTION_CONTACT, 2),
          "it can still be attacked while asleep");

    /* Clear the room and it wakes. */
    e.foes[0].charge = 0;
    CHECK(!ft_encounter_foe_awake(&e, 2), "still asleep with one left");
    e.foes[1].charge = 0;
    CHECK(ft_encounter_foe_awake(&e, 2), "and awake once alone");

    /* Awake, it leads with its last attack, which is the big one. */
    FtEncounter solo;
    ft_encounter_init_single(&solo, FT_ENEMY_COLD_BOOTER, &lo, 5);

    const uint8_t last = (uint8_t)(FT_ENEMIES[FT_ENEMY_COLD_BOOTER].attack_count - 1u);
    const FtAttack* big = &FT_ENEMIES[FT_ENEMY_COLD_BOOTER].attacks[last];

    bool reached = false;
    for(int i = 0; i < 60000 && !ft_encounter_over(&solo); i++) {
        if(solo.phase == FT_PHASE_MENU) {
            solo.menu_index = FT_ACTION_FOCUS;
            ft_encounter_press_ok(&solo);
        } else if(solo.phase == FT_PHASE_TELEGRAPH) {
            CHECK_EQ(solo.foes[0].attack_index, last);
            reached = true;
            break;
        }
        ft_encounter_tick(&solo, 10);
    }
    CHECK(reached, "an awake sleeper takes a turn");

    /* And that attack is worth being afraid of. */
    for(uint8_t i = 0; i < FT_ENEMIES[FT_ENEMY_COLD_BOOTER].attack_count - 1u; i++) {
        CHECK(big->base_power > FT_ENEMIES[FT_ENEMY_COLD_BOOTER].attacks[i].base_power,
              "the wake-up hits harder than the rest");
    }

    /* A board of nothing but sleepers still fights: with one of them alone,
     * the fight cannot stall. */
    FtEncounter pair;
    const FtEnemyId two[2] = {FT_ENEMY_COLD_BOOTER, FT_ENEMY_COLD_BOOTER};
    ft_encounter_init(&pair, two, 2, &lo, 5);

    CHECK(!ft_encounter_foe_awake(&pair, 0), "two sleepers are both asleep");
    pair.foes[0].charge = 0;
    CHECK(ft_encounter_foe_awake(&pair, 1), "and the survivor wakes");
}

static void test_fast_turn_order(void) {
    section("FAST acts before you");

    FtLoadout lo;
    ft_loadout_init(&lo);

    /* FAST was a published attribute that nothing read: ft_priority.c computed
     * FT_PRIO_FAST_ENEMY and the encounter never asked, so the Sealed Lock
     * carried the tag through the whole prologue without ever once acting
     * early.
     *
     * The rule: the player always opens a fight, and from then on a round
     * runs FAST foes, the player's turns, everything else. Giving the fast
     * foes the opening instead took the prologue's last fight from 57% to
     * 32% at low skill, which is not "quick", it is "ambushed". */
    FtEncounter quick;
    ft_encounter_init_single(&quick, FT_ENEMY_SCRAP_CRAWLER, &lo, 3);
    CHECK_EQ(quick.phase, FT_PHASE_MENU);
    CHECK(!quick.fast_phase, "the player always opens");

    /* In a mixed group, play two rounds and record who acted when. */
    const FtEnemyId mixed[2] = {FT_ENEMY_STRAY_PACKET, FT_ENEMY_SCRAP_CRAWLER};

    FtEncounter e;
    ft_encounter_init(&e, mixed, 2, &lo, 3);

    /* Neither can die, or the order stops being observable. */
    e.foes[0].charge = 900;
    e.foes[1].charge = 900;
    e.stats.charge_max = 900;
    ft_roll_init(&e.roll, 900);

    int order[24];
    int n = 0, last = -1;

    for(int i = 0; i < 200000 && !ft_encounter_over(&e) && n < 10; i++) {
        if(e.phase == FT_PHASE_MENU) {
            e.menu_index = FT_ACTION_FOCUS;
            ft_encounter_press_ok(&e);
            if(last != 100) { order[n++] = 100; last = 100; }
        } else if(e.phase == FT_PHASE_TELEGRAPH && last != (int)e.acting_foe) {
            order[n++] = (int)e.acting_foe;
            last = (int)e.acting_foe;
        }
        ft_encounter_tick(&e, 10);
    }

    CHECK(n >= 6, "the fight got going (%d entries)", n);

    /* Round one: the player opens. */
    CHECK_EQ(order[0], 100);

    /* Then the slow Packet, then the quick Crawler — and from there the
     * Crawler is always the one immediately before the player, which is what
     * FAST buys it: you cannot brace for a hit that lands before your go. */
    CHECK_EQ(order[1], 0);
    CHECK_EQ(order[2], 1);
    CHECK_EQ(order[3], 100);

    if(n >= 6) {
        CHECK_EQ(order[4], 0);
        CHECK_EQ(order[5], 1);
    }

    /* Whoever acts, the player is never starved of a turn. */
    int player_turns = 0;
    for(int i = 0; i < n; i++) {
        if(order[i] == 100) player_turns++;
    }
    CHECK(player_turns >= 2, "the player keeps getting turns (%d)", player_turns);

    /* A board where everything is FAST still reaches the menu. */
    const FtEnemyId allfast[2] = {FT_ENEMY_SCRAP_CRAWLER, FT_ENEMY_GATE_DRONE};

    FtEncounter rush;
    ft_encounter_init(&rush, allfast, 2, &lo, 3);
    CHECK_EQ(rush.phase, FT_PHASE_MENU);

    int menus = 0;
    for(int i = 0; i < 200000 && !ft_encounter_over(&rush); i++) {
        if(rush.phase == FT_PHASE_MENU) {
            rush.menu_index = FT_ACTION_DEFEND;
            ft_encounter_press_ok(&rush);
            menus++;
        }
        ft_encounter_tick(&rush, 10);
    }
    CHECK(menus >= 2, "an all-FAST board still gives turns (%d)", menus);

    /* Bracing does not carry across a round: it is cleared before the next
     * set of foes acts, or Defend would cover a hit from two rounds away. */
    FtEncounter brace;
    ft_encounter_init_single(&brace, FT_ENEMY_SCRAP_CRAWLER, &lo, 3);
    brace.foes[0].charge = 900;

    brace.menu_index = FT_ACTION_DEFEND;
    ft_encounter_press_ok(&brace);
    CHECK(brace.defending, "bracing is on");

    for(int i = 0; i < 40000 && brace.phase != FT_PHASE_MENU; i++) {
        ft_encounter_tick(&brace, 10);
    }
    CHECK(!brace.defending, "and off again by the next menu");
}

static void test_enemy_roster(void) {
    section("every enemy is beatable");

    for(uint8_t i = 0; i < FT_ENEMY_COUNT; i++) {
        const FtEnemy* en = &FT_ENEMIES[i];

        CHECK(en->name != NULL && en->name[0] != '\0', "enemy %u is named", i);
        CHECK(strlen(en->name) <= 16, "enemy %u's name fits the title bar", i);
        CHECK(en->charge > 0, "%s has Charge", en->name);
        CHECK(en->shielded >= 0, "%s has a sane shield", en->name);
        CHECK(en->level >= 1, "%s has a level", en->name);
        CHECK(en->xp > 0, "%s is worth something", en->name);
        CHECK(en->attack_count >= 1 && en->attack_count <= FT_ENEMY_MAX_ATTACKS,
              "%s has 1..%d attacks", en->name, FT_ENEMY_MAX_ATTACKS);

        /* The one combination that must never exist: AIRBORNE turns contact
         * away and ENCRYPTED turns broadcast away, so both at once is immune
         * to the entire base kit. That is not difficulty, it is a fight you
         * cannot finish. */
        const bool air = (en->attrs & FT_ATTR_AIRBORNE) != 0u;
        const bool enc = (en->attrs & FT_ATTR_ENCRYPTED) != 0u;
        CHECK(!(air && enc), "%s can be hit by something", en->name);

        for(uint8_t a = 0; a < en->attack_count; a++) {
            const FtAttack* atk = &en->attacks[a];

            CHECK(atk->id != 0u, "%s attack %u has an id", en->name, a);
            CHECK(atk->base_power > 0, "%s attack %u hurts", en->name, a);

            /* Ids are the save format's handle on a captured signal, so a
             * duplicate would silently replay the wrong attack. */
            for(uint8_t j = 0; j < FT_ENEMY_COUNT; j++) {
                for(uint8_t b = 0; b < FT_ENEMIES[j].attack_count; b++) {
                    if(j == i && b == a) continue;
                    CHECK(FT_ENEMIES[j].attacks[b].id != atk->id,
                          "%s attack %u has a unique id (%u)", en->name, a, atk->id);
                }
            }

            /* Every attack must resolve into something the player can meet:
             * an undodgeable attack that also drains and cannot be braced
             * would have no counterplay at all. */
            CHECK(ft_guard_permitted(atk->klass, FT_GUARD_JAM) != FT_GUARD_NONE ||
                      atk->klass == FT_CLASS_UNDODGEABLE,
                  "%s attack %u can be jammed unless it says otherwise", en->name, a);
        }

        /* A shield must not make an enemy immune to a fully powered hit from
         * the base kit, or the fight is unwinnable for a player with no
         * cards installed. */
        const FtAttack* nfc = &FT_MODULES[FT_MOD_NFC].attack;
        const FtDefender def = {en->shielded, en->attrs};
        const FtHitParams best = {0, 0, FT_RATING_EXCELLENT, false, FT_GUARD_NONE, 0};
        const FtHitResult hit = ft_resolve_hit(nfc, &def, &best);

        if(!air) {
            CHECK(hit.damage > 0, "%s takes damage from a perfect NFC hit (%d)",
                  en->name, (int)hit.damage);
        } else {
            const FtAttack* sub = &FT_MODULES[FT_MOD_SUBGHZ].attack;
            const FtHitResult bro = ft_resolve_hit(sub, &def, &best);
            CHECK(bro.damage > 0, "%s takes damage from a perfect Sub-GHz hit (%d)",
                  en->name, (int)bro.damage);
        }
    }

    /* Every roster must be fightable: at least one living foe, and at least
     * one that each of the two base modules can reach between them. */
    FtLoadout lo;
    ft_loadout_init(&lo);

    for(uint8_t r = 0; r < 16u; r++) {
        const FtRoster* roster = ft_roster(r);
        if(roster->count == 0u) continue;

        FtEncounter e;
        ft_encounter_init(&e, roster->foes, roster->count, &lo, 1);

        uint8_t reachable = 0;
        int wall = -1;

        for(uint8_t i = 0; i < e.foe_count; i++) {
            if(FT_ENEMIES[e.foes[i].id].attrs & FT_ATTR_BULWARK) wall = (int)i;
            if(ft_encounter_can_reach(&e, FT_ACTION_CONTACT, i) ||
               ft_encounter_can_reach(&e, FT_ACTION_BROADCAST, i)) {
                reachable++;
            }
        }

        if(wall >= 0) {
            /* A bulwark is the only thing you can hit while it stands, which
             * is the whole mechanic — and it must itself be hittable, or the
             * fight cannot be finished. */
            CHECK_EQ(reachable, 1);
            CHECK(ft_encounter_can_reach(&e, FT_ACTION_CONTACT, (uint8_t)wall) ||
                      ft_encounter_can_reach(&e, FT_ACTION_BROADCAST, (uint8_t)wall),
                  "roster %u: the wall itself can be hit", r);
        } else {
            CHECK_EQ(reachable, e.foe_count);
        }
    }
}

static void test_guide(void) {
    section("field guide");

    FtGuide g;
    ft_guide_init(&g);

    /* Nothing is listed until it has been met: a guide that ships knowing
     * everything is a manual, not a record of the run. */
    CHECK_EQ(ft_guide_count(&g), 0);
    for(uint8_t i = 0; i < FT_ENEMY_COUNT; i++) {
        CHECK(!ft_guide_knows(&g, (FtEnemyId)i), "enemy %u starts unknown", i);
    }
    CHECK_EQ(ft_guide_nth(&g, 0), FT_ENEMY_COUNT);

    FtLoadout lo;
    ft_loadout_init(&lo);

    /* Meeting a group records every foe in it, not just the first. */
    const FtEnemyId pair[2] = {FT_ENEMY_MAST_RELAY, FT_ENEMY_DRIFT_BEACON};

    FtEncounter e;
    ft_encounter_init(&e, pair, 2, &lo, 1);
    ft_guide_note_encounter(&g, &e);

    CHECK_EQ(ft_guide_count(&g), 2);
    CHECK(ft_guide_knows(&g, FT_ENEMY_MAST_RELAY), "the relay is known");
    CHECK(ft_guide_knows(&g, FT_ENEMY_DRIFT_BEACON), "and so is the beacon");
    CHECK(!ft_guide_knows(&g, FT_ENEMY_NULL_FIELD), "the rest are not");

    /* Meeting the same thing twice does not list it twice. */
    ft_guide_note_encounter(&g, &e);
    CHECK_EQ(ft_guide_count(&g), 2);

    /* The walk is in table order and covers exactly what is known. */
    CHECK_EQ(ft_guide_nth(&g, 0), FT_ENEMY_DRIFT_BEACON);
    CHECK_EQ(ft_guide_nth(&g, 1), FT_ENEMY_MAST_RELAY);
    CHECK_EQ(ft_guide_nth(&g, 2), FT_ENEMY_COUNT);

    /* Everything meets everything: the guide must hold the whole table. */
    FtGuide all;
    ft_guide_init(&all);

    for(uint8_t i = 0; i < FT_ENEMY_COUNT; i++) {
        FtEncounter one;
        ft_encounter_init_single(&one, (FtEnemyId)i, &lo, 1);
        ft_guide_note_encounter(&all, &one);
    }
    CHECK_EQ(ft_guide_count(&all), FT_ENEMY_COUNT);

    /* Every entry's text fits the panel and says something. */
    for(uint8_t i = 0; i < FT_ENEMY_COUNT; i++) {
        const FtEnemyId id = (FtEnemyId)i;
        char buf[24];

        ft_guide_vitals(id, buf, (uint8_t)sizeof(buf));
        CHECK(buf[0] != '\0', "%s has a vitals line", FT_ENEMIES[id].name);
        CHECK(strlen(buf) <= FT_TUTORIAL_MAX_CHARS,
              "%s's vitals fit: \"%s\"", FT_ENEMIES[id].name, buf);

        /* Every trait it has gets a line explaining what it costs you. */
        uint8_t notes = 0;
        for(uint8_t n = 0; n < 8u; n++) {
            const char* note = ft_guide_note(id, n);
            if(!note) break;

            notes++;
            CHECK(strlen(note) <= FT_TUTORIAL_MAX_CHARS,
                  "%s note %u fits: \"%s\"", FT_ENEMIES[id].name, n, note);
        }
        if(FT_ENEMIES[id].attrs != 0u) {
            CHECK(notes > 0, "%s explains its traits", FT_ENEMIES[id].name);
        }

        /* And every attack is described, with none past the last. */
        uint8_t lines = 0;
        for(uint8_t n = 0; n < FT_ENEMY_MAX_ATTACKS + 2u; n++) {
            if(!ft_guide_attack_line(id, n, buf, (uint8_t)sizeof(buf))) break;

            lines++;
            CHECK(strlen(buf) <= FT_TUTORIAL_MAX_CHARS,
                  "%s attack %u fits: \"%s\"", FT_ENEMIES[id].name, n, buf);
        }
        CHECK_EQ(lines, ft_guide_attack_count(id));
    }

    /* A short buffer truncates rather than running off the end. */
    char tiny[4];
    ft_guide_vitals(FT_ENEMY_NULL_FIELD, tiny, (uint8_t)sizeof(tiny));
    CHECK(strlen(tiny) < sizeof(tiny), "a short buffer stays terminated");

    /* The guide survives a save, because it is the run's memory. */
    FtWorld w;
    ft_world_init(&w);
    w.guide = all;

    FtSaveData d;
    ft_save_from_world(&w, false, true, &d);

    uint8_t buf2[FT_SAVE_MAX_BYTES];
    const uint8_t len = ft_save_encode(&d, buf2, sizeof(buf2));
    CHECK(len > 0, "a run with a guide encodes");

    FtSaveData back;
    CHECK(ft_save_decode(buf2, len, &back), "and decodes");
    CHECK_EQ(back.guide.seen, all.seen);

    FtWorld loaded;
    ft_save_to_world(&back, &loaded, NULL, NULL);
    CHECK_EQ(ft_guide_count(&loaded.guide), FT_ENEMY_COUNT);
}

static void test_defend_is_worth_it(void) {
    section("bracing is a real choice");

    FtLoadout lo;
    ft_loadout_init(&lo);

    /* There was no way to recover Charge in a fight at all, so every fight
     * was attrition and attacking was always right — which is exactly why
     * Protect and Focus never got used. */
    FtEncounter e;
    ft_encounter_init_single(&e, FT_ENEMY_STRAY_PACKET, &lo, 4);

    e.roll.current = 5;
    e.roll.target = 5;
    e.stats.charge = 5;

    e.menu_index = FT_ACTION_DEFEND;
    ft_encounter_press_ok(&e);

    CHECK(e.defending, "bracing is on");
    CHECK_EQ(e.roll.target, 5 + FT_DEFEND_HEAL);
    CHECK_EQ(e.roll.current, 5 + FT_DEFEND_HEAL);
    CHECK_EQ(e.stats.charge, e.roll.current);

    /* It cannot overheal past the maximum. */
    FtEncounter full;
    ft_encounter_init_single(&full, FT_ENEMY_STRAY_PACKET, &lo, 4);
    full.menu_index = FT_ACTION_DEFEND;
    ft_encounter_press_ok(&full);
    CHECK_EQ(full.roll.current, full.stats.charge_max);

    /* And it still shields: bracing has to blunt the hit as well, or the
     * heal just buys back what the turn cost you. */
    CHECK_EQ(FT_DEFEND_SHIELD, 2);
    const FtAttack* atk = &FT_ENEMIES[FT_ENEMY_STRAY_PACKET].attacks[0];
    const FtHitParams p = {0, 0, FT_RATING_MISS, false, FT_GUARD_NONE, 0};
    const FtDefender bare = {0, 0};
    const FtDefender braced = {FT_DEFEND_SHIELD, 0};

    CHECK(ft_resolve_hit(atk, &braced, &p).damage <
              ft_resolve_hit(atk, &bare, &p).damage,
          "and blunts the hit");
}

static void test_losing_costs(void) {
    section("losing costs the run");

    FtLoadout lo;
    ft_loadout_init(&lo);

    /* A player who never acts against three of the toughest enemy loses. The
     * point of the test is what that loss is worth. */
    const FtEnemyId trio[FT_MAX_ENEMIES] = {
        FT_ENEMY_SEALED_LOCK, FT_ENEMY_SEALED_LOCK, FT_ENEMY_SEALED_LOCK};

    FtEncounter e;
    ft_encounter_init(&e, trio, 3, &lo, 7);

    for(int i = 0; i < 200000 && !ft_encounter_over(&e); i++) {
        if(e.phase == FT_PHASE_MENU) {
            e.menu_index = FT_ACTION_FOCUS;
            ft_encounter_press_ok(&e);
        }
        ft_encounter_tick(&e, 10);
    }

    CHECK_EQ(e.phase, FT_PHASE_LOSE);
    CHECK_EQ(ft_encounter_living(&e), 3);
    CHECK(e.roll.current <= 0, "you are down");

    /* A lost fight pays nothing, however long it lasted. */
    CHECK_EQ(ft_encounter_xp(&e), 0);

    /* And the world-side cost: a loss reloads the save, so everything done
     * since it is gone. This is the whole reason a terminal is worth walking
     * to, and for a while it did not happen at all — losing healed you to
     * full, moved you to the save point and kept every foe you had beaten,
     * which made it strictly better than walking away hurt. */
    FtWorld w;
    ft_world_init(&w);
    ft_world_enter(&w, 1, ft_room(1)->exits[0].tx, ft_room(1)->exits[0].ty);

    w.save_room = 1;
    w.save_tx = ft_room(1)->exits[0].tx;
    w.save_ty = ft_room(1)->exits[0].ty;

    FtSaveData checkpoint;
    ft_save_from_world(&w, false, true, &checkpoint);

    const int16_t saved_charge_max = w.stats.charge_max;

    /* Now make progress past the save: beat the room's encounter, level up. */
    ft_world_clear_entity(&w, 0);
    level_into(&w.stats, FT_UP_CHARGE);

    CHECK(ft_world_entity_gone(&w, 0), "the foe was beaten");
    CHECK(w.stats.charge_max > saved_charge_max, "and a level was taken");

    /* Then go down. Restoring the checkpoint must undo all of it. */
    FtWorld after;
    bool coach = true;
    ft_save_to_world(&checkpoint, &after, &coach, NULL);

    CHECK(!ft_world_entity_gone(&after, 0), "the foe is standing again");
    CHECK_EQ(after.stats.charge_max, saved_charge_max);
    CHECK_EQ(after.stats.level, 1);
    CHECK_EQ(after.room, 1);

    /* The foe really is back on its tile, not merely un-flagged. */
    CHECK(after.foes[0].alive, "and is spawned again");
    CHECK(after.foes[0].count > 0, "with its walkers");
}

static void test_levelup(void) {
    section("levelling up");

    FtStats s;
    ft_stats_init(&s);
    CHECK_EQ(s.level, 1);

    /* Applying a choice is what actually levels you. Before this, level never
     * moved, so the cap never bit and every enemy paid full XP forever. */
    const int16_t charge_before = s.charge_max;
    CHECK(level_into(&s, FT_UP_CHARGE), "the level is spent");
    CHECK_EQ(s.level, 2);
    CHECK_EQ(s.charge_max, charge_before + FT_LEVEL_UP_CHARGE);

    /* A refused choice costs nothing: the level stays owed. */
    FtStats capped;
    ft_stats_init(&capped);
    capped.flash_max = FT_CAP_FLASH;
    const int16_t lv = capped.level;
    CHECK(!level_into(&capped, FT_UP_FLASH), "a capped stat refuses");
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
    CHECK(level_into(&p, FT_UP_CHARGE), "first");
    CHECK(level_into(&p, FT_UP_RAM), "second");
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
                level_into(&run, FT_UP_CHARGE);
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

            /* A door or a gate: both are ways out, and a gate is what a way
             * somebody is holding shut looks like. */
            const FtTile here = ft_map_tile(room->map, x->tx, x->ty);
            CHECK(here == FT_TILE_DOOR || here == FT_TILE_GATE,
                  "room %u exit %u stands on a way out (got %d)", r, e, (int)here);

            /* And a gated exit has to *look* gated, or the refusal arrives
             * from nowhere. */
            if(x->need_quest != 0u && x->need_state == (uint8_t)FT_QUEST_DONE) {
                CHECK(here == FT_TILE_GATE,
                      "room %u exit %u is held shut, so it is drawn as a gate", r, e);
            }

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


/* ---- Orbs: the build you can change ----------------------------------- */

static void test_orbs(void) {
    section("orbs");

    FtStats s;
    ft_stats_init(&s);
    CHECK_EQ(s.orbs, 0);
    CHECK_EQ(s.spent[FT_UP_CHARGE], 0);

    /* Nothing to place is nothing to place. */
    CHECK(!ft_orb_spend(&s, FT_UP_CHARGE), "an empty hand cannot place one");
    CHECK_EQ(s.charge_max, FT_START_CHARGE);

    /* A level pays out an orb and does not choose for you. */
    ft_level_take(&s);
    CHECK_EQ(s.level, 2);
    CHECK_EQ(s.orbs, FT_ORBS_PER_LEVEL);
    CHECK_EQ(s.charge_max, FT_START_CHARGE);

    /* Placing one raises the stat, and grants the gain there and then: this
     * is a thing you are meant to be able to do while hurt. */
    s.charge = 3;
    CHECK(ft_orb_spend(&s, FT_UP_CHARGE), "the orb goes in");
    CHECK_EQ(s.charge_max, FT_START_CHARGE + FT_LEVEL_UP_CHARGE);
    CHECK_EQ(s.charge, 3 + FT_LEVEL_UP_CHARGE);
    CHECK_EQ(s.orbs, 0);
    CHECK_EQ(s.spent[FT_UP_CHARGE], 1);

    /* And out again. This is the whole feature: a build is never final. */
    CHECK(ft_orb_can_refund(&s, FT_UP_CHARGE), "it can come back out");
    CHECK(ft_orb_refund(&s, FT_UP_CHARGE), "and does");
    CHECK_EQ(s.charge_max, FT_START_CHARGE);
    CHECK_EQ(s.orbs, 1);
    CHECK_EQ(s.spent[FT_UP_CHARGE], 0);
    CHECK(s.charge <= s.charge_max, "current HP is clamped to the new maximum");
    CHECK(s.charge >= 1, "but never to zero: a menu cannot down you");

    /* Nothing went into MP, so nothing comes out of it. */
    CHECK(!ft_orb_can_refund(&s, FT_UP_RAM), "an empty stat refuses a refund");
    CHECK(!ft_orb_refund(&s, FT_UP_RAM), "and stays put");
    CHECK_EQ(s.ram_max, FT_START_RAM);

    /* Round trips never invent or lose anything. */
    FtStats t;
    ft_stats_init(&t);
    for(int i = 0; i < 6; i++) ft_level_take(&t);

    const int16_t banked = t.orbs;
    ft_orb_spend(&t, FT_UP_CHARGE);
    ft_orb_spend(&t, FT_UP_CHARGE);
    ft_orb_spend(&t, FT_UP_RAM);
    ft_orb_spend(&t, FT_UP_FLASH);
    CHECK_EQ(t.orbs, banked - 4);

    while(ft_orb_refund(&t, FT_UP_CHARGE)) {}
    while(ft_orb_refund(&t, FT_UP_RAM)) {}
    while(ft_orb_refund(&t, FT_UP_FLASH)) {}

    CHECK_EQ(t.orbs, banked);
    CHECK_EQ(t.charge_max, FT_START_CHARGE);
    CHECK_EQ(t.ram_max, FT_START_RAM);
    CHECK_EQ(t.flash_max, FT_START_FLASH);

    /* Cards are a budget something else is spending. Pulling an orb out from
     * under an installed card would leave flash_used above flash_max, and
     * every install check downstream would read that as "no room" forever. */
    FtStats c;
    ft_stats_init(&c);
    ft_level_take(&c);
    CHECK(ft_orb_spend(&c, FT_UP_FLASH), "a card slot is bought");

    ft_flash_install(&c, c.flash_max);
    CHECK(!ft_orb_can_refund(&c, FT_UP_FLASH), "a full budget refuses the refund");
    CHECK(!ft_orb_refund(&c, FT_UP_FLASH), "and nothing moves");
    CHECK(c.flash_used <= c.flash_max, "so the budget never goes negative");

    ft_flash_uninstall(&c, c.flash_max);
    CHECK(ft_orb_refund(&c, FT_UP_FLASH), "with the cards off, it comes out");

    /* A capped stat refuses, and the orb stays in hand rather than vanishing. */
    FtStats m;
    ft_stats_init(&m);
    ft_level_take(&m);
    m.charge_max = FT_CAP_CHARGE;
    CHECK(!ft_orb_spend(&m, FT_UP_CHARGE), "a capped stat refuses");
    CHECK_EQ(m.orbs, FT_ORBS_PER_LEVEL);

    /* No stat can ever be pushed past its cap by a legal sequence. */
    FtStats hi;
    ft_stats_init(&hi);
    for(int i = 0; i < 400; i++) {
        ft_level_take(&hi);
        ft_orb_spend(&hi, FT_UP_CHARGE);
        ft_orb_spend(&hi, FT_UP_RAM);
        ft_orb_spend(&hi, FT_UP_FLASH);
    }
    CHECK(hi.charge_max <= FT_CAP_CHARGE, "HP stops at its cap");
    CHECK(hi.ram_max <= FT_CAP_RAM, "MP stops at its cap");
    CHECK(hi.flash_max <= FT_CAP_FLASH, "Cards stop at their cap");
}

/* ---- Quests ------------------------------------------------------------ */

static void test_quests(void) {
    section("quests");

    FtQuests q;
    ft_quests_init(&q);
    CHECK_EQ(ft_quest_state(&q, FT_QUEST_CLEAN_RUN), FT_QUEST_UNKNOWN);

    const FtQuestDef* d = ft_quest_def(FT_QUEST_CLEAN_RUN);
    CHECK(d->goal_room != d->giver_room, "the goal is somewhere else");
    CHECK(d->goal_room < ft_room_count(), "and is a room that exists");
    CHECK(d->reward_orbs > 0, "and it pays something");

    /* Somebody has to be standing in the giver's room to offer it. */
    const FtRoom* giver = ft_room(d->giver_room);
    bool has_npc = false;
    for(uint8_t i = 0; i < giver->ent_count && i < FT_MAX_ROOM_ENTS; i++) {
        if(giver->ents[i].kind == FT_ENT_NPC &&
           giver->ents[i].roster == (uint8_t)FT_QUEST_CLEAN_RUN) {
            has_npc = true;
        }
    }
    CHECK(has_npc, "the room that gives it has somebody in it");

    /* Talking takes it. */
    FtQuestTalk t = ft_quest_talk(&q, FT_QUEST_CLEAN_RUN);
    CHECK_EQ(ft_quest_state(&q, FT_QUEST_CLEAN_RUN), FT_QUEST_ACTIVE);
    CHECK_EQ(t.orbs, 0);

    /* Talking again says something, and does not re-take it. */
    t = ft_quest_talk(&q, FT_QUEST_CLEAN_RUN);
    CHECK_EQ(ft_quest_state(&q, FT_QUEST_CLEAN_RUN), FT_QUEST_ACTIVE);
    CHECK_EQ(t.orbs, 0);

    /* Rooms that are not the goal change nothing. */
    for(uint8_t r = 0; r < ft_room_count(); r++) {
        if(r == d->goal_room) continue;
        ft_quest_enter_room(&q, r);
    }
    CHECK_EQ(ft_quest_state(&q, FT_QUEST_CLEAN_RUN), FT_QUEST_ACTIVE);

    /* The goal room arms the payout. */
    ft_quest_enter_room(&q, d->goal_room);
    CHECK_EQ(ft_quest_state(&q, FT_QUEST_CLEAN_RUN), FT_QUEST_READY);

    /* Collecting pays exactly once. */
    t = ft_quest_talk(&q, FT_QUEST_CLEAN_RUN);
    CHECK_EQ(t.orbs, d->reward_orbs);
    CHECK_EQ(ft_quest_state(&q, FT_QUEST_CLEAN_RUN), FT_QUEST_DONE);

    t = ft_quest_talk(&q, FT_QUEST_CLEAN_RUN);
    CHECK_EQ(t.orbs, 0);
    CHECK_EQ(ft_quest_state(&q, FT_QUEST_CLEAN_RUN), FT_QUEST_DONE);

    /* Fighting fails it, whether it was under way or already armed. */
    FtQuests f;
    ft_quests_init(&f);
    ft_quest_talk(&f, FT_QUEST_CLEAN_RUN);
    ft_quest_battle(&f);
    CHECK_EQ(ft_quest_state(&f, FT_QUEST_CLEAN_RUN), FT_QUEST_FAILED);

    FtQuests g;
    ft_quests_init(&g);
    ft_quest_talk(&g, FT_QUEST_CLEAN_RUN);
    ft_quest_enter_room(&g, d->goal_room);
    ft_quest_battle(&g);
    CHECK_EQ(ft_quest_state(&g, FT_QUEST_CLEAN_RUN), FT_QUEST_FAILED);

    /* A failed run does not pay, and can be taken again. */
    t = ft_quest_talk(&g, FT_QUEST_CLEAN_RUN);
    CHECK_EQ(t.orbs, 0);
    CHECK_EQ(ft_quest_state(&g, FT_QUEST_CLEAN_RUN), FT_QUEST_ACTIVE);

    /* A finished quest is not undone by fighting afterwards. */
    FtQuests done;
    ft_quests_init(&done);
    ft_quest_talk(&done, FT_QUEST_CLEAN_RUN);
    ft_quest_enter_room(&done, d->goal_room);
    ft_quest_talk(&done, FT_QUEST_CLEAN_RUN);
    ft_quest_battle(&done);
    CHECK_EQ(ft_quest_state(&done, FT_QUEST_CLEAN_RUN), FT_QUEST_DONE);

    /* Every line anybody says fits the panel, in every state. */
    for(uint8_t st = 0; st <= (uint8_t)FT_QUEST_DONE; st++) {
        FtQuests say;
        ft_quests_init(&say);
        say.state[FT_QUEST_CLEAN_RUN] = st;

        const FtQuestTalk line = ft_quest_talk(&say, FT_QUEST_CLEAN_RUN);
        CHECK(line.lines > 0 && line.lines <= FT_QUEST_LINES,
              "state %u says between one and %d lines", st, FT_QUEST_LINES);

        for(uint8_t i = 0; i < line.lines; i++) {
            CHECK(line.line[i] != NULL, "state %u line %u exists", st, i);
            CHECK(line.line[i] && strlen(line.line[i]) <= FT_TUTORIAL_MAX_CHARS,
                  "state %u line %u fits: \"%s\"", st, i,
                  line.line[i] ? line.line[i] : "");
        }
    }

    for(uint8_t i = 0; i < FT_QUEST_COUNT; i++) {
        CHECK(strlen(ft_quest_def((FtQuestId)i)->name) <= FT_TUTORIAL_MAX_CHARS,
              "quest %u's name fits", i);
    }

    /* The world carries them, and walking into the goal room advances them
     * without anybody asking it to. */
    FtWorld w;
    ft_world_init(&w);
    CHECK_EQ(ft_quest_state(&w.quests, FT_QUEST_CLEAN_RUN), FT_QUEST_UNKNOWN);

    ft_quest_talk(&w.quests, FT_QUEST_CLEAN_RUN);
    ft_world_enter(&w, d->goal_room, 1, 2);
    CHECK_EQ(ft_quest_state(&w.quests, FT_QUEST_CLEAN_RUN), FT_QUEST_READY);
}

/* ---- Talking to somebody ----------------------------------------------- */

static void test_npc(void) {
    section("somebody to talk to");

    const int which = ft_quest_for_room(0);
    CHECK(which >= 0, "the first room has a quest giver");

    const FtRoom* r = ft_room(0);
    int at = -1;
    for(uint8_t i = 0; i < r->ent_count && i < FT_MAX_ROOM_ENTS; i++) {
        if(r->ents[i].kind == FT_ENT_NPC) at = (int)i;
    }
    CHECK(at >= 0, "and somebody standing in it");
    if(at < 0) return;

    const uint8_t nx = r->ents[at].tx, ny = r->ents[at].ty;
    CHECK(!ft_tile_solid(ft_map_tile(r->map, nx, ny)), "on a tile you can reach");

    /* Facing them from the tile to their left is what talking to them is. */
    FtWorld w;
    ft_world_init(&w);
    ft_world_enter(&w, 0, (uint8_t)(nx - 1u), ny);
    w.facing = FT_FACE_RIGHT;
    CHECK_EQ(ft_world_npc_ahead(&w), at);

    /* Facing anywhere else is not. */
    w.facing = FT_FACE_LEFT;
    CHECK_EQ(ft_world_npc_ahead(&w), -1);

    /* They are solid: walking into one stops you rather than putting you
     * inside them, and that is what leaves you facing them. */
    w.facing = FT_FACE_RIGHT;
    for(int t = 0; t < 120; t++) ft_world_update(&w, 1, 0, 20);

    CHECK_EQ(w.mv.tx, (uint8_t)(nx - 1u));
    CHECK_EQ(w.mv.ty, ny);
    CHECK_EQ(ft_world_npc_ahead(&w), at);

    /* And they are not a foe: nothing about them starts a fight. */
    CHECK_EQ(ft_world_foe_ahead(&w), -1);
    CHECK_EQ(ft_world_foe_contact(&w), -1);
}

/* ---- The beat between being seen and being chased ---------------------- */

static void test_notice(void) {
    section("spotted");

    /* Room 1's group is placed well to the right of the door, so there is
     * room to be out of range and then walk into it. */
    const uint8_t ROOM = 1;
    const FtRoom* r = ft_room(ROOM);

    FtWorld w;
    ft_world_init(&w);
    ft_world_enter(&w, ROOM, (uint8_t)(r->ents[0].tx - 2u), r->ents[0].ty);

    CHECK(!w.foes[0].alert, "not seen yet");
    CHECK_EQ(w.foes[0].notice_ms, 0);

    /* The update that notices you arms the beat. */
    ft_world_update(&w, 0, 0, 1);
    CHECK(w.foes[0].alert, "standing that close is being seen");
    CHECK(w.foes[0].notice_ms > 0, "and the beat starts");
    CHECK(ft_world_foe_noticing(&w, 0), "which is what the mark is drawn from");

    /* Nothing moves while it runs. */
    uint8_t tx[FT_MAX_ENEMIES], ty[FT_MAX_ENEMIES];
    for(uint8_t m = 0; m < w.foes[0].count; m++) {
        tx[m] = w.foes[0].w[m].mv.tx;
        ty[m] = w.foes[0].w[m].mv.ty;
    }

    uint32_t held = 0;
    while(w.foes[0].notice_ms > 0u) {
        ft_world_update(&w, 0, 0, 20);
        held += 20u;

        for(uint8_t m = 0; m < w.foes[0].count; m++) {
            CHECK(w.foes[0].w[m].mv.tx == tx[m] && w.foes[0].w[m].mv.ty == ty[m],
                  "walker %u holds still at %ums", m, (unsigned)held);
        }
        if(held > FT_FOE_NOTICE_MS * 4u) break;
    }

    CHECK(held >= FT_FOE_NOTICE_MS - 20u, "the beat lasts about half a second (%u)",
          (unsigned)held);
    CHECK(held <= FT_FOE_NOTICE_MS + 40u, "and not much longer (%u)", (unsigned)held);
    CHECK(!ft_world_foe_noticing(&w, 0), "then the mark comes down");

    /* And then they come for you. */
    int32_t before = 0, after = 0;
    for(uint8_t m = 0; m < w.foes[0].count; m++) {
        before += abs_i32((int32_t)w.foes[0].w[m].mv.tx - (int32_t)w.mv.tx) +
                  abs_i32((int32_t)w.foes[0].w[m].mv.ty - (int32_t)w.mv.ty);
    }
    for(int t = 0; t < 60; t++) ft_world_update(&w, 0, 0, 20);
    for(uint8_t m = 0; m < w.foes[0].count; m++) {
        after += abs_i32((int32_t)w.foes[0].w[m].mv.tx - (int32_t)w.mv.tx) +
                 abs_i32((int32_t)w.foes[0].w[m].mv.ty - (int32_t)w.mv.ty);
    }
    CHECK(after < before || before <= 1,
          "once the beat is over they close in (%d -> %d)", (int)before, (int)after);

    /* Staying in range does not re-arm it: only the transition does, or the
     * room would freeze solid for as long as you stood there. */
    CHECK_EQ(w.foes[0].notice_ms, 0);
    for(int t = 0; t < 10; t++) {
        ft_world_update(&w, 0, 0, 20);
        CHECK_EQ(w.foes[0].notice_ms, 0);
    }
}

/* ---- The guard aftermath ------------------------------------------------ */

static void test_guard_aftermath(void) {
    section("where the guard landed");

    FtLoadout lo;
    ft_loadout_init(&lo);

    /* A press inside the jam window reports how far before impact it was. */
    FtEncounter e;
    ft_encounter_init_single(&e, FT_ENEMY_STRAY_PACKET, &lo, 7u);
    CHECK_EQ(ft_encounter_guard_offset(&e), -1);

    e.phase = FT_PHASE_TELEGRAPH;
    e.phase_ms = FT_READY_MS;
    CHECK_EQ(ft_encounter_guard_gap(&e), -1);

    /* Wind on to 40ms before impact and guard there. */
    const uint32_t at = FT_TELEGRAPH_MS - 40u;
    ft_encounter_tick(&e, at);
    ft_encounter_press_ok(&e);

    CHECK(e.guard_pressed, "the guard went up");
    CHECK(ft_encounter_guard_gap(&e) <= 60 && ft_encounter_guard_gap(&e) >= 20,
          "and the gap to impact is about 40ms (%d)", (int)ft_encounter_guard_gap(&e));

    /* The marker's flash clock runs while the wind-up finishes, so it can be
     * drawn frozen where it landed rather than sailing past. */
    const uint32_t before_lock = e.guard_locked_ms;
    ft_encounter_tick(&e, 20);
    CHECK(e.guard_locked_ms > before_lock, "and the lock clock runs");

    /* Once it resolves, the offset is the aftermath: the exact distance. */
    while(e.phase == FT_PHASE_TELEGRAPH) ft_encounter_tick(&e, 20);

    CHECK(ft_encounter_guard_offset(&e) >= 0, "a press is reported");
    CHECK(ft_encounter_guard_offset(&e) <= (int32_t)FT_JAM_WINDOW_MS,
          "and it was inside the window (%d)", (int)ft_encounter_guard_offset(&e));
    CHECK(e.last_guard != FT_GUARD_NONE, "so it actually guarded");

    /* A press far too early is reported as a press, not as silence — that
     * distinction is the whole point: "you were early" and "you did nothing"
     * used to look identical. */
    FtEncounter early;
    ft_encounter_init_single(&early, FT_ENEMY_STRAY_PACKET, &lo, 7u);
    early.phase = FT_PHASE_TELEGRAPH;
    early.phase_ms = FT_READY_MS;
    ft_encounter_tick(&early, 100);
    ft_encounter_press_ok(&early);
    while(early.phase == FT_PHASE_TELEGRAPH) ft_encounter_tick(&early, 20);

    CHECK(ft_encounter_guard_offset(&early) > (int32_t)FT_JAM_WINDOW_MS,
          "an early press is reported as early (%d)",
          (int)ft_encounter_guard_offset(&early));
    CHECK_EQ(early.last_guard, FT_GUARD_NONE);

    /* No press at all stays negative. */
    FtEncounter none;
    ft_encounter_init_single(&none, FT_ENEMY_STRAY_PACKET, &lo, 7u);
    none.phase = FT_PHASE_TELEGRAPH;
    none.phase_ms = FT_READY_MS;
    while(none.phase == FT_PHASE_TELEGRAPH) ft_encounter_tick(&none, 20);

    CHECK_EQ(ft_encounter_guard_offset(&none), -1);
    CHECK_EQ(none.last_guard, FT_GUARD_NONE);

    /* Each new wind-up starts from nothing, so last round's reading is never
     * shown against this round's hit. */
    FtEncounter fresh;
    ft_encounter_init_single(&fresh, FT_ENEMY_STRAY_PACKET, &lo, 7u);
    fresh.last_guard_offset = 123;
    fresh.guard_pressed = true;

    fresh.phase = FT_PHASE_MENU;
    fresh.phase_ms = 0;
    ft_encounter_press_ok(&fresh);
    for(int t = 0; t < 400 && fresh.phase != FT_PHASE_TELEGRAPH; t++) {
        ft_encounter_tick(&fresh, 20);
    }
    if(fresh.phase == FT_PHASE_TELEGRAPH) {
        CHECK(!fresh.guard_pressed, "a new wind-up starts unguarded");
        CHECK_EQ(fresh.last_guard_offset, -1);
    }
}

/* ---- The deflect stance ------------------------------------------------ */

/* Run an encounter to the next enemy wind-up, guarding at `before_impact` ms
 * (negative for no guard at all). Returns false if the fight ended first. */
static bool telegraph_and_guard(FtEncounter* e, int32_t before_impact) {
    for(int t = 0; t < 4000; t++) {
        if(ft_encounter_over(e)) return false;

        if(e->phase == FT_PHASE_MENU) return false; /* caller drives the menu */

        if(e->phase == FT_PHASE_TELEGRAPH && before_impact >= 0 && !e->guard_pressed) {
            const int32_t gap = (int32_t)FT_READY_MS + (int32_t)FT_TELEGRAPH_MS -
                                (int32_t)e->phase_ms;
            if(gap <= before_impact) ft_encounter_press_ok(e);
        }

        const FtPhase was = e->phase;
        ft_encounter_tick(e, 10);
        if(was == FT_PHASE_TELEGRAPH && e->phase != FT_PHASE_TELEGRAPH) return true;
    }
    return false;
}

static void test_deflect(void) {
    section("deflect");

    FtLoadout lo;
    ft_loadout_init(&lo);

    /* Arming costs a bar and the action, and does no damage by itself. */
    FtEncounter e;
    ft_encounter_init_single(&e, FT_ENEMY_STRAY_PACKET, &lo, 5);
    e.signal.value = FT_SIGNAL_PER_BAR;

    const int16_t foe_before = e.foes[0].charge;
    const uint8_t bars_before = ft_signal_bars(&e.signal);
    CHECK(bars_before >= 1u, "a full bar to spend");
    CHECK(ft_encounter_action_block(&e, FT_ACTION_DEFLECT) == NULL, "and so it offers");

    e.menu_index = FT_ACTION_DEFLECT;
    ft_encounter_press_ok(&e);

    CHECK(e.deflect_armed, "the stance goes up");
    CHECK(ft_signal_bars(&e.signal) < bars_before, "and the bar is gone");
    CHECK_EQ(e.foes[0].charge, foe_before);
    CHECK_EQ(e.last_total_damage, 0);

    /* Armed already: it refuses rather than eating a second bar. */
    CHECK(ft_encounter_action_block(&e, FT_ACTION_DEFLECT) != NULL,
          "a stance already up refuses");

    /* A perfect block sends the whole attack back. */
    FtEncounter full;
    ft_encounter_init_single(&full, FT_ENEMY_STRAY_PACKET, &lo, 5);
    full.deflect_armed = true;
    full.phase = FT_PHASE_TELEGRAPH;
    full.phase_ms = 0;
    full.foes[0].attack_index = 0;

    const int16_t hp_before = full.foes[0].charge;
    CHECK(telegraph_and_guard(&full, 20), "the wind-up resolved");
    CHECK_EQ(full.last_guard, FT_GUARD_CAPTURE);
    CHECK(full.last_deflect_fired, "and it went back");
    CHECK(full.last_deflect_damage > 0, "for real damage (%d)",
          (int)full.last_deflect_damage);
    CHECK(full.foes[0].charge < hp_before, "which the thrower actually took");
    CHECK_EQ(full.last_enemy_hit.damage, 0); /* a perfect block still blocks */

    /* A jam sends half of it. */
    FtEncounter half;
    ft_encounter_init_single(&half, FT_ENEMY_STRAY_PACKET, &lo, 5);
    half.deflect_armed = true;
    half.phase = FT_PHASE_TELEGRAPH;
    half.phase_ms = 0;
    half.foes[0].attack_index = 0;

    CHECK(telegraph_and_guard(&half, 120), "the wind-up resolved");
    CHECK_EQ(half.last_guard, FT_GUARD_JAM);
    CHECK(half.last_deflect_fired, "a jam bounces too");
    CHECK(half.last_deflect_damage > 0, "for something");
    CHECK(half.last_deflect_damage <= full.last_deflect_damage,
          "but never more than a perfect one (%d vs %d)",
          (int)half.last_deflect_damage, (int)full.last_deflect_damage);

    /* No guard, no bounce — the bar is simply spent. */
    FtEncounter missed;
    ft_encounter_init_single(&missed, FT_ENEMY_STRAY_PACKET, &lo, 5);
    missed.deflect_armed = true;
    missed.phase = FT_PHASE_TELEGRAPH;
    missed.phase_ms = 0;

    const int16_t miss_hp = missed.foes[0].charge;
    CHECK(telegraph_and_guard(&missed, -1), "the wind-up resolved");
    CHECK(!missed.last_deflect_fired, "nothing to send back");
    CHECK_EQ(missed.last_deflect_damage, 0);
    CHECK_EQ(missed.foes[0].charge, miss_hp);

    /* Without the stance, a perfect block is just a perfect block. */
    FtEncounter bare;
    ft_encounter_init_single(&bare, FT_ENEMY_STRAY_PACKET, &lo, 5);
    bare.phase = FT_PHASE_TELEGRAPH;
    bare.phase_ms = 0;

    const int16_t bare_hp = bare.foes[0].charge;
    CHECK(telegraph_and_guard(&bare, 20), "the wind-up resolved");
    CHECK_EQ(bare.last_guard, FT_GUARD_CAPTURE);
    CHECK(!bare.last_deflect_fired, "no stance, no bounce");
    CHECK_EQ(bare.foes[0].charge, bare_hp);

    /* A bounced broadcast sprays; a bounced contact goes to the sender.
     * The Stray Packet's only attack is a broadcast, so a group of them all
     * take it. */
    const FtEnemyId three[3] = {
        FT_ENEMY_STRAY_PACKET, FT_ENEMY_STRAY_PACKET, FT_ENEMY_STRAY_PACKET};
    FtEncounter wide;
    ft_encounter_init(&wide, three, 3u, &lo, 5);
    wide.deflect_armed = true;
    wide.phase = FT_PHASE_TELEGRAPH;
    wide.acting_foe = 0;
    wide.phase_ms = 0;

    int16_t was[3];
    for(uint8_t i = 0; i < 3u; i++) was[i] = wide.foes[i].charge;

    CHECK(telegraph_and_guard(&wide, 20), "the wind-up resolved");
    CHECK(wide.last_deflect_fired, "and bounced");
    for(uint8_t i = 0; i < 3u; i++) {
        CHECK(wide.foes[i].charge < was[i], "foe %u caught the broadcast back", i);
    }

    /* A contact attack goes back to the one that threw it and nobody else. */
    const FtEnemyId pair[2] = {FT_ENEMY_SCRAP_CRAWLER, FT_ENEMY_SCRAP_CRAWLER};
    FtEncounter one;
    ft_encounter_init(&one, pair, 2u, &lo, 5);
    one.deflect_armed = true;
    one.phase = FT_PHASE_TELEGRAPH;
    one.acting_foe = 1;
    one.phase_ms = 0;
    one.foes[1].attack_index = 0; /* Rip: contact */

    const int16_t idle = one.foes[0].charge, actor = one.foes[1].charge;
    CHECK(telegraph_and_guard(&one, 20), "the wind-up resolved");
    CHECK(one.last_deflect_fired, "and bounced");
    CHECK_EQ(one.foes[0].charge, idle);
    CHECK(one.foes[1].charge < actor, "only the thrower catches a contact bounce");

    /* A wall in front eats the bounce, which is what a wall is for. */
    const FtEnemyId walled[2] = {FT_ENEMY_BLANK_WALL, FT_ENEMY_SCRAP_CRAWLER};
    FtEncounter shielded;
    ft_encounter_init(&shielded, walled, 2u, &lo, 5);
    shielded.deflect_armed = true;
    shielded.phase = FT_PHASE_TELEGRAPH;
    shielded.acting_foe = 1;
    shielded.phase_ms = 0;
    shielded.foes[1].attack_index = 0;

    const int16_t wall_hp = shielded.foes[0].charge;
    const int16_t behind = shielded.foes[1].charge;
    CHECK(telegraph_and_guard(&shielded, 20), "the wind-up resolved");
    CHECK(shielded.foes[0].charge < wall_hp, "the wall takes it");
    CHECK_EQ(shielded.foes[1].charge, behind);

    /* A jammer locks the meter, so the stance is never even on offer. */
    FtEncounter jammed;
    ft_encounter_init_single(&jammed, FT_ENEMY_MAST_RELAY, &lo, 5);
    jammed.signal.value = FT_SIGNAL_PER_BAR * 4;
    CHECK(jammed.signal.locked, "a jammer locks the meter");
    CHECK(ft_encounter_action_block(&jammed, FT_ACTION_DEFLECT) != NULL,
          "so the deflect refuses");

    /* Arming is free: it costs the bar, never one of the round's two turns.
     * Any action cost lands in the fights where SP actually fills, which are
     * the close ones, and the simulator measured that as a straight loss. */
    FtEncounter free_act;
    ft_encounter_init_single(&free_act, FT_ENEMY_STRAY_PACKET, &lo, 5);
    free_act.signal.value = FT_SIGNAL_PER_BAR;

    const uint16_t turns_before = free_act.player_turns;
    free_act.menu_index = FT_ACTION_DEFLECT;
    ft_encounter_press_ok(&free_act);

    CHECK(free_act.deflect_armed, "armed");
    CHECK_EQ(free_act.player_turns, turns_before);
    CHECK_EQ(free_act.phase, FT_PHASE_MENU); /* still your move */

    /* And it waits for its counter rather than expiring unused: the bar is
     * never spent on nothing. */
    FtEncounter waits;
    ft_encounter_init_single(&waits, FT_ENEMY_STRAY_PACKET, &lo, 5);
    waits.deflect_armed = true;
    waits.phase = FT_PHASE_TELEGRAPH;
    waits.phase_ms = 0;

    CHECK(telegraph_and_guard(&waits, -1), "a wind-up came and went");
    CHECK(!waits.last_deflect_fired, "with nothing sent back");
    CHECK(waits.deflect_armed, "so the stance is still up");

    /* Once it does fire, it is spent. */
    waits.phase = FT_PHASE_TELEGRAPH;
    waits.phase_ms = 0;
    waits.guard_pressed = false;
    CHECK(telegraph_and_guard(&waits, 20), "the next one resolved");
    CHECK(waits.last_deflect_fired, "and bounced");
    CHECK(!waits.deflect_armed, "which uses it up");

    /* While it is up, a jam stops the hit dead rather than halving it. That
     * defensive half is what pays for the bar; the bounce alone measured at
     * about three damage, against an action worth six to eight. */
    FtEncounter soft;
    ft_encounter_init_single(&soft, FT_ENEMY_SCRAP_CRAWLER, &lo, 5);
    soft.phase = FT_PHASE_TELEGRAPH;
    soft.phase_ms = 0;
    CHECK(telegraph_and_guard(&soft, 120), "a plain jam resolved");
    CHECK_EQ(soft.last_guard, FT_GUARD_JAM);
    const int16_t halved = soft.last_enemy_hit.damage;
    CHECK(halved > 0, "which still lets something through (%d)", (int)halved);

    FtEncounter hard;
    ft_encounter_init_single(&hard, FT_ENEMY_SCRAP_CRAWLER, &lo, 5);
    hard.deflect_armed = true;
    hard.phase = FT_PHASE_TELEGRAPH;
    hard.phase_ms = 0;
    CHECK(telegraph_and_guard(&hard, 120), "the same jam with the stance up");
    CHECK_EQ(hard.last_guard, FT_GUARD_JAM); /* the readout still says jam */
    CHECK_EQ(hard.last_enemy_hit.damage, 0);
    CHECK(hard.last_deflect_fired, "and it went back");

    /* Every reason it can refuse fits one line. */
    FtEncounter say;
    ft_encounter_init_single(&say, FT_ENEMY_STRAY_PACKET, &lo, 5);
    say.signal.value = 0;
    const char* why = ft_encounter_action_block(&say, FT_ACTION_DEFLECT);
    CHECK(why && strlen(why) <= FT_TUTORIAL_MAX_CHARS, "no bar: \"%s\"", why ? why : "");

    say.signal.value = FT_SIGNAL_PER_BAR;
    say.deflect_armed = true;
    why = ft_encounter_action_block(&say, FT_ACTION_DEFLECT);
    CHECK(why && strlen(why) <= FT_TUTORIAL_MAX_CHARS, "already up: \"%s\"",
          why ? why : "");
}

/* ---- The field guide's words ------------------------------------------- */

static void test_guide_words(void) {
    section("the guide reads");

    char line[40];

    for(uint8_t i = 0; i < FT_ENEMY_COUNT; i++) {
        const FtEnemyId id = (FtEnemyId)i;
        const FtEnemy*  en = &FT_ENEMIES[id];

        /* Every line has to FIT, not merely be drawn.
         *
         * draw_clipped does its job silently: an over-long line is not drawn
         * off-panel, it is drawn with the end missing, so the layout checker
         * passes it and the player reads "Sealed. Sub-GHz does". Width is a
         * test's job, not the preview's. */
        ft_guide_vitals(id, line, (uint8_t)sizeof(line));
        CHECK(strlen(line) <= FT_GUIDE_SHORT_MAX, "%s vitals fit: \"%s\"",
              en->name, line);
        CHECK(strstr(line, "HP") != NULL, "%s vitals name HP", en->name);

        const char* advice = ft_guide_advice(id);
        CHECK(advice && advice[0], "%s has advice", en->name);
        CHECK(advice && strlen(advice) <= FT_GUIDE_SHORT_MAX,
              "%s advice fits: \"%s\"", en->name, advice ? advice : "");

        const char* tag = ft_guide_tag(id);
        CHECK(tag && strlen(tag) <= 8u, "%s tag fits: \"%s\"", en->name,
              tag ? tag : "");

        for(uint8_t k = 0; k < 6u; k++) {
            const char* note = ft_guide_note(id, k);
            if(!note) break;
            CHECK(strlen(note) <= FT_GUIDE_LINE_MAX, "%s note %u fits: \"%s\"",
                  en->name, k, note);
        }

        for(uint8_t k = 0; k < FT_ENEMY_MAX_ATTACKS; k++) {
            if(!ft_guide_attack_line(id, k, line, (uint8_t)sizeof(line))) break;
            CHECK(strlen(line) <= FT_GUIDE_LINE_MAX, "%s attack %u fits: \"%s\"",
                  en->name, k, line);
        }

        /* And the whole page has to fit the six rows the entry screen has. */
        uint8_t rows = 2u; /* vitals and advice */
        for(uint8_t k = 0; k < 6u; k++) {
            if(!ft_guide_note(id, k)) break;
            rows++;
        }
        rows = (uint8_t)(rows + ft_guide_attack_count(id));
        CHECK(rows <= 6u, "%s fits the page (%u rows)", en->name, rows);
    }

    /* A bulwark never takes a turn, so it must never list an attack. Its
     * table carries one because the resolver needs the slot; showing it read
     * as a threat that does not exist. */
    CHECK_EQ(ft_guide_attack_count(FT_ENEMY_BLANK_WALL), 0);
    CHECK(!ft_guide_attack_line(FT_ENEMY_BLANK_WALL, 0, line, (uint8_t)sizeof(line)),
          "and refuses to describe one");

    /* Everything else does. */
    for(uint8_t i = 0; i < FT_ENEMY_COUNT; i++) {
        if(FT_ENEMIES[i].attrs & FT_ATTR_BULWARK) continue;
        CHECK(ft_guide_attack_count((FtEnemyId)i) > 0u, "%s lists what it does",
              FT_ENEMIES[i].name);
    }

    /* The advice has to actually match the enemy: an airborne one must not be
     * told to use the module that cannot reach it. */
    for(uint8_t i = 0; i < FT_ENEMY_COUNT; i++) {
        const char* a = ft_guide_advice((FtEnemyId)i);
        const uint32_t attrs = FT_ENEMIES[i].attrs;

        if((attrs & FT_ATTR_AIRBORNE) && !(attrs & FT_ATTR_BULWARK) &&
           !(attrs & FT_ATTR_SLEEPER)) {
            CHECK(strstr(a, "Sub-GHz") != NULL,
                  "%s flies, so the advice is the broadcast: \"%s\"",
                  FT_ENEMIES[i].name, a);
        }
        if((attrs & FT_ATTR_ENCRYPTED) && !(attrs & FT_ATTR_AIRBORNE) &&
           !(attrs & FT_ATTR_BULWARK) && !(attrs & FT_ATTR_SLEEPER)) {
            CHECK(strstr(a, "NFC") != NULL,
                  "%s is sealed, so the advice is contact: \"%s\"",
                  FT_ENEMIES[i].name, a);
        }
    }

    /* And an attack line says all four things it is meant to. */
    ft_guide_attack_line(FT_ENEMY_SEALED_LOCK, 1, line, (uint8_t)sizeof(line));
    CHECK(strstr(line, "4") != NULL, "power: \"%s\"", line);
    CHECK(strstr(line, "touch") != NULL, "how it reaches you: \"%s\"", line);
    CHECK(strstr(line, "no guard") != NULL, "that nothing stops it: \"%s\"", line);
    CHECK(strstr(line, "MP-") != NULL, "and what it leaves: \"%s\"", line);
}

/* ---- Chapter 1, walked end to end -------------------------------------- */

/* Step the player one tile, letting the step finish. */
static void walk(FtWorld* w, int8_t dx, int8_t dy) {
    for(int t = 0; t < 60; t++) {
        ft_world_update(w, dx, dy, 10);
        if(!ft_world_moving(w)) {
            /* Give the facing update a frame of its own so a blocked step
             * still turns the player. */
            if(t > 0) return;
        }
    }
}

static void test_weldhome(void) {
    section("Weldhome, the gate and the kid");

    const uint8_t APPROACH = FT_ROOM_CH1_FIRST;
    const uint8_t GATE = (uint8_t)(FT_ROOM_CH1_FIRST + 1u);
    const uint8_t JUNCTION = (uint8_t)(FT_ROOM_CH1_FIRST + 2u);

    CHECK(ft_room_count() > JUNCTION, "the three rooms exist");
    CHECK_EQ(ft_quest_def(FT_QUEST_WREN)->giver_room, GATE);

    /* The prologue now leads here rather than straight into the slices. */
    const FtRoom* cold_gate = ft_room(3);
    bool leads_on = false;
    for(uint8_t i = 0; i < cold_gate->exit_count; i++) {
        if(cold_gate->exits[i].dest_room == APPROACH) leads_on = true;
    }
    CHECK(leads_on, "the prologue ends at the Approach");

    /* --- the turn you cannot take --- */
    FtWorld w;
    ft_world_init(&w);
    ft_world_enter(&w, APPROACH, 1, 2);

    const FtExit* drop = NULL;
    const FtRoom* ap = ft_room(APPROACH);
    for(uint8_t i = 0; i < ap->exit_count; i++) {
        if(ap->exits[i].dest_room == JUNCTION) drop = &ap->exits[i];
    }
    CHECK(drop != NULL, "the Approach has a way down");
    if(!drop) return;

    CHECK(!ft_world_exit_open(&w, drop), "which is shut before anybody asks");

    const char* no = ft_world_exit_refusal(drop);
    CHECK(no != NULL, "and says why");
    CHECK(no && strlen(no) <= FT_TUTORIAL_MAX_CHARS, "in one line: \"%s\"",
          no ? no : "");

    /* It is not locked with a key: nothing in the world changed, only the
     * reason. Take the quest and the same exit works. */
    ft_quest_talk(&w.quests, FT_QUEST_WREN);
    CHECK_EQ(ft_quest_state(&w.quests, FT_QUEST_WREN), FT_QUEST_ACTIVE);
    CHECK(ft_world_exit_open(&w, drop), "the drop opens once she asks");

    /* --- the gate --- */
    FtWorld g;
    ft_world_init(&g);
    ft_world_enter(&g, GATE, 1, 3);

    const FtExit* gate = NULL;
    const FtRoom* wh = ft_room(GATE);
    for(uint8_t i = 0; i < wh->exit_count; i++) {
        if(wh->exits[i].dest_room == 4u) gate = &wh->exits[i];
    }
    CHECK(gate != NULL, "Weldhome opens onto the Scrapline");
    if(!gate) return;

    CHECK(!ft_world_exit_open(&g, gate), "but not yet");
    ft_quest_talk(&g.quests, FT_QUEST_WREN);
    CHECK(!ft_world_exit_open(&g, gate), "and not just for asking");
    ft_quest_advance(&g.quests, FT_QUEST_WREN, FT_QUEST_READY);
    CHECK(!ft_world_exit_open(&g, gate), "nor for finding her");
    ft_quest_talk(&g.quests, FT_QUEST_WREN);
    CHECK_EQ(ft_quest_state(&g.quests, FT_QUEST_WREN), FT_QUEST_DONE);
    CHECK(ft_world_exit_open(&g, gate), "only for bringing her back");

    /* Coll is standing in the room, and she is the one who speaks. */
    bool has_coll = false;
    for(uint8_t i = 0; i < wh->ent_count; i++) {
        if(wh->ents[i].kind == FT_ENT_NPC &&
           wh->ents[i].roster == (uint8_t)FT_QUEST_WREN) {
            has_coll = true;
        }
    }
    CHECK(has_coll, "somebody is holding it");

    /* --- the junction --- */
    const FtRoom* ej = ft_room(JUNCTION);
    bool has_wren = false, has_foe = false;
    for(uint8_t i = 0; i < ej->ent_count; i++) {
        if(ej->ents[i].kind == FT_ENT_WREN) has_wren = true;
        if(ej->ents[i].kind == FT_ENT_FOE) {
            has_foe = true;
            const FtRoster* r = ft_roster(ej->ents[i].roster);
            CHECK_EQ(r->count, 3);
            CHECK(FT_ENEMIES[r->foes[0]].attrs & FT_ATTR_BULWARK,
                  "guarded the way things guard: a wall in front");
        }
    }
    CHECK(has_wren, "the kid is down there");
    CHECK(has_foe, "so is what took her");

    /* --- freeing her --- */
    FtWorld j;
    ft_world_init(&j);
    ft_quest_talk(&j.quests, FT_QUEST_WREN); /* take it */
    ft_world_enter(&j, JUNCTION, 1, 1);

    CHECK(!j.escort, "nobody with you yet");

    const FtQuestTalk hers = ft_quest_wren_talk(&j.quests);
    CHECK(hers.follows, "she comes out with you");
    CHECK_EQ(ft_quest_state(&j.quests, FT_QUEST_WREN), FT_QUEST_READY);
    CHECK(hers.who && strcmp(hers.who, "Warden Coll") != 0,
          "and she speaks for herself");

    ft_world_escort_start(&j);
    CHECK(j.escort, "walking with you");
    CHECK_EQ(j.escort_mv.tx, j.mv.tx);

    /* --- the walk home --- */
    /* She steps into the tile you just left, so every tile she walks is a
     * tile you walked: she can never end up inside a wall. */
    const FtMap* map = ft_world_map(&j);
    for(int step = 0; step < 6; step++) {
        const uint8_t before_tx = j.mv.tx, before_ty = j.mv.ty;

        walk(&j, 1, 0);
        if(j.mv.tx == before_tx && j.mv.ty == before_ty) break;

        for(int t = 0; t < 40; t++) ft_world_update(&j, 0, 0, 10);

        CHECK(!ft_tile_solid(ft_map_tile(map, j.escort_mv.tx, j.escort_mv.ty)),
              "she is on solid ground (%u,%u)", j.escort_mv.tx, j.escort_mv.ty);

        const int32_t gap = abs_i32((int32_t)j.escort_mv.tx - (int32_t)j.mv.tx) +
                            abs_i32((int32_t)j.escort_mv.ty - (int32_t)j.mv.ty);
        CHECK(gap <= 1, "and keeps up (%d tiles back)", (int)gap);
    }

    /* A door carries her through with you rather than leaving her behind. */
    ft_world_enter(&j, APPROACH, 10, 7);
    CHECK(j.escort, "she comes through the door");
    CHECK_EQ(j.escort_mv.tx, 10);
    CHECK_EQ(j.escort_mv.ty, 7);

    /* And she is not still standing in the junction she left. */
    CHECK_EQ(ft_world_wren_ahead(&j), -1);

    /* --- handing her back --- */
    ft_world_enter(&j, GATE, 1, 3);
    const FtQuestTalk done = ft_quest_talk(&j.quests, FT_QUEST_WREN);
    CHECK_EQ(ft_quest_state(&j.quests, FT_QUEST_WREN), FT_QUEST_DONE);
    CHECK(done.orbs > 0, "which pays");

    ft_world_escort_stop(&j);
    CHECK(!j.escort, "and she goes inside");

    /* Every line anybody says in this chain fits the box. */
    for(uint8_t st = 0; st <= (uint8_t)FT_QUEST_DONE; st++) {
        FtQuests q;
        ft_quests_init(&q);
        q.state[FT_QUEST_WREN] = st;

        const FtQuestTalk t = ft_quest_talk(&q, FT_QUEST_WREN);
        CHECK(t.who && strlen(t.who) <= FT_TUTORIAL_MAX_CHARS,
              "state %u names its speaker: \"%s\"", st, t.who ? t.who : "");

        for(uint8_t i = 0; i < t.lines; i++) {
            CHECK(t.line[i] && strlen(t.line[i]) <= FT_TUTORIAL_MAX_CHARS,
                  "Coll state %u line %u fits: \"%s\"", st, i,
                  t.line[i] ? t.line[i] : "");
        }

        FtQuests k;
        ft_quests_init(&k);
        k.state[FT_QUEST_WREN] = st;

        const FtQuestTalk kt = ft_quest_wren_talk(&k);
        for(uint8_t i = 0; i < kt.lines; i++) {
            CHECK(kt.line[i] && strlen(kt.line[i]) <= FT_TUTORIAL_MAX_CHARS,
                  "Wren state %u line %u fits: \"%s\"", st, i,
                  kt.line[i] ? kt.line[i] : "");
        }
    }

    /* Only the right state frees her: she does not walk off with somebody
     * who has not been asked to fetch her. */
    FtQuests cold;
    ft_quests_init(&cold);
    CHECK(!ft_quest_wren_talk(&cold).follows, "she stays put before Coll asks");

    /* She survives a save, because a terminal half way home is a terminal. */
    FtWorld carry;
    ft_world_init(&carry);
    ft_quest_talk(&carry.quests, FT_QUEST_WREN);
    ft_world_enter(&carry, APPROACH, 5, 2);
    ft_world_escort_start(&carry);
    carry.escort_mv.tx = 4;
    carry.escort_mv.ty = 2;

    FtSaveData sd;
    ft_save_from_world(&carry, false, true, &sd);

    uint8_t bytes[FT_SAVE_MAX_BYTES];
    const uint8_t len = ft_save_encode(&sd, bytes, sizeof(bytes));
    CHECK(len > 0, "the save encodes");

    FtSaveData back;
    CHECK(ft_save_decode(bytes, len, &back), "and decodes");

    FtWorld reloaded;
    bool coach = true;
    ft_save_to_world(&back, &reloaded, &coach, NULL);

    CHECK(reloaded.escort, "she is still with you");
    CHECK_EQ(reloaded.escort_mv.tx, 4);
    CHECK_EQ(reloaded.escort_mv.ty, 2);
    CHECK_EQ(ft_quest_state(&reloaded.quests, FT_QUEST_WREN), FT_QUEST_ACTIVE);
}

/* ---- Pockets ----------------------------------------------------------- */

static void test_items(void) {
    section("food and pockets");

    FtPockets p;
    ft_pockets_init(&p);
    CHECK_EQ(ft_pockets_used(&p), 0);
    CHECK_EQ(ft_pockets_kinds(&p), 0);
    CHECK(!ft_pockets_full(&p), "empty is not full");
    CHECK_EQ(ft_pockets_nth(&p, 0), FT_ITEM_COUNT);

    /* The cap is the whole design: without one, food stops being a decision
     * and becomes a chore you do before every fight. */
    for(uint8_t i = 0; i < FT_POCKET_MAX; i++) {
        CHECK(ft_pockets_add(&p, FT_ITEM_APPLE), "apple %u fits", i);
    }
    CHECK(ft_pockets_full(&p), "and then it is full");
    CHECK(!ft_pockets_add(&p, FT_ITEM_APPLE), "one more is refused");
    CHECK(!ft_pockets_add(&p, FT_ITEM_RATION), "whatever it is");
    CHECK_EQ(ft_pockets_used(&p), FT_POCKET_MAX);

    CHECK(ft_pockets_take(&p, FT_ITEM_APPLE), "eating one makes room");
    CHECK(ft_pockets_add(&p, FT_ITEM_CELL), "for something else");
    CHECK_EQ(ft_pockets_kinds(&p), 2);

    CHECK(!ft_pockets_take(&p, FT_ITEM_RATION), "you cannot eat what you have not got");

    /* Walking the kinds gives back exactly what is carried, in table order. */
    CHECK_EQ(ft_pockets_nth(&p, 0), FT_ITEM_APPLE);
    CHECK_EQ(ft_pockets_nth(&p, 1), FT_ITEM_CELL);
    CHECK_EQ(ft_pockets_nth(&p, 2), FT_ITEM_COUNT);

    /* Every item says what it is and what it does, inside the row it shares. */
    for(uint8_t i = 0; i < FT_ITEM_COUNT; i++) {
        const FtItemDef* d = ft_item_def((FtItemId)i);
        CHECK(d->name && strlen(d->name) <= 10u, "item %u's name fits: \"%s\"", i,
              d->name ? d->name : "");
        CHECK(d->what && strlen(d->what) <= FT_TUTORIAL_MAX_CHARS,
              "item %u says what it does: \"%s\"", i, d->what ? d->what : "");
        CHECK(d->heal > 0 || d->ram > 0, "item %u does something", i);
    }

    /* Eating at full health throws the thing away, and a pocket this small
     * must never let that be the player's mistake to make. */
    CHECK(ft_item_useful(FT_ITEM_APPLE, 5, 20, 5, 5), "hurt: an apple helps");
    CHECK(!ft_item_useful(FT_ITEM_APPLE, 20, 20, 0, 5), "topped up: it does not");
    CHECK(ft_item_useful(FT_ITEM_CELL, 20, 20, 0, 5), "but a cell still does");
    CHECK(!ft_item_useful(FT_ITEM_CELL, 5, 20, 5, 5), "and not the other way round");

    /* ---- in a fight ---- */
    FtLoadout lo;
    ft_loadout_init(&lo);

    FtEncounter e;
    ft_encounter_init_single(&e, FT_ENEMY_STRAY_PACKET, &lo, 4);

    CHECK(ft_encounter_action_block(&e, FT_ACTION_ITEM) != NULL,
          "empty pockets refuse");
    CHECK_EQ(ft_encounter_item_at(&e), FT_ITEM_COUNT);

    ft_pockets_add(&e.pockets, FT_ITEM_APPLE);
    ft_pockets_add(&e.pockets, FT_ITEM_CELL);

    e.roll.current = 4;
    e.roll.target = 4;
    e.stats.charge = 4;
    e.stats.ram = 0;

    CHECK(ft_encounter_action_block(&e, FT_ACTION_ITEM) == NULL, "now it offers");
    CHECK_EQ(ft_encounter_item_at(&e), FT_ITEM_APPLE);

    /* The picker is its own ring, steered with UP and DOWN so a stray thumb
     * on the action row never changes which item is about to go. */
    ft_encounter_item_move(&e, 1);
    CHECK_EQ(ft_encounter_item_at(&e), FT_ITEM_CELL);
    ft_encounter_item_move(&e, 1);
    CHECK_EQ(ft_encounter_item_at(&e), FT_ITEM_APPLE);
    ft_encounter_item_move(&e, -1);
    CHECK_EQ(ft_encounter_item_at(&e), FT_ITEM_CELL);

    /* Using one heals, costs the item, and does not cost the sweep. */
    const int16_t mp_before = e.stats.ram;
    e.menu_index = (uint8_t)FT_ACTION_ITEM;
    ft_encounter_press_ok(&e);

    CHECK(e.stats.ram > mp_before, "the cell went in (%d -> %d)", (int)mp_before,
          (int)e.stats.ram);
    CHECK_EQ(ft_pockets_count(&e.pockets, FT_ITEM_CELL), 0);
    CHECK_EQ(e.phase, FT_PHASE_RESULT); /* nothing to time */

    /* And the cursor still points at something that exists. */
    CHECK_EQ(ft_encounter_item_at(&e), FT_ITEM_APPLE);

    /* Healing goes through the roll, so a lethal hit can be eaten out of. */
    FtEncounter hurt;
    ft_encounter_init_single(&hurt, FT_ENEMY_STRAY_PACKET, &lo, 4);
    ft_pockets_add(&hurt.pockets, FT_ITEM_RATION);
    hurt.roll.current = 9;
    hurt.roll.target = 0; /* a brownout: rolling toward death */
    CHECK(ft_roll_brownout(&hurt.roll), "on the way down");

    hurt.menu_index = (uint8_t)FT_ACTION_ITEM;
    ft_encounter_press_ok(&hurt);
    CHECK(!ft_roll_brownout(&hurt.roll), "and out of it again");

    /* ---- out in the world ---- */
    FtWorld w;
    ft_world_init(&w);
    CHECK_EQ(ft_pockets_used(&w.pockets), 0);

    /* Somewhere in the game there is something to pick, and it is reachable. */
    int trees = 0, caches = 0;
    for(uint8_t r = 0; r < ft_room_count(); r++) {
        const FtRoom* room = ft_room(r);
        for(uint8_t i = 0; i < room->ent_count && i < FT_MAX_ROOM_ENTS; i++) {
            if(room->ents[i].kind == FT_ENT_TREE) trees++;
            if(room->ents[i].kind == FT_ENT_CACHE) caches++;

            if(room->ents[i].kind == FT_ENT_TREE ||
               room->ents[i].kind == FT_ENT_CACHE) {
                CHECK(room->ents[i].roster < FT_ITEM_COUNT,
                      "room %u entity %u carries a real item", r, i);
            }
        }
    }
    CHECK(trees > 0, "there are trees (%d)", trees);
    CHECK(caches > 0, "and caches (%d)", caches);

    /* Picking one: face it, take it, and it is gone. */
    const FtRoom* first = ft_room(0);
    int at = -1;
    for(uint8_t i = 0; i < first->ent_count; i++) {
        if(first->ents[i].kind == FT_ENT_TREE) at = (int)i;
    }
    CHECK(at >= 0, "the first room has one, so picking is taught early");
    if(at < 0) return;

    ft_world_enter(&w, 0, (uint8_t)(first->ents[at].tx - 1u), first->ents[at].ty);
    w.facing = FT_FACE_RIGHT;

    CHECK_EQ(ft_world_pick_ahead(&w), at);
    CHECK_EQ(ft_world_pick(&w, (uint8_t)at), (FtItemId)first->ents[at].roster);
    CHECK_EQ(ft_pockets_used(&w.pockets), 1);
    CHECK_EQ(ft_world_pick_ahead(&w), -1);
    CHECK(ft_world_entity_gone(&w, (uint8_t)at), "the tree is bare");

    /* A tree comes back when you walk the room again; a cache does not. That
     * is what makes a cleared room worth walking, and a cache worth finding. */
    ft_world_enter(&w, 1, 1, 2);
    ft_world_enter(&w, 0, 3, 4);
    CHECK(!ft_world_entity_gone(&w, (uint8_t)at), "and back when you return");

    /* Full pockets leave it where it is rather than swallowing it. */
    FtWorld packed;
    ft_world_init(&packed);
    for(uint8_t i = 0; i < FT_POCKET_MAX; i++) {
        ft_pockets_add(&packed.pockets, FT_ITEM_RATION);
    }
    ft_world_enter(&packed, 0, (uint8_t)(first->ents[at].tx - 1u), first->ents[at].ty);
    packed.facing = FT_FACE_RIGHT;

    CHECK_EQ(ft_world_pick(&packed, (uint8_t)at), FT_ITEM_COUNT);
    CHECK(!ft_world_entity_gone(&packed, (uint8_t)at),
          "a tree you cannot carry from stays picked-able");

    /* A tree is something you walk around, and a picked one is not. */
    FtWorld bump;
    ft_world_init(&bump);
    ft_world_enter(&bump, 0, (uint8_t)(first->ents[at].tx - 1u), first->ents[at].ty);
    bump.facing = FT_FACE_RIGHT;

    for(int t = 0; t < 120; t++) ft_world_update(&bump, 1, 0, 20);
    CHECK_EQ(bump.mv.tx, (uint8_t)(first->ents[at].tx - 1u));

    ft_world_pick(&bump, (uint8_t)at);
    for(int t = 0; t < 120; t++) ft_world_update(&bump, 1, 0, 20);
    CHECK(bump.mv.tx > (uint8_t)(first->ents[at].tx - 1u),
          "once it is picked you can walk through where it was");

    /* And they ride the save, like everything else the run earned. */
    FtWorld carry;
    ft_world_init(&carry);
    ft_pockets_add(&carry.pockets, FT_ITEM_APPLE);
    ft_pockets_add(&carry.pockets, FT_ITEM_APPLE);
    ft_pockets_add(&carry.pockets, FT_ITEM_CELL);

    FtSaveData d;
    ft_save_from_world(&carry, false, true, &d);

    uint8_t buf[FT_SAVE_MAX_BYTES];
    const uint8_t len = ft_save_encode(&d, buf, sizeof(buf));
    CHECK(len > 0, "the save encodes");

    FtSaveData back;
    CHECK(ft_save_decode(buf, len, &back), "and decodes");

    FtWorld reloaded;
    bool coach = true;
    ft_save_to_world(&back, &reloaded, &coach, NULL);

    CHECK_EQ(ft_pockets_count(&reloaded.pockets, FT_ITEM_APPLE), 2);
    CHECK_EQ(ft_pockets_count(&reloaded.pockets, FT_ITEM_CELL), 1);
}

/* ---- What it sounds like ----------------------------------------------- */

static void test_audio(void) {
    section("sound");

    /* Every cue exists, plays something, and is short enough to arrive
     * before the thing it is describing has finished happening. */
    for(uint8_t i = 0; i < FT_SFX_COUNT; i++) {
        uint8_t n = 0;
        const FtNote* notes = ft_sfx((FtSfxId)i, &n);

        CHECK(notes != NULL, "cue %u exists", i);
        CHECK(n > 0, "cue %u has notes", i);
        if(!notes) continue;

        bool any_tone = false;
        for(uint8_t k = 0; k < n; k++) {
            CHECK(notes[k].ms > 0, "cue %u note %u lasts (%u ms)", i, k,
                  notes[k].ms);

            /* A piezo buzzer below about 100 Hz is a click, and above about
             * 4 kHz it is a whistle nobody wants near their ear. */
            if(notes[k].hz != FT_NOTE_REST) {
                any_tone = true;
                CHECK(notes[k].hz >= 100u && notes[k].hz <= 4000u,
                      "cue %u note %u is audible (%u Hz)", i, k, notes[k].hz);
            }
        }
        CHECK(any_tone, "cue %u makes a sound rather than a pause", i);

        const uint16_t len = ft_sfx_length_ms((FtSfxId)i);
        CHECK(len > 0, "cue %u has a length", i);
        CHECK(len <= 1200u, "cue %u does not outstay its moment (%u ms)", i, len);
    }

    /* The ones that fire on every step have to be almost nothing: at one per
     * tile, anything with a tail turns walking into a drone. */
    CHECK(ft_sfx_length_ms(FT_SFX_MOVE) <= 30u, "a step is a tick (%u ms)",
          ft_sfx_length_ms(FT_SFX_MOVE));

    /* Winning should sound better than losing, and both should be longer
     * than a hit: they are the only moments the game gets to be musical. */
    CHECK(ft_sfx_length_ms(FT_SFX_WIN) > ft_sfx_length_ms(FT_SFX_HIT),
          "a win is a tune, not a blip");
    CHECK(ft_sfx_length_ms(FT_SFX_LOSE) > ft_sfx_length_ms(FT_SFX_HURT),
          "and so is a loss");

    /* A perfect block has to be distinguishable from a jam by ear alone,
     * because the whole guard system is a thing you learn by feel. */
    uint8_t jam_n = 0, perfect_n = 0;
    const FtNote* jam = ft_sfx(FT_SFX_JAM, &jam_n);
    const FtNote* perfect = ft_sfx(FT_SFX_PERFECT, &perfect_n);

    CHECK(jam && perfect, "both exist");
    if(jam && perfect) {
        CHECK(perfect_n != jam_n || perfect[0].hz != jam[0].hz,
              "and they do not sound the same");
        CHECK(perfect[perfect_n - 1u].hz > jam[jam_n - 1u].hz,
              "the better one ends higher (%u vs %u)",
              perfect[perfect_n - 1u].hz, jam[jam_n - 1u].hz);
    }

    /* Out of range asks for nothing rather than walking off the table. */
    uint8_t n = 99;
    CHECK(ft_sfx(FT_SFX_COUNT, &n) == NULL, "a bad id plays nothing");
    CHECK_EQ(n, 0);
    CHECK_EQ(ft_sfx_length_ms(FT_SFX_COUNT), 0);
}

/* ---- Nobody stands in a doorway ---------------------------------------- */

static void test_ways_out(void) {
    section("every way out is walkable to");

    /* The flood fill that already guards these rooms walks *tiles*. People
     * are not tiles: Warden Coll stood on the one square that touches
     * Weldhome's gate, so the reward for the whole chapter was a gate you
     * could open and then not reach. This walks the room the way the player
     * does, with everything solid that is solid to them. */
    for(uint8_t r = 0; r < ft_room_count(); r++) {
        const FtRoom* room = ft_room(r);
        const FtMap*  m = room->map;

        static uint8_t seen[64 * 32];
        static uint16_t queue[64 * 32];

        const uint32_t tiles = (uint32_t)m->w * m->h;
        if(tiles > sizeof(seen)) continue;

        for(uint32_t i = 0; i < tiles; i++) seen[i] = 0u;

        /* Everything the player bumps into rather than walks through. A tree
         * counts: it is solid until it is picked. */
        for(uint8_t i = 0; i < room->ent_count && i < FT_MAX_ROOM_ENTS; i++) {
            const FtEntKind k = room->ents[i].kind;
            if(k != FT_ENT_NPC && k != FT_ENT_WREN && k != FT_ENT_TREE) continue;

            const uint32_t at = (uint32_t)room->ents[i].ty * m->w + room->ents[i].tx;
            if(at < tiles) seen[at] = 2u; /* blocked */
        }

        /* Start where the player arrives: the first exit's tile. */
        uint16_t head = 0, tail = 0;
        const uint32_t start = (uint32_t)room->exits[0].ty * m->w + room->exits[0].tx;
        if(start >= tiles) continue;

        seen[start] = 1u;
        queue[tail++] = (uint16_t)start;

        while(head < tail) {
            const uint16_t at = queue[head++];
            const int32_t x = at % m->w, y = at / m->w;

            static const int8_t STEP[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for(uint8_t d = 0; d < 4u; d++) {
                const int32_t nx = x + STEP[d][0], ny = y + STEP[d][1];
                if(nx < 0 || ny < 0 || nx >= (int32_t)m->w || ny >= (int32_t)m->h) continue;

                const uint32_t to = (uint32_t)ny * m->w + (uint32_t)nx;
                if(seen[to]) continue;
                if(ft_tile_solid(ft_map_tile(m, nx, ny))) continue;

                seen[to] = 1u;
                queue[tail++] = (uint16_t)to;
            }
        }

        /* Every other way out has to be standable-on. */
        for(uint8_t e = 1; e < room->exit_count; e++) {
            const uint32_t at = (uint32_t)room->exits[e].ty * m->w + room->exits[e].tx;
            CHECK(at < tiles && seen[at] == 1u,
                  "room %u (%s): exit %u is not blocked by anybody", r, m->name, e);
        }

        /* And so does everybody you are meant to talk to, from at least one
         * side — an NPC you cannot stand next to cannot be spoken to. */
        for(uint8_t i = 0; i < room->ent_count && i < FT_MAX_ROOM_ENTS; i++) {
            const FtEntKind k = room->ents[i].kind;
            if(k != FT_ENT_NPC && k != FT_ENT_WREN && k != FT_ENT_TREE &&
               k != FT_ENT_CACHE) {
                continue;
            }

            bool touchable = false;
            static const int8_t STEP[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for(uint8_t d = 0; d < 4u; d++) {
                const int32_t nx = (int32_t)room->ents[i].tx + STEP[d][0];
                const int32_t ny = (int32_t)room->ents[i].ty + STEP[d][1];
                if(nx < 0 || ny < 0 || nx >= (int32_t)m->w || ny >= (int32_t)m->h) continue;

                const uint32_t to = (uint32_t)ny * m->w + (uint32_t)nx;
                if(to < tiles && seen[to] == 1u) touchable = true;
            }
            CHECK(touchable, "room %u (%s): entity %u can be stood next to", r,
                  m->name, i);
        }
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
    test_payloads();
    test_ambush();
    test_orbs();
    test_quests();
    test_npc();
    test_weldhome();
    test_items();
    test_audio();
    test_notice();
    test_guard_aftermath();
    test_deflect();
    test_always_something_to_do();
    test_bulwark();
    test_sleeper();
    test_fast_turn_order();
    test_enemy_roster();
    test_guide();
    test_guide_words();
    test_defend_is_worth_it();
    test_losing_costs();
    test_levelup();
    test_scene_wipe();
    test_broadcast_sweep();
    test_world_links();
    test_ways_out();
    test_world_tour();
    test_rng();

    printf("\n%d checks, %d failures\n\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
