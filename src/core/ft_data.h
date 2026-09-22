/* Static content tables for Milestone 1. See DESIGN.md 7.
 *
 * These live in flash as const data, never RAM. */
#ifndef FT_DATA_H
#define FT_DATA_H

#include "ft_combat.h"
#include "ft_types.h"

typedef enum {
    FT_SLOT_BROADCAST = 0,
    FT_SLOT_CONTACT,
    FT_SLOT_PASSIVE
} FtSlot;

typedef struct {
    const char* name;
    FtSlot      slot;
    uint8_t     flash_cost;
    uint8_t     ram_cost;
    uint8_t     max_stacks;
    bool        hits_all;

    FtAttack attack; /* meaningful for attack slots only */

    /* Passive contributions, all applied per stack. */
    int16_t charge_max_bonus;
    int16_t atk_up;
    uint8_t jam_bonus_pct;
    uint8_t deep_focus;
    bool    hard_mode;
} FtModule;

typedef enum {
    FT_MOD_SUBGHZ = 0,
    FT_MOD_AMPLIFY,
    FT_MOD_NFC,
    FT_MOD_PAYLOAD,
    FT_MOD_CHARGE_PLUS,
    FT_MOD_FARADAY,
    FT_MOD_DEEP_FOCUS,
    FT_MOD_HARD_MODE,
    FT_MODULE_COUNT
} FtModuleId;

extern const FtModule FT_MODULES[FT_MODULE_COUNT];

/* RAM cost of using a module at a given stack count: stacking raises both the
 * effect and the cost (DESIGN.md 3). */
uint8_t ft_module_ram_cost(FtModuleId id, uint8_t stacks);

/* ---- Loadout --------------------------------------------------------- */

typedef struct {
    uint8_t stacks[FT_MODULE_COUNT];
} FtLoadout;

/* Everything the battle code needs to know about what is installed. */
typedef struct {
    int16_t flash_used;
    int16_t charge_max_bonus;
    int16_t atk_up;
    uint8_t jam_reduction_pct;
    uint8_t deep_focus_stacks;
    bool    hard_mode;
} FtLoadoutEffects;

void ft_loadout_init(FtLoadout* lo);

/* Install one more copy. Returns false if the module is at max stacks. */
bool ft_loadout_add(FtLoadout* lo, FtModuleId id);

FtLoadoutEffects ft_loadout_effects(const FtLoadout* lo);

/* ---- Enemies --------------------------------------------------------- */

#define FT_ENEMY_MAX_ATTACKS 3

typedef struct {
    const char* name;
    int16_t     charge;
    int16_t     shielded;
    uint32_t    attrs;
    int16_t     level;
    int16_t     xp;
    uint8_t     attack_count;
    FtAttack    attacks[FT_ENEMY_MAX_ATTACKS];
} FtEnemy;

typedef enum {
    /* The prologue: one enemy per lesson. */
    FT_ENEMY_STRAY_PACKET = 0, /* plain      — both modules work */
    FT_ENEMY_DRIFT_BEACON,     /* AIRBORNE   — contact cannot reach */
    FT_ENEMY_SEALED_LOCK,      /* ENCRYPTED  — broadcast does nothing */

    /* One per area, each taking a mechanic the prologue taught and turning
     * it up. An area that looks like five places and fights like one is not
     * five places.
     *
     * No enemy is ever both AIRBORNE and ENCRYPTED: that combination is
     * immune to both modules at once, which is not difficulty, it is an
     * unwinnable fight. ft_data.c's tests assert it. */
    FT_ENEMY_SCRAP_CRAWLER,    /* FAST            — hits first, hits hard */
    FT_ENEMY_RIME_SHELL,       /* ENCRYPTED, SH3  — a wall; pierce it */
    FT_ENEMY_GATE_DRONE,       /* AIRBORNE, FAST  — out of reach and quick */
    FT_ENEMY_MAST_RELAY,       /* JAMMER          — no replays in this fight */
    FT_ENEMY_NULL_FIELD,       /* ENCRYPTED+JAM   — and its hits resist capture */

    FT_ENEMY_COUNT
} FtEnemyId;

extern const FtEnemy FT_ENEMIES[FT_ENEMY_COUNT];

/* Find an attack by its stable id, across every enemy. Used to replay a
 * captured signal, which stores only the id. NULL if no such attack. */
const FtAttack* ft_attack_by_id(uint16_t id);

#endif /* FT_DATA_H */
