/* Levelling and XP. See DESIGN.md 4.1. */
#ifndef FT_PROGRESS_H
#define FT_PROGRESS_H

#include "ft_types.h"

typedef struct {
    int16_t charge, charge_max;
    int16_t ram, ram_max;
    int16_t flash_used, flash_max;
    int16_t level;
    int16_t xp;
} FtStats;

typedef enum {
    FT_UP_CHARGE = 0,
    FT_UP_RAM,
    FT_UP_FLASH
} FtLevelChoice;

void ft_stats_init(FtStats* s);

/* Highest level reachable having finished this many chapters. */
int16_t ft_level_cap(int16_t chapters_completed);

/* Can this stat still be raised? Caps make some choices unavailable. */
bool ft_level_choice_available(const FtStats* s, FtLevelChoice choice);

/* Spend one owed level-up on this stat. Raises `level`, and restores Charge
 * and RAM in full as levelling always does. Returns false if that stat is
 * already capped, in which case nothing changes and the level is not spent. */
bool ft_level_apply(FtStats* s, FtLevelChoice choice);

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

/* Flash budget: can a module costing this much still be installed? */
bool ft_flash_can_install(const FtStats* s, int16_t cost);
void ft_flash_install(FtStats* s, int16_t cost);
void ft_flash_uninstall(FtStats* s, int16_t cost);

#endif /* FT_PROGRESS_H */
