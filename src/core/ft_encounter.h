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

/* The action menu is two levels: a minimal bar of three, and an attack panel
 * listing the modules by full name. Five bare abbreviations in a row read as
 * one cramped string and hid what each actually was. */
typedef enum {
    FT_MENU_ROOT = 0,
    FT_MENU_ATTACK
} FtMenuLevel;

typedef enum {
    FT_ROOT_ATTACK = 0,
    FT_ROOT_DEFEND,
    FT_ROOT_FOCUS,
    FT_ROOT_COUNT
} FtRootItem;

#define FT_ATTACK_COUNT 3

typedef enum {
    FT_ACTION_BROADCAST = 0, /* Sub-GHz: every foe, weaker per hit */
    FT_ACTION_CONTACT,       /* NFC: one foe, strong, halves its shield */
    FT_ACTION_DEFEND,        /* brace: shield for the turn, recover RAM */
    FT_ACTION_FOCUS,         /* charge the Signal meter */
    FT_ACTION_SIGNAL,        /* spend a bar to replay a captured attack */
    FT_ACTION_COUNT
} FtAction2;

/* Attack-panel entries, in order. */
extern const FtAction2 FT_ATTACK_ITEMS[FT_ATTACK_COUNT];

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
    uint8_t acting_foe; /* whose turn it is during TELEGRAPH and IMPACT */

    /* menu_index is the resolved FtAction2 the player is about to take.
     * menu_level and the two cursors are what the UI is actually showing. */
    uint8_t     menu_index;
    FtMenuLevel menu_level;
    uint8_t     root_index;
    uint8_t     attack_index;

    bool defending;

    /* Player actions taken this battle. Foes only act on every other one —
     * see FT_PLAYER_TURNS_PER_ROUND. */
    uint16_t player_turns;

    /* Action command state. The sweep stops the moment it is pressed, so the
     * cursor can be shown frozen where it landed. */
    bool     action_pressed;
    uint32_t action_locked_ms; /* time since the press */
    uint32_t action_press_ms;
    FtRating last_rating;

    /* Guard state. */
    bool     guard_pressed;
    uint32_t guard_press_ms;
    FtGuard  last_guard;

    /* Per-foe results, so a broadcast can show what it did to each of them. */
    FtHitResult foe_hits[FT_MAX_ENEMIES];
    bool        foe_hit_valid[FT_MAX_ENEMIES];

    /* Charge before the current action landed. The renderer shows this until
     * the strike frame, so a foe does not drop dead before the attack that
     * killed it has visibly reached it. */
    int16_t foe_charge_before[FT_MAX_ENEMIES];

    FtHitResult last_player_hit; /* headline result, for the popup */
    FtHitResult last_enemy_hit;
    bool        last_capture_was_new;
    bool        last_was_replay;
    int16_t     last_total_damage;

    bool coach;

    FtRng rng;
} FtEncounter;

/* ---- Pure timing helpers --------------------------------------------- */

/* Which guard a press this many ms before impact earns.
 *
 * A GUARDED attack cannot be captured, so its jam window shrinks to what the
 * capture window would have been: losing the reward should cost precision,
 * not just remove an option. */
FtGuard ft_guard_from_timing(int32_t ms_before_impact, bool hard_mode, FtAttackClass klass);

/* Width of the jam window for this attack class, in ms. */
uint32_t ft_jam_window_ms(bool hard_mode, FtAttackClass klass);
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

/* Move within the current menu level. */
void ft_encounter_menu_move(FtEncounter* e, int8_t delta);

/* Open the attack panel, or take the highlighted root action. */
void ft_encounter_menu_confirm(FtEncounter* e);

/* Close the attack panel. Returns false when already at the root, so the app
 * knows the press should open the pause menu instead. */
bool ft_encounter_menu_back(FtEncounter* e);

/* Full name of an attack-panel entry, for the panel. */
const char* ft_action_name(FtAction2 action);


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


/* The foe that is currently acting. */
const FtEnemy* ft_encounter_enemy(const FtEncounter* e);

/* The attack currently being telegraphed, or NULL outside the wind-up. */
const FtAttack* ft_encounter_incoming(const FtEncounter* e);

/* The captured attack a SIGNAL action would replay, or NULL if none. */
const FtAttack* ft_encounter_replay_attack(const FtEncounter* e);

/* Does this action strike every foe at once? */
bool ft_encounter_action_is_broadcast(const FtEncounter* e, FtAction2 action);

/* ---- Hit transition ---------------------------------------------------- */

typedef enum {
    FT_HIT_FX_NONE = 0,
    FT_HIT_FX_FLICKER /* both fighters strobe */
} FtHitFxStage;

typedef struct {
    FtHitFxStage stage;
    uint8_t      amount; /* unused; kept so callers need not special-case */
    bool         strobe; /* invert the fighters this frame */
} FtHitFx;

/* The flinch on a hit that landed. A jam or a capture returns
 * FT_HIT_FX_NONE. There is deliberately no screen transition here: the iris
 * belongs to changing scene, not to taking damage. */
FtHitFx ft_encounter_hit_fx(const FtEncounter* e);

/* How long the current impact holds. A landed hit runs the iris, so it needs
 * longer than a jam. */
uint32_t ft_encounter_impact_hold(const FtEncounter* e);

/* ---- Reach and targeting ----------------------------------------------- */

/* Can this action actually land on foe i? Attributes answer this; they never
 * answer whether the action may be chosen. */
bool ft_encounter_can_reach(const FtEncounter* e, FtAction2 action, uint8_t i);

/* The foe a single-target action will really hit: the nearest living one the
 * action can reach. There is no cursor — targeting is automatic. */
uint8_t ft_encounter_effective_target(const FtEncounter* e, FtAction2 action);

/* ---- The scene wipe ---------------------------------------------------- */

/* Entering and leaving a fight close the screen and open it again. Same shape
 * as the hit iris, so the two read as one piece of vocabulary: the screen
 * shutting means something just changed. This one is free of any encounter,
 * because at the moment it starts there may not be one yet. */
typedef enum {
    FT_WIPE_NONE = 0,
    FT_WIPE_CLOSING,
    FT_WIPE_OPENING
} FtWipeStage;

typedef struct {
    FtWipeStage stage;
    uint8_t     amount; /* 0 clear, 255 shut */
} FtWipe;

#define FT_WIPE_SWAP FT_WIPE_CLOSE_MS
#define FT_WIPE_MS   (FT_WIPE_CLOSE_MS + FT_WIPE_OPEN_MS)

FtWipe ft_wipe_at(uint32_t ms);

/* ---- Per-foe hit timing ------------------------------------------------ */

/* Anim progress (0-255) at which foe `i` takes the current action's damage.
 * A broadcast sweeps across the row, so foes are struck in order as the
 * signal reaches them rather than all at once. */
uint8_t ft_encounter_foe_hit_at(const FtEncounter* e, uint8_t i);

/* Charge to draw for a foe right now — its pre-hit value while the attack is
 * still travelling, its real value afterwards. */
int16_t ft_encounter_foe_shown_charge(const FtEncounter* e, uint8_t i);

/* Should this foe still be drawn? A foe killed by the action in flight stays
 * on screen until the strike frame. */
/* How far through falling over a foe this attack just killed is: 0 before the
 * hit reaches it and for anything still standing, 255 once it is gone. A foe
 * used to simply blink out of existence on the frame its bar emptied. */
uint8_t ft_encounter_foe_defeat(const FtEncounter* e, uint8_t i);

bool ft_encounter_foe_visible(const FtEncounter* e, uint8_t i);

/* ---- Pacing ---------------------------------------------------------- */

bool     ft_encounter_in_ready(const FtEncounter* e);
uint32_t ft_encounter_sweep_ms(const FtEncounter* e);
uint32_t ft_encounter_sweep_window(const FtEncounter* e);

bool    ft_encounter_in_anim(const FtEncounter* e);
uint8_t ft_encounter_anim_progress(const FtEncounter* e);

#endif /* FT_ENCOUNTER_H */
