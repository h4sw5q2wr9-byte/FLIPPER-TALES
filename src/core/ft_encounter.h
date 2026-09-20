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
    FT_PHASE_TELEGRAPH,  /* enemy winding up; the guard window is open */
    FT_PHASE_IMPACT,     /* showing what the enemy's action did */
    FT_PHASE_DRAIN,      /* rolling Charge settling toward its target */
    FT_PHASE_WIN,
    FT_PHASE_LOSE
} FtPhase;

typedef enum {
    FT_ACTION_BROADCAST = 0,
    FT_ACTION_CONTACT,
    FT_ACTION_DEFEND,
    FT_ACTION_FOCUS,
    FT_ACTION_COUNT
} FtAction2;

typedef struct {
    FtPhase  phase;
    uint32_t phase_ms;

    FtStats          stats;
    FtRoll           roll;
    FtSignal         signal;
    FtSignalLibrary  lib;
    FtLoadout        loadout;
    FtLoadoutEffects fx;

    FtEnemyId enemy_id;
    int16_t   enemy_charge;
    int16_t   enemy_charge_max;

    uint8_t menu_index;
    bool    defending;

    /* Action command state. */
    bool     action_pressed;
    uint32_t action_press_ms;
    FtRating last_rating;

    /* Guard state. */
    uint8_t  enemy_attack_index;
    bool     guard_pressed;
    uint32_t guard_press_ms;
    FtGuard  last_guard;

    FtHitResult last_player_hit;
    FtHitResult last_enemy_hit;
    bool        last_capture_was_new;

    FtRng rng;
} FtEncounter;

/* ---- Pure timing helpers (the part worth testing hardest) ------------- */

/* Which guard a press at this many ms before impact earns.
 * Negative means the press came after the hit landed. */
FtGuard ft_guard_from_timing(int32_t ms_before_impact, bool hard_mode);

/* Rating for an action command press this far from the perfect moment.
 * The sign of the offset does not matter, only the distance. */
FtRating ft_rating_from_timing(int32_t ms_from_perfect);

/* ---- Encounter ------------------------------------------------------- */

void ft_encounter_init(FtEncounter* e, FtEnemyId enemy, const FtLoadout* lo, uint32_t seed);

/* Advance by dt_ms. Drives every phase transition. */
void ft_encounter_tick(FtEncounter* e, uint32_t dt_ms);

/* OK pressed. Meaning depends on the phase: confirm, action command, or guard. */
void ft_encounter_press_ok(FtEncounter* e);

/* Move the action menu. Only meaningful during FT_PHASE_MENU. */
void ft_encounter_menu_move(FtEncounter* e, int8_t delta);

/* Is this menu entry usable right now? Locked modules are shown but refused,
 * so the player learns the attribute rather than being silently denied. */
bool ft_encounter_action_available(const FtEncounter* e, FtAction2 action);

const FtEnemy* ft_encounter_enemy(const FtEncounter* e);
bool           ft_encounter_over(const FtEncounter* e);

/* The attack currently being telegraphed, or NULL outside the wind-up. */
const FtAttack* ft_encounter_incoming(const FtEncounter* e);

#endif /* FT_ENCOUNTER_H */
