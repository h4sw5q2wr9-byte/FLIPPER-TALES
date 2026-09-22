/* Levelling and XP. See DESIGN.md 4.1. */
#ifndef FT_PROGRESS_H
#define FT_PROGRESS_H

#include "ft_types.h"

typedef enum {
    FT_UP_CHARGE = 0,
    FT_UP_RAM,
    FT_UP_POWER,
    FT_UP_COUNT
} FtLevelChoice;

typedef struct {
    int16_t charge, charge_max;
    int16_t ram, ram_max;
    int16_t power; /* flat damage added to every attack */
    int16_t level;
    int16_t xp;

    /* Orbs: the part of the build that is not final.
     *
     * `orbs` is what is in hand; `spent` is how many went into each stat, and
     * is what makes taking one back possible at all — without it there is no
     * way to tell a levelled stat from a starting one. Every max above is
     * therefore derived, never accumulated. */
    int16_t orbs;
    uint8_t spent[FT_UP_COUNT];
} FtStats;

void ft_stats_init(FtStats* s);

/* Highest level reachable having finished this many chapters. */
int16_t ft_level_cap(int16_t chapters_completed);

/* Can this stat still be raised? Caps make some choices unavailable. */
bool ft_level_choice_available(const FtStats* s, FtLevelChoice choice);

/* Take one owed level: raises `level`, pays out FT_ORBS_PER_LEVEL orbs, and
 * restores HP and MP in full as levelling always did. Nothing here picks a
 * stat — that is the orb's job, and it can be changed later. */
void ft_level_take(FtStats* s);

/* What one orb is worth in this stat. */
int16_t ft_orb_step(FtLevelChoice choice);

/* Put an orb into a stat. False — and nothing changes — with no orbs in hand
 * or the stat already capped. The gain is granted immediately, so raising HP
 * mid-run actually heals you by that much. */
bool ft_orb_spend(FtStats* s, FtLevelChoice choice);

/* Can this orb be taken back out? False only when none went in here. */
bool ft_orb_can_refund(const FtStats* s, FtLevelChoice choice);

/* Take an orb back out. Current HP and MP are clamped to the new maximum. */
bool ft_orb_refund(FtStats* s, FtLevelChoice choice);

/* Bank XP and report how many level-ups are now owed. XP is capped per battle
 * before it gets here. */
int16_t ft_xp_gain(FtStats* s, int16_t xp, int16_t level_cap);

/* XP actually awarded for an enemy, after the underlevelling penalty.
 *
 * Our rule (Block Tales states the behaviour but not a formula): full value
 * while the enemy is at or above the player's level, tapering by a third per
 * level below, and nothing once it is 3 or more levels under. This is what
 * stops low-level farming. */
int16_t ft_xp_award(int16_t enemy_level, int16_t player_level, int16_t base_xp);

#endif /* FT_PROGRESS_H */
