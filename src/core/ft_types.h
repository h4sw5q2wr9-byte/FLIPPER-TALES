/*
 * Flipper Tales — core types and tuning constants.
 *
 * This header, and everything else in src/core, is pure C99 with no Flipper
 * SDK dependency. It compiles and runs on a host machine so the combat maths
 * can be unit-tested and balance-simulated before anything is flashed.
 *
 * Rules for this directory:
 *   - no floating point anywhere (the MCU has no FPU budget)
 *   - no heap allocation (fixed-size storage only)
 *   - no stdio, no Furi, no hardware
 */
#ifndef FT_TYPES_H
#define FT_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- Stat limits (DESIGN.md 4.1) ------------------------------------- */

#define FT_START_CHARGE 14
#define FT_START_RAM    5
#define FT_START_FLASH  3

#define FT_CAP_CHARGE 100
#define FT_CAP_RAM    100
#define FT_CAP_FLASH  30

#define FT_LEVEL_UP_CHARGE 5
#define FT_LEVEL_UP_RAM    5
#define FT_LEVEL_UP_FLASH  3

#define FT_XP_PER_LEVEL   100
#define FT_XP_BATTLE_CAP  100

#define FT_LEVEL_CAP_BASE         4
#define FT_LEVEL_CAP_PER_CHAPTER  4

/* ---- Combat tuning (DESIGN.md 4.2, 4.6) ------------------------------ */

/* An attack always has at least this much power before defence is applied. */
#define FT_MIN_RAW_DAMAGE 1

/* Damage at or above this bypasses rolling Charge and applies instantly.
 * 125 is the highest Charge reachable: the 100 cap plus 5 stacked Charge+. */
#define FT_INSTANT_DAMAGE_THRESHOLD 125

#define FT_ROLL_BASE_INTERVAL_MS 60
#define FT_JAM_REDUCTION_PCT     50

/* ---- Signal meter (DESIGN.md 4.7) ------------------------------------ */

#define FT_SIGNAL_PER_BAR      100
#define FT_SIGNAL_BATTLE_START 50
#define FT_SIGNAL_MAX_BARS     4

#define FT_SIGNAL_GAIN_ATTACK      10
#define FT_SIGNAL_GAIN_LOW_CHARGE  15
#define FT_SIGNAL_GAIN_LAST_CHARGE 20
#define FT_SIGNAL_GAIN_ENEMY_TURN  10
#define FT_SIGNAL_GAIN_FOCUS       35
#define FT_SIGNAL_GAIN_DEEP_FOCUS  5

/* ---- Signal Library (DESIGN.md 4.5) ---------------------------------- */

#define FT_SIGLIB_SLOTS       4
#define FT_SIGLIB_REPLAY_PCT  75

/* ---- Battle sizing --------------------------------------------------- */

#define FT_MAX_ENEMIES   3  /* what fits across a 128px arena at 16px each */
#define FT_MAX_ACTORS    (FT_MAX_ENEMIES + 1)
#define FT_MAX_INSTALLED 12

/* ---- Ratings (DESIGN.md 4.3) ----------------------------------------- */

typedef enum {
    FT_RATING_MISS = 0,
    FT_RATING_NICE,
    FT_RATING_GOOD,
    FT_RATING_GREAT,
    FT_RATING_AMAZING,
    FT_RATING_EXCELLENT,
    FT_RATING_COUNT
} FtRating;

/* ---- Guarding (DESIGN.md 4.5) ---------------------------------------- */

typedef enum {
    FT_GUARD_NONE = 0,
    FT_GUARD_JAM,     /* halves damage, nullifies the payload */
    FT_GUARD_CAPTURE  /* frame-perfect: zero damage, writes to Signal Library */
} FtGuard;

typedef enum {
    FT_CLASS_NORMAL = 0,  /* jammable and capturable */
    FT_CLASS_GUARDED,     /* jammable, not capturable */
    FT_CLASS_UNDODGEABLE  /* neither */
} FtAttackClass;

/* How an attack reaches its target. Drives the attribute locks below. */
typedef enum {
    FT_DELIVERY_CONTACT = 0,  /* NFC, iButton — cannot reach AIRBORNE */
    FT_DELIVERY_BROADCAST,    /* Sub-GHz, BLE — does nothing to ENCRYPTED */
    FT_DELIVERY_DIRECTED,     /* Infrared, RFID — reaches anything in front */
    FT_DELIVERY_NONE          /* support actions that never target */
} FtDelivery;

/* ---- Enemy attributes (DESIGN.md 4.8) -------------------------------- */

#define FT_ATTR_AIRBORNE  (1u << 0) /* contact attacks cannot reach it */
#define FT_ATTR_ENCRYPTED (1u << 1) /* broadcast attacks do nothing, refund RAM */
#define FT_ATTR_FAST      (1u << 2) /* acts before the player */
#define FT_ATTR_JAMMER    (1u << 3) /* locks the SP meter */

/* Never attacks, and nothing behind it can be touched while it stands. The
 * whole fight becomes "get through this first". */
#define FT_ATTR_BULWARK   (1u << 4)

/* Sits the fight out while anything else is alive, then wakes up and is the
 * hardest thing on the board. Clearing the room is what starts the fight. */
#define FT_ATTR_SLEEPER   (1u << 5)

/* ---- Status payloads ------------------------------------------------- */

typedef enum {
    FT_PAYLOAD_NONE = 0,
    FT_PAYLOAD_CORRUPT,  /* damage over time */
    FT_PAYLOAD_DRAIN,    /* RAM loss per turn */
    FT_PAYLOAD_STALL,    /* may lose the turn */
    FT_PAYLOAD_COUNT
} FtPayload;

/* ---- Input timing windows (DESIGN.md 4.4) ---------------------------- */

/* Measured backwards from the impact frame. Hard Mode halves both. */
#define FT_JAM_WINDOW_MS     150
#define FT_CAPTURE_WINDOW_MS 50

/* Action command: tolerance around the perfect moment, per rating band. */
#define FT_BAND_EXCELLENT_MS 40
#define FT_BAND_GREAT_MS     80
#define FT_BAND_GOOD_MS      130
#define FT_BAND_NICE_MS      190

/* ---- Battle pacing --------------------------------------------------- */

/* The player acts this many times per enemy round.
 *
 * Without it a fight is one player action against N foe actions, so a group of
 * three simply deletes a level-one character before they can respond — the
 * balance simulator measured 0% wins at low skill. The reference solves the
 * same problem the same way: a lone player gets two turns to the enemy's one. */
#define FT_PLAYER_TURNS_PER_ROUND 2

/* A "get set" beat before either timing bar starts moving. The bar and its
 * target zones are already on screen, so the player can see what they are
 * aiming at before the cursor is released. Presses during this beat are
 * ignored rather than penalised. */
#define FT_READY_MS 500

/* Both sweeps are short on purpose. The bar that shows them is only ~120px
 * wide, so a longer sweep makes every timing band too few pixels to read. */
#define FT_ACTION_WINDOW_MS    700  /* action command bar sweep */
#define FT_TELEGRAPH_MS        800  /* enemy wind-up before impact */
/* A resolved action plays out in two parts: the sprites act, and only then
 * does the result popup appear. Without the split the popup covers the arena
 * for the whole hold and the animation is never seen. */
/* Staged: wind-up, emit, travel, impact, recover. Slow enough to actually
 * watch on a 128x64 panel — the first pass was a 320ms twitch. */
#define FT_ANIM_MS             900
#define FT_IMPACT_HOLD_MS     1450  /* animation, then the popup */

/* How long the action-command cursor stays frozen and flashing at the point
 * it was stopped, before the action resolves. */
#define FT_LOCK_HOLD_MS        420

/* Taking a hit: both fighters flicker, then an iris closes to black, holds,
 * and opens again onto the same fight. Measured from the strike frame. */
#define FT_FLICKER_MS   180
/* The wipe between the overworld and a fight. Longer than the hit iris: this
 * one is a scene change, not a flinch. */
#define FT_WIPE_CLOSE_MS 260
#define FT_WIPE_OPEN_MS  260

/* The wipe between the overworld and a fight. Longer than the hit iris: this
 * one is a scene change, not a flinch. */
#define FT_WIPE_CLOSE_MS 260
#define FT_WIPE_OPEN_MS  260

#define FT_IRIS_CLOSE_MS 220
#define FT_IRIS_HOLD_MS  500
#define FT_IRIS_OPEN_MS  220

/* A hit therefore holds longer than a jam or a capture, which show no iris. */
#define FT_IMPACT_HOLD_HIT_MS 2400

/* Stage boundaries as a fraction of FT_ANIM_MS, in 0..255 progress units. */
#define FT_ANIM_WINDUP  70
#define FT_ANIM_EMIT    110
#define FT_ANIM_STRIKE  195
#define FT_ANIM_RECOVER 255
#define FT_OUTCOME_HOLD_MS    1400  /* win/lose banner */

#endif /* FT_TYPES_H */
