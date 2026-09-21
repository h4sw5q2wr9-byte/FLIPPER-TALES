#include "ft_encounter.h"

/* ---- Timing ---------------------------------------------------------- */

uint32_t ft_jam_window_ms(bool hard_mode, FtAttackClass klass) {
    uint32_t jam = FT_JAM_WINDOW_MS;
    uint32_t capture = FT_CAPTURE_WINDOW_MS;

    if(hard_mode) {
        jam /= 2u;
        capture /= 2u;
    }

    /* No capture on offer means no wide window either: a GUARDED attack asks
     * for capture-grade timing to get half its damage off. */
    return (klass == FT_CLASS_GUARDED) ? capture : jam;
}

FtGuard ft_guard_from_timing(int32_t ms_before_impact, bool hard_mode, FtAttackClass klass) {
    /* A press after the hit landed is simply late. */
    if(ms_before_impact < 0) return FT_GUARD_NONE;
    if(klass == FT_CLASS_UNDODGEABLE) return FT_GUARD_NONE;

    int32_t capture = FT_CAPTURE_WINDOW_MS;
    if(hard_mode) capture /= 2;

    const int32_t jam = (int32_t)ft_jam_window_ms(hard_mode, klass);

    if(klass != FT_CLASS_GUARDED && ms_before_impact <= capture) return FT_GUARD_CAPTURE;
    if(ms_before_impact <= jam) return FT_GUARD_JAM;

    /* Too early: the guard has lapsed by the time the hit arrives. */
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

/* ---- Pacing ---------------------------------------------------------- */

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

bool ft_encounter_in_anim(const FtEncounter* e) {
    if(e->phase != FT_PHASE_RESULT && e->phase != FT_PHASE_IMPACT) return false;
    return e->phase_ms < FT_ANIM_MS;
}

uint8_t ft_encounter_anim_progress(const FtEncounter* e) {
    if(e->phase != FT_PHASE_RESULT && e->phase != FT_PHASE_IMPACT) return 255u;
    if(e->phase_ms >= FT_ANIM_MS) return 255u;

    return (uint8_t)((e->phase_ms * 255u) / FT_ANIM_MS);
}

/* ---- Foes ------------------------------------------------------------ */

bool ft_encounter_foe_alive(const FtEncounter* e, uint8_t i) {
    return i < e->foe_count && e->foes[i].charge > 0;
}

uint8_t ft_encounter_living(const FtEncounter* e) {
    uint8_t n = 0;
    for(uint8_t i = 0; i < e->foe_count; i++) {
        if(e->foes[i].charge > 0) n++;
    }
    return n;
}

const FtEnemy* ft_encounter_foe(const FtEncounter* e, uint8_t i) {
    if(i >= e->foe_count) i = 0;
    return &FT_ENEMIES[e->foes[i].id];
}

const FtEnemy* ft_encounter_enemy(const FtEncounter* e) {
    return ft_encounter_foe(e, e->acting_foe);
}

/* Next living foe at or after `from`, or -1 when the row is clear. */
static int next_living(const FtEncounter* e, uint8_t from) {
    for(uint8_t i = from; i < e->foe_count; i++) {
        if(e->foes[i].charge > 0) return (int)i;
    }
    return -1;
}

uint8_t ft_encounter_target(const FtEncounter* e) {
    if(ft_encounter_foe_alive(e, e->target)) return e->target;

    /* The chosen target died; fall through to whoever is left. */
    const int n = next_living(e, 0);
    return (n < 0) ? 0u : (uint8_t)n;
}

void ft_encounter_target_move(FtEncounter* e, int8_t delta) {
    if(e->phase != FT_PHASE_MENU || e->foe_count == 0u) return;
    if(ft_encounter_living(e) <= 1u) return;

    const int8_t step = (delta < 0) ? -1 : 1;
    uint8_t idx = ft_encounter_target(e);

    /* Walk to the next living foe, wrapping. Bounded by foe_count so a board
     * of corpses cannot spin here. */
    for(uint8_t guard = 0; guard < e->foe_count; guard++) {
        const int16_t next = (int16_t)(idx + step);
        idx = (uint8_t)((next < 0) ? (e->foe_count - 1) : (next % e->foe_count));
        if(ft_encounter_foe_alive(e, idx)) break;
    }

    e->target = idx;
}

bool ft_encounter_over(const FtEncounter* e) {
    return e->phase == FT_PHASE_WIN || e->phase == FT_PHASE_LOSE;
}

/* ---- Setup ----------------------------------------------------------- */

void ft_encounter_init(
    FtEncounter*     e,
    const FtEnemyId* foes,
    uint8_t          count,
    const FtLoadout* lo,
    uint32_t         seed) {
    if(count == 0u) count = 1u;
    if(count > FT_MAX_ENEMIES) count = FT_MAX_ENEMIES;

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

    e->foe_count = count;
    bool jammer = false;
    for(uint8_t i = 0; i < count; i++) {
        const FtEnemy* proto = &FT_ENEMIES[foes[i]];
        e->foes[i].id = foes[i];
        e->foes[i].charge = proto->charge;
        e->foes[i].charge_max = proto->charge;
        e->foes[i].attack_index = 0;
        if(proto->attrs & FT_ATTR_JAMMER) jammer = true;

        e->foe_hits[i] = (FtHitResult){FT_HIT_OK, 0, false, false, false, 0};
        e->foe_hit_valid[i] = false;
        e->foe_charge_before[i] = e->foes[i].charge;
    }
    for(uint8_t i = count; i < FT_MAX_ENEMIES; i++) {
        e->foes[i].id = foes[0];
        e->foes[i].charge = 0;
        e->foes[i].charge_max = 0;
        e->foes[i].attack_index = 0;
        e->foe_hit_valid[i] = false;
    }

    /* Any jammer on the board locks the meter for the whole fight. */
    e->signal.locked = jammer;

    e->target = 0;
    e->acting_foe = 0;

    e->phase = FT_PHASE_MENU;
    e->phase_ms = 0;
    e->menu_index = 0;
    e->menu_level = FT_MENU_ROOT;
    e->root_index = FT_ROOT_ATTACK;
    e->attack_index = 0;
    e->defending = false;
    e->player_turns = 0;

    e->action_pressed = false;
    e->action_press_ms = 0;
    e->action_locked_ms = 0;
    e->last_rating = FT_RATING_MISS;

    e->guard_pressed = false;
    e->guard_press_ms = 0;
    e->last_guard = FT_GUARD_NONE;

    const FtHitResult blank = {FT_HIT_OK, 0, false, false, false, 0};
    e->last_player_hit = blank;
    e->last_enemy_hit = blank;
    e->last_capture_was_new = false;
    e->last_was_replay = false;
    e->last_total_damage = 0;

    e->coach = true;

    ft_rng_seed(&e->rng, seed);
}

void ft_encounter_init_single(
    FtEncounter* e, FtEnemyId foe, const FtLoadout* lo, uint32_t seed) {
    const FtEnemyId one[1] = {foe};
    ft_encounter_init(e, one, 1u, lo, seed);
}

/* ---- Actions --------------------------------------------------------- */

const FtAttack* ft_encounter_replay_attack(const FtEncounter* e) {
    return ft_attack_by_id(ft_siglib_latest(&e->lib));
}

bool ft_encounter_action_is_broadcast(const FtEncounter* e, FtAction2 action) {
    if(action == FT_ACTION_BROADCAST) return true;

    if(action == FT_ACTION_SIGNAL) {
        const FtAttack* atk = ft_encounter_replay_attack(e);
        return atk && atk->delivery == FT_DELIVERY_BROADCAST;
    }
    return false;
}

const char* ft_encounter_action_block(const FtEncounter* e, FtAction2 action) {
    switch(action) {
    case FT_ACTION_BROADCAST:
        /* Broadcast reaches everything unless every living foe ignores it. */
        for(uint8_t i = 0; i < e->foe_count; i++) {
            if(e->foes[i].charge > 0 &&
               !(FT_ENEMIES[e->foes[i].id].attrs & FT_ATTR_ENCRYPTED)) {
                return NULL;
            }
        }
        return "Encrypted: use NFC";

    case FT_ACTION_CONTACT:
        if(ft_encounter_foe(e, ft_encounter_target(e))->attrs & FT_ATTR_AIRBORNE) {
            return "Flying: use SUB.";
        }
        return NULL;

    case FT_ACTION_SIGNAL:
        if(ft_encounter_replay_attack(e) == NULL) return "Capture one first.";
        if(e->signal.locked) return "Signal jammed.";
        if(ft_signal_bars(&e->signal) < FT_SIGNAL_COST_BARS) return "Need a full bar.";
        return NULL;

    case FT_ACTION_DEFEND:
    case FT_ACTION_FOCUS:
    default:
        return NULL;
    }
}

bool ft_encounter_action_available(const FtEncounter* e, FtAction2 action) {
    if(action >= FT_ACTION_COUNT) return false;
    return ft_encounter_action_block(e, action) == NULL;
}

const FtAction2 FT_ATTACK_ITEMS[FT_ATTACK_COUNT] = {
    FT_ACTION_BROADCAST,
    FT_ACTION_CONTACT,
    FT_ACTION_SIGNAL,
};

const char* ft_action_name(FtAction2 action) {
    switch(action) {
    case FT_ACTION_BROADCAST: return "Sub-GHz";
    case FT_ACTION_CONTACT:   return "NFC";
    case FT_ACTION_SIGNAL:    return "Signal";
    case FT_ACTION_DEFEND:    return "Protect";
    case FT_ACTION_FOCUS:     return "Focus";
    default:                  return "?";
    }
}

/* Keep menu_index in step with whatever the cursors are pointing at, so every
 * existing rule (availability, descriptions, resolution) keeps working. */
static void sync_menu_index(FtEncounter* e) {
    if(e->menu_level == FT_MENU_ATTACK) {
        e->menu_index = (uint8_t)FT_ATTACK_ITEMS[e->attack_index % FT_ATTACK_COUNT];
        return;
    }

    switch((FtRootItem)e->root_index) {
    case FT_ROOT_DEFEND: e->menu_index = (uint8_t)FT_ACTION_DEFEND; break;
    case FT_ROOT_FOCUS:  e->menu_index = (uint8_t)FT_ACTION_FOCUS; break;
    case FT_ROOT_ATTACK:
    default:
        /* Highlighting Attack previews whichever module is selected. */
        e->menu_index = (uint8_t)FT_ATTACK_ITEMS[e->attack_index % FT_ATTACK_COUNT];
        break;
    }
}

void ft_encounter_menu_move(FtEncounter* e, int8_t delta) {
    if(e->phase != FT_PHASE_MENU) return;

    if(e->menu_level == FT_MENU_ATTACK) {
        int16_t idx = (int16_t)(e->attack_index + delta);
        while(idx < 0) idx = (int16_t)(idx + FT_ATTACK_COUNT);
        while(idx >= FT_ATTACK_COUNT) idx = (int16_t)(idx - FT_ATTACK_COUNT);
        e->attack_index = (uint8_t)idx;
    } else {
        int16_t idx = (int16_t)(e->root_index + delta);
        while(idx < 0) idx = (int16_t)(idx + FT_ROOT_COUNT);
        while(idx >= FT_ROOT_COUNT) idx = (int16_t)(idx - FT_ROOT_COUNT);
        e->root_index = (uint8_t)idx;
    }

    sync_menu_index(e);
}

void ft_encounter_menu_confirm(FtEncounter* e) {
    if(e->phase != FT_PHASE_MENU) return;

    /* Attack opens the panel rather than committing; everything else is a
     * single press as before. */
    if(e->menu_level == FT_MENU_ROOT && e->root_index == FT_ROOT_ATTACK) {
        e->menu_level = FT_MENU_ATTACK;
        sync_menu_index(e);
        return;
    }

    ft_encounter_press_ok(e);
}

bool ft_encounter_menu_back(FtEncounter* e) {
    if(e->menu_level != FT_MENU_ATTACK) return false;

    e->menu_level = FT_MENU_ROOT;
    sync_menu_index(e);
    return true;
}

/* ---- Resolution ------------------------------------------------------ */

static void enter_phase(FtEncounter* e, FtPhase phase) {
    e->phase = phase;
    e->phase_ms = 0;
}

static void gain_ram(FtEncounter* e, int16_t amount) {
    e->stats.ram = (int16_t)(e->stats.ram + amount);
    if(e->stats.ram > e->stats.ram_max) e->stats.ram = e->stats.ram_max;
    if(e->stats.ram < 0) e->stats.ram = 0;
}

/* Apply one attack to one foe, recording the per-foe result. */
uint8_t ft_encounter_foe_hit_at(const FtEncounter* e, uint8_t i) {
    /* A single-target attack lands on the strike frame. A broadcast is a
     * signal crossing the arena, so each foe is struck as it is reached:
     * leftmost first, rightmost last, spread across the strike window. */
    if(!ft_encounter_action_is_broadcast(e, (FtAction2)e->menu_index)) {
        return FT_ANIM_STRIKE;
    }
    if(e->foe_count <= 1u || i >= e->foe_count) return FT_ANIM_STRIKE;

    const uint32_t span = FT_ANIM_RECOVER - FT_ANIM_EMIT;
    const uint32_t at = FT_ANIM_EMIT + (span * i) / e->foe_count;

    return (uint8_t)at;
}

/* True while the current action has not yet reached this particular foe. */
static bool pre_strike_for(const FtEncounter* e, uint8_t i) {
    if(e->phase != FT_PHASE_RESULT && e->phase != FT_PHASE_IMPACT) return false;
    return ft_encounter_anim_progress(e) < ft_encounter_foe_hit_at(e, i);
}

int16_t ft_encounter_foe_shown_charge(const FtEncounter* e, uint8_t i) {
    if(i >= FT_MAX_ENEMIES) return 0;
    if(e->phase == FT_PHASE_RESULT && pre_strike_for(e, i)) return e->foe_charge_before[i];
    return e->foes[i].charge;
}

bool ft_encounter_foe_visible(const FtEncounter* e, uint8_t i) {
    if(i >= e->foe_count) return false;
    if(e->foes[i].charge > 0) return true;

    /* Killed by the attack currently in flight: keep it up until the signal
     * actually reaches it. */
    return e->phase == FT_PHASE_RESULT && pre_strike_for(e, i) &&
           e->foe_charge_before[i] > 0;
}

/* ---- Hit transition ---------------------------------------------------- */

/* When the iris starts, in ms from the beginning of FT_PHASE_IMPACT. */
static uint32_t iris_start_ms(void) {
    return ((uint32_t)FT_ANIM_MS * FT_ANIM_STRIKE) / 255u;
}

uint32_t ft_encounter_impact_hold(const FtEncounter* e) {
    const bool landed = (e->phase == FT_PHASE_IMPACT) && (e->last_enemy_hit.damage > 0);
    return landed ? FT_IMPACT_HOLD_HIT_MS : FT_IMPACT_HOLD_MS;
}

FtHitFx ft_encounter_hit_fx(const FtEncounter* e) {
    FtHitFx fx = {FT_HIT_FX_NONE, 0, false};

    /* Only a hit that landed. Jamming or capturing is its own reward and does
     * not deserve a two-second interruption. */
    if(e->phase != FT_PHASE_IMPACT || e->last_enemy_hit.damage <= 0) return fx;

    const uint32_t start = iris_start_ms();
    if(e->phase_ms < start) return fx;

    uint32_t t = e->phase_ms - start;

    if(t < FT_FLICKER_MS) {
        fx.stage = FT_HIT_FX_FLICKER;
        fx.strobe = ((t / 45u) % 2u) != 0u;
        return fx;
    }
    t -= FT_FLICKER_MS;

    if(t < FT_IRIS_CLOSE_MS) {
        fx.stage = FT_HIT_FX_CLOSING;
        fx.amount = (uint8_t)((t * 255u) / FT_IRIS_CLOSE_MS);
        return fx;
    }
    t -= FT_IRIS_CLOSE_MS;

    if(t < FT_IRIS_HOLD_MS) {
        fx.stage = FT_HIT_FX_BLACK;
        fx.amount = 255u;
        return fx;
    }
    t -= FT_IRIS_HOLD_MS;

    if(t < FT_IRIS_OPEN_MS) {
        fx.stage = FT_HIT_FX_OPENING;
        fx.amount = (uint8_t)(255u - (t * 255u) / FT_IRIS_OPEN_MS);
        return fx;
    }

    return fx; /* NONE: back to the fight */
}

static void strike_foe(FtEncounter* e, uint8_t i, const FtAttack* atk, FtRating rating) {
    const FtEnemy* proto = &FT_ENEMIES[e->foes[i].id];
    const FtDefender def = {proto->shielded, proto->attrs};
    const FtHitParams p = {e->fx.atk_up, 0, rating, false, FT_GUARD_NONE, 0};

    const FtHitResult r = ft_resolve_hit(atk, &def, &p);

    e->foe_hits[i] = r;
    e->foe_hit_valid[i] = true;

    if(r.ram_refund > 0) gain_ram(e, r.ram_refund);

    if(r.damage > 0) {
        e->foes[i].charge = (int16_t)(e->foes[i].charge - r.damage);
        if(e->foes[i].charge < 0) e->foes[i].charge = 0;
        e->last_total_damage = (int16_t)(e->last_total_damage + r.damage);
    }
}

static void resolve_player_action(FtEncounter* e) {
    const FtAction2 action = (FtAction2)e->menu_index;

    const FtHitResult blank = {FT_HIT_OK, 0, false, false, false, 0};
    e->last_player_hit = blank;
    e->last_total_damage = 0;
    e->last_was_replay = false;
    for(uint8_t i = 0; i < FT_MAX_ENEMIES; i++) {
        e->foe_hit_valid[i] = false;
        e->foe_charge_before[i] = e->foes[i].charge;
    }

    if(action == FT_ACTION_DEFEND) {
        e->defending = true;
        gain_ram(e, FT_DEFEND_RAM);
        return;
    }

    if(action == FT_ACTION_FOCUS) {
        ft_signal_add(&e->signal, ft_signal_focus_gain(e->fx.deep_focus_stacks));
        return;
    }

    const FtAttack* atk = NULL;
    FtAttack replay;

    if(action == FT_ACTION_SIGNAL) {
        const FtAttack* src = ft_encounter_replay_attack(e);
        if(src == NULL) return;
        if(!ft_signal_spend_bars(&e->signal, FT_SIGNAL_COST_BARS)) return;

        /* A replay is the enemy's own attack at reduced power. */
        replay = *src;
        replay.base_power = ft_siglib_replay_power(src->base_power);
        atk = &replay;
        e->last_was_replay = true;
    } else if(action == FT_ACTION_BROADCAST) {
        atk = &FT_MODULES[FT_MOD_SUBGHZ].attack;
    } else if(action == FT_ACTION_CONTACT) {
        atk = &FT_MODULES[FT_MOD_NFC].attack;
    }

    if(atk == NULL) return;

    /* A replay carries the original's timing, so it takes an action command
     * like anything else. */
    const FtRating rating =
        e->action_pressed ?
            ft_rating_from_timing((int32_t)e->action_press_ms - (FT_ACTION_WINDOW_MS / 2)) :
            FT_RATING_MISS;
    e->last_rating = rating;

    if(atk->delivery == FT_DELIVERY_BROADCAST) {
        for(uint8_t i = 0; i < e->foe_count; i++) {
            if(e->foes[i].charge > 0) strike_foe(e, i, atk, rating);
        }
        /* Headline the first foe that was actually reached. */
        for(uint8_t i = 0; i < e->foe_count; i++) {
            if(e->foe_hit_valid[i]) {
                e->last_player_hit = e->foe_hits[i];
                if(e->foe_hits[i].outcome == FT_HIT_OK) break;
            }
        }
    } else {
        const uint8_t t = ft_encounter_target(e);
        strike_foe(e, t, atk, rating);
        e->last_player_hit = e->foe_hits[t];
    }

    if(e->last_total_damage > 0) {
        ft_signal_add(&e->signal, ft_signal_attack_gain(e->roll.current, e->stats.charge_max));
    }
}

static void choose_enemy_attack(FtEncounter* e) {
    const FtEnemy* en = ft_encounter_enemy(e);
    e->foes[e->acting_foe].attack_index = (uint8_t)ft_rng_below(&e->rng, en->attack_count);
    e->guard_pressed = false;
    e->guard_press_ms = 0;
}

const FtAttack* ft_encounter_incoming(const FtEncounter* e) {
    if(e->phase != FT_PHASE_TELEGRAPH && e->phase != FT_PHASE_IMPACT) return NULL;
    return &ft_encounter_enemy(e)->attacks[e->foes[e->acting_foe].attack_index];
}

static void resolve_enemy_action(FtEncounter* e) {
    const FtAttack* atk = ft_encounter_incoming(e);
    if(atk == NULL) return;

    /* The press is recorded as a time within the sweep; convert it to a
     * distance before impact, which is what the guard windows are defined in. */
    const int32_t before_impact =
        e->guard_pressed ? (int32_t)FT_TELEGRAPH_MS - (int32_t)e->guard_press_ms : -1;

    e->last_guard = ft_guard_from_timing(before_impact, e->fx.hard_mode, atk->klass);

    /* Bracing is a real shield, so it can blunt or even deflect a hit. */
    const FtDefender def = {e->defending ? FT_DEFEND_SHIELD : 0, 0};
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
    case FT_PHASE_MENU: {
        const FtAction2 action = (FtAction2)e->menu_index;
        if(!ft_encounter_action_available(e, action)) return;

        e->action_pressed = false;
        e->action_press_ms = 0;
        e->action_locked_ms = 0;
        e->defending = false;
        e->menu_level = FT_MENU_ROOT;

        e->player_turns++;

        /* Defend and Focus have nothing to time, so they skip the sweep. */
        if(action == FT_ACTION_DEFEND || action == FT_ACTION_FOCUS) {
            resolve_player_action(e);
            enter_phase(e, FT_PHASE_RESULT);
        } else {
            enter_phase(e, FT_PHASE_PLAYER_ACT);
        }
        break;
    }

    case FT_PHASE_PLAYER_ACT:
        /* Presses during the ready beat are ignored, not penalised. Mashing
         * still costs you: the first press once the cursor moves lands at the
         * very start of the sweep, nowhere near the target. */
        if(ft_encounter_in_ready(e)) break;

        if(!e->action_pressed) {
            e->action_pressed = true;
            e->action_press_ms = ft_encounter_sweep_ms(e);
            e->action_locked_ms = 0;
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

/* Hand the turn to the next foe, or back to the player when the row is done. */
static void advance_foe_turn(FtEncounter* e, uint8_t from) {
    const int n = next_living(e, from);

    if(n < 0) {
        e->defending = false;
        enter_phase(e, FT_PHASE_MENU);
        return;
    }

    e->acting_foe = (uint8_t)n;
    choose_enemy_attack(e);
    enter_phase(e, FT_PHASE_TELEGRAPH);
}

void ft_encounter_tick(FtEncounter* e, uint32_t dt_ms) {
    if(ft_encounter_over(e)) return;

    e->phase_ms += dt_ms;

    switch(e->phase) {
    case FT_PHASE_MENU:
        /* The roll is deliberately paused while the player deliberates, so
         * thinking never costs Charge. */
        break;

    case FT_PHASE_PLAYER_ACT:
        /* A press stops the sweep dead and holds briefly, so the cursor can be
         * seen frozen exactly where it landed before anything resolves. */
        if(e->action_pressed) {
            e->action_locked_ms += dt_ms;
            if(e->action_locked_ms >= FT_LOCK_HOLD_MS) {
                resolve_player_action(e);
                enter_phase(e, FT_PHASE_RESULT);
            }
        } else if(e->phase_ms >= FT_READY_MS + FT_ACTION_WINDOW_MS) {
            resolve_player_action(e);
            enter_phase(e, FT_PHASE_RESULT);
        }
        break;

    case FT_PHASE_RESULT:
        if(e->phase_ms >= FT_IMPACT_HOLD_MS) {
            if(ft_encounter_living(e) == 0u) {
                enter_phase(e, FT_PHASE_WIN);
            } else if(e->player_turns % FT_PLAYER_TURNS_PER_ROUND != 0u) {
                /* Still the player's round: straight back to the menu. */
                e->defending = false;
                enter_phase(e, FT_PHASE_MENU);
            } else {
                advance_foe_turn(e, 0);
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
        if(e->phase_ms >= ft_encounter_impact_hold(e)) enter_phase(e, FT_PHASE_DRAIN);
        break;

    case FT_PHASE_DRAIN: {
        const uint32_t interval = ft_roll_interval_ms(0, e->defending, e->fx.hard_mode);
        ft_roll_tick(&e->roll, dt_ms, interval);

        e->stats.charge = e->roll.current;

        if(ft_roll_down(&e->roll)) {
            enter_phase(e, FT_PHASE_LOSE);
        } else if(!ft_roll_active(&e->roll)) {
            /* Each foe acts in turn before the player moves again. */
            advance_foe_turn(e, (uint8_t)(e->acting_foe + 1u));
        }
        break;
    }

    case FT_PHASE_WIN:
    case FT_PHASE_LOSE:
    default:
        break;
    }
}
