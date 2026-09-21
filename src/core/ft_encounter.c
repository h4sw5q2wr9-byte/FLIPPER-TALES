#include "ft_encounter.h"

/* ---- Timing ---------------------------------------------------------- */

FtGuard ft_guard_from_timing(int32_t ms_before_impact, bool hard_mode) {
    /* A press after the hit landed is simply late. */
    if(ms_before_impact < 0) return FT_GUARD_NONE;

    int32_t capture = FT_CAPTURE_WINDOW_MS;
    int32_t jam = FT_JAM_WINDOW_MS;
    if(hard_mode) {
        capture /= 2;
        jam /= 2;
    }

    if(ms_before_impact <= capture) return FT_GUARD_CAPTURE;
    if(ms_before_impact <= jam) return FT_GUARD_JAM;

    /* Too early: the guard has already lapsed by the time the hit arrives. */
    return FT_GUARD_NONE;
}

FtRating ft_rating_from_timing(int32_t ms_from_perfect) {
    const int32_t d = (ms_from_perfect < 0) ? -ms_from_perfect : ms_from_perfect;

    if(d <= FT_BAND_EXCELLENT_MS) return FT_RATING_EXCELLENT;
    if(d <= FT_BAND_GREAT_MS) return FT_RATING_AMAZING;
    if(d <= FT_BAND_GOOD_MS) return FT_RATING_GREAT;
    if(d <= FT_BAND_NICE_MS) return FT_RATING_GOOD;

    return FT_RATING_MISS;
}

/* ---- Ready beat ------------------------------------------------------ */

bool ft_encounter_in_ready(const FtEncounter* e) {
    if(e->phase != FT_PHASE_PLAYER_ACT && e->phase != FT_PHASE_TELEGRAPH) return false;
    return e->phase_ms < FT_READY_MS;
}

uint32_t ft_encounter_sweep_window(const FtEncounter* e) {
    if(e->phase == FT_PHASE_PLAYER_ACT) return FT_ACTION_WINDOW_MS;
    if(e->phase == FT_PHASE_TELEGRAPH) return FT_TELEGRAPH_MS;
    return 0u;
}

uint32_t ft_encounter_sweep_ms(const FtEncounter* e) {
    if(e->phase_ms <= FT_READY_MS) return 0u;

    const uint32_t elapsed = e->phase_ms - FT_READY_MS;
    const uint32_t window = ft_encounter_sweep_window(e);

    return (window && elapsed > window) ? window : elapsed;
}

/* ---- Setup ----------------------------------------------------------- */

const FtEnemy* ft_encounter_enemy(const FtEncounter* e) {
    return &FT_ENEMIES[e->enemy_id];
}

bool ft_encounter_over(const FtEncounter* e) {
    return e->phase == FT_PHASE_WIN || e->phase == FT_PHASE_LOSE;
}

void ft_encounter_init(FtEncounter* e, FtEnemyId enemy, const FtLoadout* lo, uint32_t seed) {
    const FtEnemy* proto = &FT_ENEMIES[enemy];

    e->loadout = *lo;
    e->fx = ft_loadout_effects(&e->loadout);

    ft_stats_init(&e->stats);
    e->stats.charge_max = (int16_t)(e->stats.charge_max + e->fx.charge_max_bonus);
    e->stats.charge = e->stats.charge_max;
    e->stats.flash_used = e->fx.flash_used;

    ft_roll_init(&e->roll, e->stats.charge);
    ft_signal_init(&e->signal, 1);
    ft_signal_battle_start(&e->signal);
    ft_siglib_init(&e->lib);

    /* A JAMMER present at the start locks the meter for the whole battle. */
    e->signal.locked = (proto->attrs & FT_ATTR_JAMMER) != 0u;

    e->enemy_id = enemy;
    e->enemy_charge = proto->charge;
    e->enemy_charge_max = proto->charge;

    e->phase = FT_PHASE_MENU;
    e->phase_ms = 0;
    e->menu_index = 0;
    e->defending = false;

    e->action_pressed = false;
    e->action_press_ms = 0;
    e->last_rating = FT_RATING_MISS;

    e->enemy_attack_index = 0;
    e->guard_pressed = false;
    e->guard_press_ms = 0;
    e->last_guard = FT_GUARD_NONE;

    const FtHitResult blank = {FT_HIT_OK, 0, false, false, false, 0};
    e->last_player_hit = blank;
    e->last_enemy_hit = blank;
    e->last_capture_was_new = false;

    ft_rng_seed(&e->rng, seed);
}

/* ---- Menu ------------------------------------------------------------ */

bool ft_encounter_action_available(const FtEncounter* e, FtAction2 action) {
    const FtEnemy* en = ft_encounter_enemy(e);

    switch(action) {
    case FT_ACTION_BROADCAST:
        return (en->attrs & FT_ATTR_ENCRYPTED) == 0u;
    case FT_ACTION_CONTACT:
        return (en->attrs & FT_ATTR_AIRBORNE) == 0u;
    case FT_ACTION_DEFEND:
    case FT_ACTION_FOCUS:
        return true;
    default:
        return false;
    }
}

void ft_encounter_menu_move(FtEncounter* e, int8_t delta) {
    if(e->phase != FT_PHASE_MENU) return;

    int16_t idx = (int16_t)(e->menu_index + delta);
    while(idx < 0) idx = (int16_t)(idx + FT_ACTION_COUNT);
    while(idx >= FT_ACTION_COUNT) idx = (int16_t)(idx - FT_ACTION_COUNT);

    e->menu_index = (uint8_t)idx;
}

static const FtAttack* player_attack_for(FtAction2 action) {
    if(action == FT_ACTION_BROADCAST) return &FT_MODULES[FT_MOD_SUBGHZ].attack;
    if(action == FT_ACTION_CONTACT) return &FT_MODULES[FT_MOD_NFC].attack;
    return NULL;
}

/* ---- Resolution ------------------------------------------------------ */

static void enter_phase(FtEncounter* e, FtPhase phase) {
    e->phase = phase;
    e->phase_ms = 0;
}

static void resolve_player_action(FtEncounter* e) {
    const FtAction2 action = (FtAction2)e->menu_index;
    const FtEnemy* en = ft_encounter_enemy(e);

    const FtHitResult blank = {FT_HIT_OK, 0, false, false, false, 0};
    e->last_player_hit = blank;

    if(action == FT_ACTION_DEFEND) {
        e->defending = true;
        e->stats.ram = (int16_t)(e->stats.ram + 1);
        if(e->stats.ram > e->stats.ram_max) e->stats.ram = e->stats.ram_max;
        return;
    }

    if(action == FT_ACTION_FOCUS) {
        ft_signal_add(&e->signal, ft_signal_focus_gain(e->fx.deep_focus_stacks));
        return;
    }

    const FtAttack* atk = player_attack_for(action);
    if(atk == NULL) return;

    /* No press at all during the sweep is a miss. */
    const FtRating rating =
        e->action_pressed ?
            ft_rating_from_timing((int32_t)e->action_press_ms - (FT_ACTION_WINDOW_MS / 2)) :
            FT_RATING_MISS;
    e->last_rating = rating;

    const FtDefender def = {en->shielded, en->attrs};
    const FtHitParams p = {e->fx.atk_up, 0, rating, false, FT_GUARD_NONE, 0};

    e->last_player_hit = ft_resolve_hit(atk, &def, &p);

    if(e->last_player_hit.ram_refund > 0) {
        e->stats.ram = (int16_t)(e->stats.ram + e->last_player_hit.ram_refund);
        if(e->stats.ram > e->stats.ram_max) e->stats.ram = e->stats.ram_max;
    }

    if(e->last_player_hit.damage > 0) {
        e->enemy_charge = (int16_t)(e->enemy_charge - e->last_player_hit.damage);
        ft_signal_add(&e->signal, ft_signal_attack_gain(e->roll.current, e->stats.charge_max));
    }
}

static void choose_enemy_attack(FtEncounter* e) {
    const FtEnemy* en = ft_encounter_enemy(e);
    e->enemy_attack_index = (uint8_t)ft_rng_below(&e->rng, en->attack_count);
    e->guard_pressed = false;
    e->guard_press_ms = 0;
}

const FtAttack* ft_encounter_incoming(const FtEncounter* e) {
    if(e->phase != FT_PHASE_TELEGRAPH && e->phase != FT_PHASE_IMPACT) return NULL;
    return &ft_encounter_enemy(e)->attacks[e->enemy_attack_index];
}

static void resolve_enemy_action(FtEncounter* e) {
    const FtAttack* atk = &ft_encounter_enemy(e)->attacks[e->enemy_attack_index];

    /* The press is recorded as a time within the telegraph; convert it to a
     * distance before impact, which is what the guard windows are defined in. */
    const int32_t before_impact =
        e->guard_pressed ? (int32_t)FT_TELEGRAPH_MS - (int32_t)e->guard_press_ms : -1;

    e->last_guard = ft_guard_from_timing(before_impact, e->fx.hard_mode);

    const FtDefender def = {0, 0};
    const FtHitParams p = {0, 0, FT_RATING_MISS, false, e->last_guard,
                           e->fx.jam_reduction_pct};

    e->last_enemy_hit = ft_resolve_hit(atk, &def, &p);

    e->last_capture_was_new = false;
    if(e->last_enemy_hit.captured) {
        e->last_capture_was_new = ft_siglib_capture(&e->lib, atk->id);
    }

    int16_t damage = e->last_enemy_hit.damage;
    if(e->fx.hard_mode) damage = (int16_t)(damage * 2);

    ft_roll_apply_damage(&e->roll, damage);
    ft_signal_add(&e->signal, FT_SIGNAL_GAIN_ENEMY_TURN);
}

/* ---- Input ----------------------------------------------------------- */

void ft_encounter_press_ok(FtEncounter* e) {
    switch(e->phase) {
    case FT_PHASE_MENU:
        if(!ft_encounter_action_available(e, (FtAction2)e->menu_index)) return;

        e->action_pressed = false;
        e->action_press_ms = 0;
        e->defending = false;

        /* Defend and Focus take no action command, so skip the sweep. */
        if(e->menu_index == FT_ACTION_DEFEND || e->menu_index == FT_ACTION_FOCUS) {
            resolve_player_action(e);
            enter_phase(e, FT_PHASE_RESULT);
        } else {
            enter_phase(e, FT_PHASE_PLAYER_ACT);
        }
        break;

    case FT_PHASE_PLAYER_ACT:
        /* Presses during the ready beat are ignored, not penalised. Mashing
         * still costs you: the first press once the cursor moves lands at the
         * very start of the sweep, nowhere near the target. */
        if(ft_encounter_in_ready(e)) break;

        /* Only the first press counts. */
        if(!e->action_pressed) {
            e->action_pressed = true;
            e->action_press_ms = ft_encounter_sweep_ms(e);
        }
        break;

    case FT_PHASE_TELEGRAPH:
        if(ft_encounter_in_ready(e)) break;

        if(!e->guard_pressed) {
            e->guard_pressed = true;
            e->guard_press_ms = ft_encounter_sweep_ms(e);
        }
        break;

    default:
        break;
    }
}

/* ---- Tick ------------------------------------------------------------ */

/* Milestone 1 simplification: the phase machine always resolves the player
 * before the enemy, so ft_priority (and with it the FAST attribute) does not
 * yet drive turn order. Ordering only becomes observable with more than one
 * enemy on the board, which arrives with the overworld in M2. Tracked in
 * DESIGN.md 7. */

void ft_encounter_tick(FtEncounter* e, uint32_t dt_ms) {
    if(ft_encounter_over(e)) return;

    e->phase_ms += dt_ms;

    switch(e->phase) {
    case FT_PHASE_MENU:
        /* The roll is deliberately paused while the player deliberates, so
         * thinking never costs Charge. */
        break;

    case FT_PHASE_PLAYER_ACT:
        if(e->phase_ms >= FT_READY_MS + FT_ACTION_WINDOW_MS) {
            resolve_player_action(e);
            enter_phase(e, FT_PHASE_RESULT);
        }
        break;

    case FT_PHASE_RESULT:
        if(e->phase_ms >= FT_IMPACT_HOLD_MS) {
            if(e->enemy_charge <= 0) {
                enter_phase(e, FT_PHASE_WIN);
            } else {
                choose_enemy_attack(e);
                enter_phase(e, FT_PHASE_TELEGRAPH);
            }
        }
        break;

    case FT_PHASE_TELEGRAPH:
        if(e->phase_ms >= FT_READY_MS + FT_TELEGRAPH_MS) {
            resolve_enemy_action(e);
            enter_phase(e, FT_PHASE_IMPACT);
        }
        break;

    case FT_PHASE_IMPACT:
        if(e->phase_ms >= FT_IMPACT_HOLD_MS) enter_phase(e, FT_PHASE_DRAIN);
        break;

    case FT_PHASE_DRAIN: {
        const uint32_t interval =
            ft_roll_interval_ms(0, e->defending, e->fx.hard_mode);
        ft_roll_tick(&e->roll, dt_ms, interval);

        e->stats.charge = e->roll.current;

        if(ft_roll_down(&e->roll)) {
            enter_phase(e, FT_PHASE_LOSE);
        } else if(!ft_roll_active(&e->roll)) {
            e->defending = false;
            enter_phase(e, FT_PHASE_MENU);
        }
        break;
    }

    case FT_PHASE_WIN:
    case FT_PHASE_LOSE:
    default:
        break;
    }
}
