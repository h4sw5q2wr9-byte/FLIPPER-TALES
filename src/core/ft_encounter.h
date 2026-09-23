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
#include "ft_item.h"
#include "ft_priority.h"
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
    FT_ACTION_DEFLECT,       /* spend a bar to send their next hits back */
    FT_ACTION_ITEM,          /* eat something out of your pockets */
    FT_ACTION_INFRARED,      /* the nearest foe, whatever it is; only once you have it */
    FT_ACTION_COUNT
} FtAction2;

/* Bracing grants a real shield for the turn, not just a slower drain. Without
 * this, Defend is never worth a turn. */
#define FT_DEFEND_SHIELD 2
#define FT_DEFEND_RAM    1

/* Bracing also patches you up a little.
 *
 * There was no way to recover Charge in a fight at all — not an item, not a
 * skill, nothing — so every fight was pure attrition and attacking was always
 * the right answer. "I never use Protect and Focus" is the correct read of a
 * game where defending only delays the same loss. A small heal makes the turn
 * a real choice: spend it staying alive, or spend it ending the fight. */
#define FT_DEFEND_HEAL 2

/* Arming the deflect costs this many whole bars, on top of the action. */
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

    FtFoe   foes[FT_MAX_ENEMIES];
    uint8_t foe_count;
    uint8_t acting_foe; /* whose turn it is during TELEGRAPH and IMPACT */

    /* The action the player is about to take. One flat cursor: the menu used
     * to be two levels with the attack modules behind a drill-down, which
     * hid two of the five actions and gave the screen two rows of buttons. */
    uint8_t menu_index;

    bool defending;

    /* Rounds remaining on each status payload.
     *
     * CORRUPT, DRAIN and STALL were in the data from the start and none of
     * them did anything: ft_resolve_hit even computed payload_applied and
     * nobody read it. An attack that says it corrupts you and does not is
     * worse than one that never claimed to. */
    uint8_t status[FT_PAYLOAD_COUNT];

    /* Player actions taken this battle. Foes only act on every other one —
     * see FT_PLAYER_TURNS_PER_ROUND. */
    uint16_t player_turns;

    /* True while the round's FAST foes are taking their pre-emptive turns.
     * A round runs: fast foes, the player's turns, then everything else. */
    bool fast_phase;

    /* Action command state. The sweep stops the moment it is pressed, so the
     * cursor can be shown frozen where it landed. */
    bool     action_pressed;
    uint32_t action_locked_ms; /* time since the press */
    uint32_t action_press_ms;
    FtRating last_rating;

    /* Guard state. The press freezes the cursor where it landed, exactly as
     * the strike check does, and the lock clock drives its flash. */
    bool     guard_pressed;
    uint32_t guard_press_ms;
    uint32_t guard_locked_ms;
    FtGuard  last_guard;

    /* How far before the impact frame the last guard was pressed, in ms.
     * Negative when nothing was pressed at all. This is the aftermath: the
     * exact distance between what you did and what you were aiming at. */
    int32_t  last_guard_offset;

    /* Per-foe results, so a broadcast can show what it did to each of them. */
    FtHitResult foe_hits[FT_MAX_ENEMIES];
    bool        foe_hit_valid[FT_MAX_ENEMIES];

    /* Charge before the current action landed. The renderer shows this until
     * the strike frame, so a foe does not drop dead before the attack that
     * killed it has visibly reached it. */
    int16_t foe_charge_before[FT_MAX_ENEMIES];

    /* The deflect stance.
     *
     * Armed by FT_ACTION_DEFLECT and held until the player's next turn comes
     * round, which is one unbroken block of enemy turns however the round is
     * ordered. While it is up, every guard sends the attack back. */
    bool    deflect_armed;
    bool    last_deflect_fired;  /* the hit just resolved was sent back */
    int16_t last_deflect_damage; /* and this is what it did */

    FtHitResult last_player_hit; /* headline result, for the popup */
    FtHitResult last_enemy_hit;
    int16_t     last_total_damage;

    /* A foe with FT_ATTR_RETREATS got away this fight. The app says so
     * instead of "Cleared.", because it will be back. */
    bool        retreated;

    /* Whether you have Infrared (Ma Rivet's clicker). Without it the action
     * is not in the menu at all. The app sets it when a fight starts. */
    bool        infrared;

    bool coach;

    /* What you brought with you. Copied in and out like the stats, so a
     * fight cannot hand you something you did not walk in carrying. */
    FtPockets pockets;
    uint8_t   item_index; /* which kind the picker is on */
    FtItemId  last_item;  /* what was just used, for the popup */

    FtRng rng;
} FtEncounter;

/* ---- Pure timing helpers --------------------------------------------- */

/* Which guard a press this many ms before impact earns.
 *
 * `hard_mode` is always false at the moment: it was a Card, and the Cards
 * system is gone. The parameter stays because halving both windows is the
 * whole of what a difficulty option would need, and both branches are
 * tested — deleting it would throw away working, covered behaviour to save
 * one argument.
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
    FtEncounter* e, const FtEnemyId* foes, uint8_t count, uint32_t seed);

/* Convenience for a duel. */
void ft_encounter_init_single(FtEncounter* e, FtEnemyId foe, uint32_t seed);

/* Hand the opening turn to the foes. Used when one of them reached the
 * player rather than the other way round. */
void ft_encounter_enemy_opens(FtEncounter* e);

void ft_encounter_tick(FtEncounter* e, uint32_t dt_ms);
void ft_encounter_press_ok(FtEncounter* e);

/* Move within the current menu level. */
void ft_encounter_menu_move(FtEncounter* e, int8_t delta);

/* Take the highlighted action. Kept as its own name so the app does not have
 * to know that confirming is just a press. */
void ft_encounter_menu_confirm(FtEncounter* e);

/* Full name of an attack-panel entry, for the panel. */
const char* ft_action_name(FtAction2 action);


/* Usable right now? Unusable entries are shown and refused, never hidden, so
 * the player learns the rule instead of losing the option. */
bool ft_encounter_action_available(const FtEncounter* e, FtAction2 action);

/* MP this action spends. Only the strong module costs any. */
uint8_t ft_encounter_action_cost(const FtEncounter* e, FtAction2 action);

/* Why an action is unusable, in at most 20 characters, or NULL if it is fine. */
const char* ft_encounter_action_block(const FtEncounter* e, FtAction2 action);

bool ft_encounter_over(const FtEncounter* e);

/* ---- Status ------------------------------------------------------------ */

/* Rounds left on a payload, 0 when clear. */
uint8_t ft_encounter_status(const FtEncounter* e, FtPayload p);

/* A short tag for whatever is on the player, or NULL when nothing is. Only
 * one is shown: three at once would need a row the screen does not have, and
 * the worst one is the one worth knowing about. */
const char* ft_encounter_status_tag(const FtEncounter* e);

/* Player actions this round. A stall halves them. */
uint8_t ft_encounter_turns_this_round(const FtEncounter* e);

/* ---- Pockets ----------------------------------------------------------- */

/* Move the item picker. Wraps over the kinds actually carried. */
void ft_encounter_item_move(FtEncounter* e, int8_t delta);

/* The kind the picker is on, or FT_ITEM_COUNT when pockets are empty. */
FtItemId ft_encounter_item_at(const FtEncounter* e);

/* ---- Guard aftermath --------------------------------------------------- */

/* Milliseconds before impact that the last guard landed, or -1 for no press.
 * Smaller is later and therefore better: inside the capture window is a
 * capture, inside the jam window a jam, anything wider has lapsed. */
int32_t ft_encounter_guard_offset(const FtEncounter* e);

/* The same figure while the wind-up is still running, so the marker can be
 * drawn frozen at the press with the impact edge closing on it. */
int32_t ft_encounter_guard_gap(const FtEncounter* e);

/* ---- Foes ------------------------------------------------------------ */

bool           ft_encounter_foe_alive(const FtEncounter* e, uint8_t i);

/* Is this foe taking turns? A BULWARK never does; a SLEEPER only once it is
 * the last one standing. The arena marks the ones that are not. */
bool           ft_encounter_foe_awake(const FtEncounter* e, uint8_t i);
uint8_t        ft_encounter_living(const FtEncounter* e);
const FtEnemy* ft_encounter_foe(const FtEncounter* e, uint8_t i);


/* The foe that is currently acting. */
const FtEnemy* ft_encounter_enemy(const FtEncounter* e);

/* The attack currently being telegraphed, or NULL outside the wind-up. */
const FtAttack* ft_encounter_incoming(const FtEncounter* e);

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

/* XP this fight is worth, after the underlevelling taper on each foe. Zero
 * unless it was actually won. */
int16_t ft_encounter_xp(const FtEncounter* e);

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
