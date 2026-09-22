#include "ft_encounter.h"

/* Defined below with the rest of the turn order; needed by ft_encounter_init,
 * because a FAST foe opens the fight rather than waiting for round two. */
static void advance_foe_turn(FtEncounter* e, uint8_t from);

/* Defined with resolution; the status tick spends MP. */
static void gain_ram(FtEncounter* e, int16_t amount);

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

uint8_t ft_encounter_status(const FtEncounter* e, FtPayload p) {
    if(p >= FT_PAYLOAD_COUNT) return 0u;
    return e->status[p];
}

const char* ft_encounter_status_tag(const FtEncounter* e) {
    /* Worst first: losing turns beats losing MP beats losing a point of HP. */
    if(e->status[FT_PAYLOAD_STALL]) return "SLOW";
    if(e->status[FT_PAYLOAD_DRAIN]) return "MP-";
    if(e->status[FT_PAYLOAD_CORRUPT]) return "DOT";
    return NULL;
}

uint8_t ft_encounter_turns_this_round(const FtEncounter* e) {
    /* A stall costs you one of your two actions, which is the whole of what
     * "may lose the turn" should mean on a two-action round: predictable,
     * and expensive enough to be worth guarding against. */
    return e->status[FT_PAYLOAD_STALL] ? 1u : FT_PLAYER_TURNS_PER_ROUND;
}

/* Tick the payloads down and charge for them. Called once at the top of each
 * player round, not once per action: a per-action drip would charge twice
  * over for no reason the player could see. */
static void status_round(FtEncounter* e) {
    if(e->status[FT_PAYLOAD_CORRUPT]) {
        ft_roll_apply_damage(&e->roll, FT_CORRUPT_DAMAGE);
    }
    if(e->status[FT_PAYLOAD_DRAIN]) {
        gain_ram(e, -(int16_t)FT_DRAIN_MP);
    }

    for(uint8_t i = 0; i < FT_PAYLOAD_COUNT; i++) {
        if(e->status[i]) e->status[i]--;
    }
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

    e->acting_foe = 0;

    e->phase = FT_PHASE_MENU;
    e->phase_ms = 0;
    e->fast_phase = false;
    e->menu_index = 0;
    e->defending = false;
    e->player_turns = 0;
    for(uint8_t i = 0; i < FT_PAYLOAD_COUNT; i++) e->status[i] = 0u;

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

    /* The player always opens. FAST orders the foes *within* a round — it
     * does not buy them a free hit before the fight has started. Kicking the
     * fast phase off here instead took the prologue's last fight from 57% to
     * 32% at low skill, which is not "quick", it is "ambushed". */
}

void ft_encounter_enemy_opens(FtEncounter* e) {
    if(e->phase != FT_PHASE_MENU) return;
    if(next_living(e, 0) < 0) return;

    /* Whoever is quickest leads the ambush, same as any other round. */
    e->fast_phase = true;
    advance_foe_turn(e, 0);

    /* Nothing fast and awake to take it: fall back to the ordinary side of
     * the round rather than handing the turn back to the player. */
    if(e->phase == FT_PHASE_MENU) {
        e->fast_phase = false;
        advance_foe_turn(e, 0);
    }
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

/* Which attack an action actually throws. Needed by reach and by targeting,
 * both of which have to answer questions about a replay's delivery. */
static const FtAttack* action_attack(const FtEncounter* e, FtAction2 action) {
    switch(action) {
    case FT_ACTION_BROADCAST: return &FT_MODULES[FT_MOD_SUBGHZ].attack;
    case FT_ACTION_CONTACT:   return &FT_MODULES[FT_MOD_NFC].attack;
    case FT_ACTION_SIGNAL:    return ft_encounter_replay_attack(e);
    default:                  return NULL;
    }
}

/* The first living foe that will not let anything past it, or -1. */
static int living_bulwark(const FtEncounter* e) {
    for(uint8_t i = 0; i < e->foe_count; i++) {
        if(e->foes[i].charge <= 0) continue;
        if(FT_ENEMIES[e->foes[i].id].attrs & FT_ATTR_BULWARK) return (int)i;
    }
    return -1;
}

bool ft_encounter_can_reach(const FtEncounter* e, FtAction2 action, uint8_t i) {
    if(!ft_encounter_foe_alive(e, i)) return false;

    /* Everything behind a bulwark is safe until it is gone — a broadcast
     * included, which is the point: there is no way round it. */
    const int wall = living_bulwark(e);
    if(wall >= 0 && (uint8_t)wall != i) return false;

    const FtAttack* atk = action_attack(e, action);
    if(atk == NULL) return false;

    const uint32_t attrs = FT_ENEMIES[e->foes[i].id].attrs;

    /* Attributes decide who an attack can touch, never whether you may choose
     * it. A grounded strike cannot reach something in the air, and a
     * broadcast cannot get into something encrypted — that is a targeting
     * fact, not a locked button. */
    if(atk->delivery == FT_DELIVERY_BROADCAST) return !(attrs & FT_ATTR_ENCRYPTED);
    return !(attrs & FT_ATTR_AIRBORNE);
}

uint8_t ft_encounter_effective_target(const FtEncounter* e, FtAction2 action) {
    if(e->foe_count == 0u) return 0u;

    /* Nearest first. There is no cursor any more: a single-target attack goes
     * to the closest foe it can actually reach, and walks down the row past
     * anything it cannot. Choosing a target by hand was a whole extra control
     * for a decision that, on a row of at most three, makes itself. */
    for(uint8_t i = 0; i < e->foe_count; i++) {
        if(ft_encounter_can_reach(e, action, i)) return i;
    }

    /* Nothing reachable: the swing whiffs on the nearest living foe, which
     * the description row warns about before you press. */
    for(uint8_t i = 0; i < e->foe_count; i++) {
        if(ft_encounter_foe_alive(e, i)) return i;
    }
    return 0u;
}

/* MP the chosen action will spend. Only the strong module costs any. */
uint8_t ft_encounter_action_cost(const FtEncounter* e, FtAction2 action) {
    (void)e;
    if(action != FT_ACTION_CONTACT) return 0u;

    return ft_module_ram_cost(FT_MOD_NFC, 1u);
}

const char* ft_encounter_action_block(const FtEncounter* e, FtAction2 action) {
    /* Only what you do not *have* can stop you: a capture you never made, a
     * meter that is empty or jammed. An enemy's attributes never take a
     * module away from you — they decide who it lands on, which the caret
     * over the row already shows. */
    if(e->stats.ram < (int16_t)ft_encounter_action_cost(e, action)) {
        return "Out of MP. Guard.";
    }

    switch(action) {
    case FT_ACTION_SIGNAL:
        if(ft_encounter_replay_attack(e) == NULL) return "Capture one first.";
        if(e->signal.locked) return "Signal jammed.";
        if(ft_signal_bars(&e->signal) < FT_SIGNAL_COST_BARS) return "Need a full bar.";
        return NULL;

    case FT_ACTION_BROADCAST:
    case FT_ACTION_CONTACT:
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
/* The menu is one flat ring of every action.
 *
 * It was two levels — a bar of three with the attack modules a drill-down
 * away — which put two of the five actions behind an extra press and gave
 * the battle screen two rows of competing buttons. One row, one cursor,
 * LEFT and RIGHT. */
void ft_encounter_menu_move(FtEncounter* e, int8_t delta) {
    if(e->phase != FT_PHASE_MENU) return;

    int16_t idx = (int16_t)(e->menu_index + delta);
    while(idx < 0) idx = (int16_t)(idx + FT_ACTION_COUNT);
    while(idx >= FT_ACTION_COUNT) idx = (int16_t)(idx - FT_ACTION_COUNT);

    e->menu_index = (uint8_t)idx;
}

void ft_encounter_menu_confirm(FtEncounter* e) {
    ft_encounter_press_ok(e);
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

int16_t ft_encounter_xp(const FtEncounter* e) {
    if(e->phase != FT_PHASE_WIN) return 0;

    /* Each foe is tapered against the player's level separately, so a mixed
     * group pays properly rather than being averaged. */
    int16_t total = 0;
    for(uint8_t i = 0; i < e->foe_count; i++) {
        const FtEnemy* proto = &FT_ENEMIES[e->foes[i].id];
        total = (int16_t)(total + ft_xp_award(proto->level, e->stats.level, proto->xp));
    }
    return total;
}

uint8_t ft_encounter_foe_defeat(const FtEncounter* e, uint8_t i) {
    if(i >= e->foe_count) return 0u;
    if(e->foes[i].charge > 0) return 0u;
    if(e->phase != FT_PHASE_RESULT) return 0u;

    /* Only the foe this attack just killed: one that was already down when
     * the turn started has finished falling. */
    if(e->foe_charge_before[i] <= 0) return 0u;

    const uint8_t at = ft_encounter_foe_hit_at(e, i);
    const uint8_t now = ft_encounter_anim_progress(e);
    if(now <= at) return 0u;

    /* 0 at the moment of the hit, 255 once it is gone. Dying takes the rest
     * of the animation however early in it the hit landed, so a foe struck
     * first by a sweeping broadcast falls slower than the last one — which
     * is right: they all finish together, as the turn ends. */
    const uint32_t span = (uint32_t)(FT_ANIM_RECOVER - at);
    if(span == 0u) return 255u;

    const uint32_t done = ((uint32_t)(now - at) * 255u) / span;
    return (done > 255u) ? 255u : (uint8_t)done;
}

bool ft_encounter_foe_visible(const FtEncounter* e, uint8_t i) {
    if(i >= e->foe_count) return false;
    if(e->foes[i].charge > 0) return true;

    /* Killed by the attack currently in flight: keep it up until the signal
     * actually reaches it, and then for as long as it takes to fall over. */
    return e->phase == FT_PHASE_RESULT && e->foe_charge_before[i] > 0 &&
           (pre_strike_for(e, i) || ft_encounter_foe_defeat(e, i) < 255u);
}

/* ---- Hit transition ---------------------------------------------------- */

/* When the iris starts, in ms from the beginning of FT_PHASE_IMPACT. */
static uint32_t iris_start_ms(void) {
    return ((uint32_t)FT_ANIM_MS * FT_ANIM_STRIKE) / 255u;
}

uint32_t ft_encounter_impact_hold(const FtEncounter* e) {
    (void)e;
    return FT_IMPACT_HOLD_MS;
}

FtWipe ft_wipe_at(uint32_t ms) {
    FtWipe w = {FT_WIPE_NONE, 0};

    if(ms < FT_WIPE_CLOSE_MS) {
        w.stage = FT_WIPE_CLOSING;
        w.amount = (uint8_t)((ms * 255u) / FT_WIPE_CLOSE_MS);
        return w;
    }

    ms -= FT_WIPE_CLOSE_MS;
    if(ms < FT_WIPE_OPEN_MS) {
        w.stage = FT_WIPE_OPENING;
        w.amount = (uint8_t)(255u - (ms * 255u) / FT_WIPE_OPEN_MS);
        return w;
    }

    return w; /* NONE: the new scene is fully visible */
}

FtHitFx ft_encounter_hit_fx(const FtEncounter* e) {
    FtHitFx fx = {FT_HIT_FX_NONE, 0, false};

    /* Only a hit that landed. Jamming or capturing is its own reward. */
    if(e->phase != FT_PHASE_IMPACT || e->last_enemy_hit.damage <= 0) return fx;

    const uint32_t start = iris_start_ms();
    if(e->phase_ms < start) return fx;

    /* A flinch, and nothing more. This used to close a full-screen iris,
     * hold black for half a second and open it again, every single time a
     * foe connected — which in a three-foe round is three interruptions in
     * one turn. The strobe says "that hurt" in a fifth of the time and never
     * takes the fight off the screen. */
    const uint32_t t = e->phase_ms - start;
    if(t >= FT_FLICKER_MS) return fx;

    fx.stage = FT_HIT_FX_FLICKER;
    fx.strobe = ((t / 45u) % 2u) != 0u;
    return fx;
}

static void strike_foe(FtEncounter* e, uint8_t i, const FtAttack* atk, FtRating rating) {
    /* Nothing gets past a bulwark, a broadcast least of all.
     *
     * The broadcast path struck every living foe directly and never asked
     * about reach — ft_resolve_hit only knows about AIRBORNE and ENCRYPTED —
     * so the wall blocked single-target attacks and was transparent to the
     * one attack that hits everything. It was a 100% roster. */
    const int wall = living_bulwark(e);
    if(wall >= 0 && (uint8_t)wall != i) {
        e->foe_hits[i] = (FtHitResult){FT_HIT_LOCKED, 0, false, false, false, 0};
        e->foe_hit_valid[i] = true;
        return;
    }

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
        ft_roll_heal(&e->roll, FT_DEFEND_HEAL, e->stats.charge_max);
        e->stats.charge = e->roll.current;
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
        gain_ram(e, -(int16_t)ft_encounter_action_cost(e, action));
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
        const uint8_t t = ft_encounter_effective_target(e, action);
        strike_foe(e, t, atk, rating);
        e->last_player_hit = e->foe_hits[t];
    }

    if(e->last_total_damage > 0) {
        ft_signal_add(&e->signal, ft_signal_attack_gain(e->roll.current, e->stats.charge_max));
    }
}

static void choose_enemy_attack(FtEncounter* e) {
    const FtEnemy* en = ft_encounter_enemy(e);

    /* A sleeper that has woken up leads with its last attack, which is the
     * big one: the whole shape of the fight is "it was quiet, and then it
     * was not". */
    if((en->attrs & FT_ATTR_SLEEPER) && en->attack_count > 1u) {
        e->foes[e->acting_foe].attack_index = (uint8_t)(en->attack_count - 1u);
        e->guard_pressed = false;
        e->guard_press_ms = 0;
        return;
    }

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

    /* A jam or a capture already nullifies it — ft_resolve_hit works that
     * out — so this only fires on a hit that got through clean. */
    if(e->last_enemy_hit.payload_applied && atk->payload < FT_PAYLOAD_COUNT) {
        e->status[atk->payload] = FT_STATUS_TURNS;
    }

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
static bool foe_is_fast(const FtEncounter* e, uint8_t i) {
    return ft_priority_for_enemy(FT_ENEMIES[e->foes[i].id].attrs) == FT_PRIO_FAST_ENEMY;
}

/* Is this foe taking turns at all right now? */
static bool foe_acts(const FtEncounter* e, uint8_t i) {
    const uint32_t attrs = FT_ENEMIES[e->foes[i].id].attrs;

    /* A bulwark's whole job is standing there. */
    if(attrs & FT_ATTR_BULWARK) return false;

    /* A sleeper waits until it is the only thing left, and then it is the
     * hardest thing you have met. */
    if(attrs & FT_ATTR_SLEEPER) return ft_encounter_living(e) <= 1u;

    return true;
}

bool ft_encounter_foe_awake(const FtEncounter* e, uint8_t i) {
    if(!ft_encounter_foe_alive(e, i)) return false;
    return foe_acts(e, i);
}

/* Next living foe at or after `from` on the side of the round we are in.
 *
 * FAST used to be a published attribute that nothing read: ft_priority.c
 * computed FT_PRIO_FAST_ENEMY and the encounter never asked. The Sealed
 * Lock has carried the tag since the prologue was written and it never once
 * acted early. */
static int next_actor(const FtEncounter* e, uint8_t from) {
    for(uint8_t i = from; i < e->foe_count; i++) {
        if(e->foes[i].charge <= 0) continue;
        if(!foe_acts(e, i)) continue;
        if(foe_is_fast(e, i) == e->fast_phase) return (int)i;
    }
    return -1;
}

/* Kept so the round can tell "nothing is alive" from "nothing on this side of
 * the round is alive" — the two have very different consequences. */
static int any_living(const FtEncounter* e) {
    return next_living(e, 0);
}

static void advance_foe_turn(FtEncounter* e, uint8_t from) {
    const int n = next_actor(e, from);

    if(n >= 0) {
        e->acting_foe = (uint8_t)n;
        choose_enemy_attack(e);
        enter_phase(e, FT_PHASE_TELEGRAPH);
        return;
    }

    e->defending = false;

    if(e->fast_phase) {
        /* The quick ones have had their say; now the player moves — and
         * whatever is eating them takes its bite first. */
        e->fast_phase = false;
        e->player_turns = 0;
        status_round(e);
        enter_phase(e, FT_PHASE_MENU);
        return;
    }

    if(any_living(e) < 0) {
        enter_phase(e, FT_PHASE_MENU); /* the tick will call it a win */
        return;
    }

    /* Round over. The next one opens with whatever is FAST, or with the
     * player if nothing is. */
    e->fast_phase = true;
    advance_foe_turn(e, 0);
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
            } else if(e->player_turns % ft_encounter_turns_this_round(e) != 0u) {
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
