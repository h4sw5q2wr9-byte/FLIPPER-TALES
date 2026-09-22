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
    uint8_t     ram_cost;
    bool        hits_all;

    FtAttack attack;
} FtModule;

/* The two attacks you have. There used to be six more — equippable passive
 * Cards with a slot budget — and no way in the game to obtain any of them.
 * See FT_START_POWER. */
typedef enum {
    FT_MOD_SUBGHZ = 0,
    FT_MOD_NFC,
    FT_MODULE_COUNT
} FtModuleId;

extern const FtModule FT_MODULES[FT_MODULE_COUNT];

/* MP this module costs to use. */
uint8_t ft_module_ram_cost(FtModuleId id);


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

    /* Two that change the shape of a fight rather than its numbers. */
    FT_ENEMY_BLANK_WALL,       /* BULWARK  — never attacks, nothing gets past */
    FT_ENEMY_COLD_BOOTER,      /* SLEEPER  — quiet until it is the last one */

    FT_ENEMY_COUNT
} FtEnemyId;

extern const FtEnemy FT_ENEMIES[FT_ENEMY_COUNT];

/* Find an attack by its stable id, across every enemy. Used to replay a
 * captured signal, which stores only the id. NULL if no such attack. */
const FtAttack* ft_attack_by_id(uint16_t id);

#endif /* FT_DATA_H */
