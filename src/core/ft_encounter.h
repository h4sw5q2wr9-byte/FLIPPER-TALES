/* The battle state machine.
 *
 * Lives in core, not app, because it is driven purely by elapsed milliseconds:
 * nothing here touches the canvas or the input driver, so the whole battle
 * flow — including the guard windows — is host-testable. src/app only renders
 * this struct and feeds it dt and button presses. */
#ifndef FT_ENCOUNTER_H
#define FT_ENCOUNTER_H

#include "ft_combat.h"
#include "ft_data.h"
#include "ft_progress.h"
#include "ft_rng.h"
#include "ft_roll.h"
#include "ft_signal.h"
#include "ft_types.h"

typedef enum {
    FT_PHASE_MENU = 0,   /* choosing an action; the roll is paused here */
    FT_PHASE_PLAYER_ACT, /* action command sweeping */
    FT_PHASE_RESULT,     /* showing what the player's action did */
    FT_PHASE_TELEGRAPH,  /* one foe winding up; the guard window is open */
    FT_PHASE_IMPACT,     /* showing what that foe's action did */
    FT_PHASE_DRAIN,      /* rolling Charge settling toward its target */
    FT_PHASE_WIN,
    FT_PHASE_LOSE
} FtPhase;

typedef enum {
    FT_ACTION_BROADCAST = 0, /* Sub-GHz: every foe, weaker per hit */
    FT_ACTION_CONTACT,       /* NFC: one foe, strong, halves its shield */
    FT_ACTION_DEFEND,        /* brace: shield for the turn, recover RAM */
    FT_ACTION_FOCUS,         /* charge the Signal meter */
    FT_ACTION_SIGNAL,        /* spend a bar to replay a captured attack */
    FT_ACTION_COUNT
} FtAction2;

/* Bracing grants a real shield for the turn, not just a slower drain. Without
 * this, Defend is never worth a turn. */
#define FT_DEFEND_SHIELD 2
#define FT_DEFEND_RAM    1

/* A replayed signal costs this many whole bars. */
#define FT_SIGNAL_COST_BARS 1

/* One enemy on the board. */
typedef struct {
    FtEnemyId id;
    int16_t   charge;
    int16_t   charge_max;
    uint8_t   attack_index; /* what it is winding up, while it acts */
} FtFoe;

typedef struct {
    FtPhase  phase;
    uint32_t phase_ms;

    FtStats          stats;
    FtRoll           roll;
    FtSignal         signal;
    FtSignalLibrary  lib;
    FtLoadout        loadout;
    FtLoadoutEffects fx;

    FtFoe   foes[FT_MAX_ENEMIES];
    uint8_t foe_count;
    uint8_t target;     /* player's chosen foe for single-target actions */
    uint8_t acting_foe; /* whose turn it is during TELEGRAPH and IMPACT */

    uint8_t menu_index;
    bool    defending;

    /* Action command state. */
    bool     action_pressed;
    uint32_t action_press_ms;
    FtRating last_rating;

    /* Guard state. */
    bool     guard_pressed;
    uint32_t guard_press_ms;
    FtGuard  last_guard;

    /* Per-foe results, so a broadcast can show what it did to each of them. */
    FtHitResult foe_hits[FT_MAX_ENEMIES];
    bool        foe_hit_valid[FT_MAX_ENEMIES];

    FtHitResult last_player_hit; /* headline result, for the popup */
    FtHitResult last_enemy_hit;
    bool        last_capture_was_new;
    bool        last_was_replay;
    int16_t     last_total_damage;

    bool coach;

    FtRng rng;
} FtEncounter;

/* ---- Pure timing helpers --------------------------------------------- */

FtGuard  ft_guard_from_timing(int32_t ms_before_impact, bool hard_mode);
FtRating ft_rating_from_timing(int32_t ms_from_perfect);

/* ---- Lifecycle ------------------------------------------------------- */

/* Up to FT_MAX_ENEMIES foes, laid out and resolved left to right. */
void ft_encounter_init(
    FtEncounter*     e,
    const FtEnemyId* foes,
    uint8_t          count,
    const FtLoadout* lo,
    uint32_t         seed);

/* Convenience for a duel. */
void ft_encounter_init_single(
    FtEncounter* e, FtEnemyId foe, const FtLoadout* lo, uint32_t seed);

void ft_encounter_tick(FtEncounter* e, uint32_t dt_ms);
void ft_encounter_press_ok(FtEncounter* e);

void ft_encounter_menu_move(FtEncounter* e, int8_t delta);

/* Cycle the target among living foes. Only meaningful during FT_PHASE_MENU. */
void ft_encounter_target_move(FtEncounter* e, int8_t delta);

/* Usable right now? Unusable entries are shown and refused, never hidden, so
 * the player learns the rule instead of losing the option. */
bool ft_encounter_action_available(const FtEncounter* e, FtAction2 action);

/* Why an action is unusable, in at most 20 characters, or NULL if it is fine. */
const char* ft_encounter_action_block(const FtEncounter* e, FtAction2 action);

bool ft_encounter_over(const FtEncounter* e);

/* ---- Foes ------------------------------------------------------------ */

bool           ft_encounter_foe_alive(const FtEncounter* e, uint8_t i);
uint8_t        ft_encounter_living(const FtEncounter* e);
const FtEnemy* ft_encounter_foe(const FtEncounter* e, uint8_t i);

/* The player's current target. Always a living foe while any remain. */
uint8_t ft_encounter_target(const FtEncounter* e);

/* The foe that is currently acting. */
const FtEnemy* ft_encounter_enemy(const FtEncounter* e);

/* The attack currently being telegraphed, or NULL outside the wind-up. */
const FtAttack* ft_encounter_incoming(const FtEncounter* e);

/* The captured attack a SIGNAL action would replay, or NULL if none. */
const FtAttack* ft_encounter_replay_attack(const FtEncounter* e);

/* Does this action strike every foe at once? */
bool ft_encounter_action_is_broadcast(const FtEncounter* e, FtAction2 action);

/* ---- Pacing ---------------------------------------------------------- */

bool     ft_encounter_in_ready(const FtEncounter* e);
uint32_t ft_encounter_sweep_ms(const FtEncounter* e);
uint32_t ft_encounter_sweep_window(const FtEncounter* e);

bool    ft_encounter_in_anim(const FtEncounter* e);
uint8_t ft_encounter_anim_progress(const FtEncounter* e);

#endif /* FT_ENCOUNTER_H */
