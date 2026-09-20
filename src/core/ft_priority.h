/* Turn order. See DESIGN.md 4.6.
 *
 * Deliberately a published table rather than a hidden speed stat: the player
 * can reason about ordering, and it costs no RAM and no RNG. */
#ifndef FT_PRIORITY_H
#define FT_PRIORITY_H

#include "ft_types.h"

typedef enum {
    FT_PRIO_SPECIAL = 0,   /* replayed signals and module ultimates */
    FT_PRIO_HEAL,
    FT_PRIO_STATUS,
    FT_PRIO_FAST_ENEMY,    /* the FAST attribute */
    FT_PRIO_DEBUFF,
    FT_PRIO_PLAYER_ATTACK,
    FT_PRIO_ENEMY,
    FT_PRIO_FOCUS,
    FT_PRIO_REVIVE,
    FT_PRIO_COUNT
} FtPriority;

typedef struct {
    uint8_t    actor;    /* 0 is the player, 1..n are enemies */
    FtPriority priority;
    uint8_t    slot;     /* board position, breaks ties */
} FtAction;

/* Sort in place into resolution order. Stable, so equal priorities keep their
 * slot ordering. n is at most FT_MAX_ACTORS, so insertion sort is right. */
void ft_priority_sort(FtAction* actions, uint8_t n);

/* Priority for a standard enemy action, accounting for the FAST attribute. */
FtPriority ft_priority_for_enemy(uint32_t attrs);

#endif /* FT_PRIORITY_H */
