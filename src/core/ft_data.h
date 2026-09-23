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

    /* Ma Rivet's clicker. Line of sight: the nearest foe, whatever it is —
     * flying or sealed, Infrared reaches it — and hard, for more MP. */
    FT_MOD_INFRARED,

    /* Ledger's reader. Up close, and it reads straight through a shield:
     * the answer to a Chiller. */
    FT_MOD_RFID,
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
    /* Every enemy is one of the Carrier's own maintenance machines, still
     * doing the job it was built for — for Hush, who told them all that
     * everybody stays home. Its job is why it fights the way it does; see
     * STORY.md §3 and the table in §4b.
     *
     * The prologue: one enemy per lesson. */
    FT_ENEMY_PARCEL_RUNNER = 0, /* carried the mail       — plain */
    FT_ENEMY_LAMPLIGHTER,       /* lit the night roads    — AIRBORNE */
    FT_ENEMY_CURFEW_LOCK,       /* locked up at night     — ENCRYPTED */

    /* One per area, each taking a mechanic the prologue taught and turning
     * it up.
     *
     * No enemy is ever both AIRBORNE and ENCRYPTED: that combination is
     * immune to both modules at once, which is not difficulty, it is an
     * unwinnable fight. ft_data.c's tests assert it. */
    FT_ENEMY_SWEEPER,       /* cleared the spans      — FAST */
    FT_ENEMY_CHILLER,       /* kept the vaults cold   — ENCRYPTED, SH3 */
    FT_ENEMY_TICKET_DRONE,  /* checked tickets        — AIRBORNE, FAST */
    FT_ENEMY_LOUDHAILER,    /* made announcements     — JAMMER */
    FT_ENEMY_SHUSHER,       /* Hush's own quiet-maker — ENCRYPTED, JAMMER */

    /* Two that change the shape of a fight rather than its numbers. */
    FT_ENEMY_QUEUE_BARRIER, /* kept the queue orderly — BULWARK */
    FT_ENEMY_NIGHT_SHIFT,   /* works when nobody else is — SLEEPER */

    /* The Courier before you, and a boss rather than a machine (STORY.md
     * §4). It fights like you — one attack that reaches everyone, one that
     * hits hard up close — and the first time, it gets away. */
    FT_ENEMY_ECHO,          /* BOSS, RETREATS */

    FT_ENEMY_COUNT
} FtEnemyId;

extern const FtEnemy FT_ENEMIES[FT_ENEMY_COUNT];

/* Find an attack by its stable id, across every enemy. Used to replay a
 * captured signal, which stores only the id. NULL if no such attack. */
const FtAttack* ft_attack_by_id(uint16_t id);

/* What an attack is called, for the wind-up and the field guide. Every enemy
 * attack has one; the player's modules are named by their module. Never NULL. */
const char* ft_attack_name(uint16_t id);

/* Longest attack name, so the wind-up title and the guide can budget for it. */
#define FT_ATTACK_NAME_MAX 12

#endif /* FT_DATA_H */
