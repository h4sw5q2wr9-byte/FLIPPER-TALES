#include "ft_practice.h"

/* The fixed line-ups, chosen to cover the reasons a fight can be hard rather
 * than to be a list of every combination: one of each attribute, a crowd, and
 * the boards where a single module cannot do the job alone. */
typedef struct {
    const char* name;
    uint8_t     count;
    FtEnemyId   foes[FT_MAX_ENEMIES];
} PracticeGroup;

static const PracticeGroup GROUPS[FT_PRACTICE_GROUPS - 1] = {
    {"Packet",     1, {FT_ENEMY_STRAY_PACKET, 0, 0}},
    {"Beacon",     1, {FT_ENEMY_DRIFT_BEACON, 0, 0}},
    {"Lock",       1, {FT_ENEMY_SEALED_LOCK, 0, 0}},
    {"2 Packets",  2, {FT_ENEMY_STRAY_PACKET, FT_ENEMY_STRAY_PACKET, 0}},
    {"Air pair",   2, {FT_ENEMY_DRIFT_BEACON, FT_ENEMY_DRIFT_BEACON, 0}},
    {"Mixed pair", 2, {FT_ENEMY_DRIFT_BEACON, FT_ENEMY_SEALED_LOCK, 0}},
    {"Full trio",  3, {FT_ENEMY_STRAY_PACKET, FT_ENEMY_DRIFT_BEACON,
                       FT_ENEMY_SEALED_LOCK}},
};

static const char* const KIT_NAMES[FT_KIT_COUNT] = {"Basic", "Loaded", "Max"};

void ft_practice_init(FtPractice* p, uint32_t seed) {
    p->row = FT_PRACTICE_FOES;
    p->group = FT_FOES_RANDOM;
    p->level = 1;
    p->kit = FT_KIT_BASIC;
    p->seed = seed ? seed : 0x5EEDu;
}

void ft_practice_move(FtPractice* p, int8_t delta) {
    const int16_t n = (int16_t)(p->row + delta);
    p->row = (uint8_t)((n < 0) ? (FT_PRACTICE_ROWS - 1) : (n % FT_PRACTICE_ROWS));
}

/* Wrap v by delta within [0, n). */
static uint8_t wrap(uint8_t v, int8_t delta, uint8_t n) {
    const int16_t x = (int16_t)(v + delta);
    if(x < 0) return (uint8_t)(n - 1u);
    return (uint8_t)(x % n);
}

void ft_practice_adjust(FtPractice* p, int8_t delta) {
    switch((FtPracticeRow)p->row) {
    case FT_PRACTICE_FOES:
        p->group = wrap(p->group, delta, FT_PRACTICE_GROUPS);
        break;
    case FT_PRACTICE_LEVEL:
        p->level = (uint8_t)(wrap((uint8_t)(p->level - 1u), delta,
                                  FT_PRACTICE_MAX_LEVEL) + 1u);
        break;
    case FT_PRACTICE_KIT:
        p->kit = wrap(p->kit, delta, FT_KIT_COUNT);
        break;
    case FT_PRACTICE_FIGHT:
    default:
        break; /* nothing to cycle: this row is the button */
    }
}

const char* ft_practice_row_name(uint8_t row) {
    switch((FtPracticeRow)row) {
    case FT_PRACTICE_FOES:  return "Foes";
    case FT_PRACTICE_LEVEL: return "Level";
    case FT_PRACTICE_KIT:   return "Kit";
    case FT_PRACTICE_FIGHT: return "FIGHT";
    default:                return "";
    }
}

/* Small unsigned to text, so the setup screen needs no snprintf. */
static const char* small_num(uint8_t v) {
    static const char* const N[FT_PRACTICE_MAX_LEVEL + 1] = {
        "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10"};
    return (v <= FT_PRACTICE_MAX_LEVEL) ? N[v] : "?";
}

const char* ft_practice_value(const FtPractice* p, uint8_t row) {
    switch((FtPracticeRow)row) {
    case FT_PRACTICE_FOES:
        return (p->group == FT_FOES_RANDOM) ? "Random" : GROUPS[p->group - 1u].name;
    case FT_PRACTICE_LEVEL:
        return small_num(p->level);
    case FT_PRACTICE_KIT:
        return KIT_NAMES[p->kit < FT_KIT_COUNT ? p->kit : 0];
    case FT_PRACTICE_FIGHT:
    default:
        return "";
    }
}

const char* ft_practice_help(const FtPractice* p) {
    switch((FtPracticeRow)p->row) {
    case FT_PRACTICE_FOES:
        return (p->group == FT_FOES_RANDOM) ? "A new group each go" :
                                              "LEFT/RIGHT to swap";
    case FT_PRACTICE_LEVEL:
        return "More Charge and RAM";
    case FT_PRACTICE_KIT:
        switch((FtKit)p->kit) {
        case FT_KIT_LOADED: return "Every card, one each";
        case FT_KIT_MAX:    return "Every card, stacked";
        case FT_KIT_BASIC:
        default:            return "What you start with";
        }
    case FT_PRACTICE_FIGHT:
    default:
        return "OK to start";
    }
}

void ft_practice_loadout(uint8_t kit, FtLoadout* lo) {
    ft_loadout_init(lo);
    if(kit == FT_KIT_BASIC) return;

    /* Hard Mode is a card like any other, but switching it on by surprise
     * would make "more abilities" mean "twice the damage taken". It stays off
     * unless the player installs it deliberately. */
    for(uint8_t id = 0; id < FT_MODULE_COUNT; id++) {
        if(id == FT_MOD_HARD_MODE) continue;

        ft_loadout_add(lo, (FtModuleId)id);
        if(kit != FT_KIT_MAX) continue;

        /* Stack until the module refuses, which is its own cap. */
        for(uint8_t n = 0; n < 8u; n++) {
            if(!ft_loadout_add(lo, (FtModuleId)id)) break;
        }
    }
}

/* Roll a group: one to three foes, drawn freely from the roster. */
static uint8_t roll_group(FtRng* rng, FtEnemyId* out) {
    const uint8_t count = (uint8_t)(1u + ft_rng_below(rng, FT_MAX_ENEMIES));

    for(uint8_t i = 0; i < count; i++) {
        out[i] = (FtEnemyId)ft_rng_below(rng, FT_ENEMY_COUNT);
    }
    return count;
}

void ft_practice_start(FtPractice* p, FtEncounter* e) {
    FtRng rng;
    ft_rng_seed(&rng, p->seed);

    FtEnemyId foes[FT_MAX_ENEMIES];
    uint8_t count;

    if(p->group == FT_FOES_RANDOM) {
        count = roll_group(&rng, foes);
    } else {
        const PracticeGroup* g = &GROUPS[p->group - 1u];
        count = g->count;
        for(uint8_t i = 0; i < count; i++) foes[i] = g->foes[i];
    }

    /* Move the seed on, so pressing FIGHT again is a different fight rather
     * than the same one again. */
    p->seed = ft_rng_next(&rng);

    FtLoadout lo;
    ft_practice_loadout(p->kit, &lo);

    ft_encounter_init(e, foes, count, &lo, p->seed);

    /* Levels, taken and spent as the game takes and spends them: each level
     * pays an orb, and the arena places them evenly rather than making you
     * pick ten times. */
    static const FtLevelChoice CYCLE[FT_UP_COUNT] = {
        FT_UP_CHARGE, FT_UP_RAM, FT_UP_FLASH};
    for(uint8_t l = 1; l < p->level; l++) {
        ft_level_take(&e->stats);
        ft_orb_spend(&e->stats, CYCLE[(l - 1u) % FT_UP_COUNT]);
    }
    e->stats.charge_max = (int16_t)(e->stats.charge_max + e->fx.charge_max_bonus);
    e->stats.charge = e->stats.charge_max;
    ft_roll_init(&e->roll, e->stats.charge);

    /* A kit you cannot use is not a kit. Give the loaded sets a meter that is
     * already worth spending, or Deflect is a button that does nothing for
     * the first four turns. */
    if(p->kit != FT_KIT_BASIC) {
        ft_signal_add(&e->signal, (int16_t)(FT_SIGNAL_PER_BAR * e->signal.max_bars));
    }

    /* The arena is for trying things, so it never nags. */
    e->coach = false;
}
