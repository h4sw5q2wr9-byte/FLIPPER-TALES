#include "ft_priority.h"

void ft_priority_sort(FtAction* actions, uint8_t n) {
    for(uint8_t i = 1; i < n; i++) {
        const FtAction key = actions[i];
        int16_t j = (int16_t)i - 1;

        /* Strict > keeps the sort stable: equal keys never swap. */
        while(j >= 0 && actions[j].priority > key.priority) {
            actions[j + 1] = actions[j];
            j--;
        }
        actions[j + 1] = key;
    }
}

FtPriority ft_priority_for_enemy(uint32_t attrs) {
    return (attrs & FT_ATTR_FAST) ? FT_PRIO_FAST_ENEMY : FT_PRIO_ENEMY;
}
