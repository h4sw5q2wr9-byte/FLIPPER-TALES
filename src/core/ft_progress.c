#include "ft_progress.h"

void ft_stats_init(FtStats* s) {
    s->charge = s->charge_max = FT_START_CHARGE;
    s->ram = s->ram_max = FT_START_RAM;
    s->flash_used = 0;
    s->flash_max = FT_START_FLASH;
    s->level = 1;
    s->xp = 0;
}

int16_t ft_level_cap(int16_t chapters_completed) {
    if(chapters_completed < 0) chapters_completed = 0;
    return (int16_t)(FT_LEVEL_CAP_BASE + chapters_completed * FT_LEVEL_CAP_PER_CHAPTER);
}

bool ft_level_choice_available(const FtStats* s, FtLevelChoice choice) {
    switch(choice) {
    case FT_UP_CHARGE: return s->charge_max < FT_CAP_CHARGE;
    case FT_UP_RAM:    return s->ram_max < FT_CAP_RAM;
    case FT_UP_FLASH:  return s->flash_max < FT_CAP_FLASH;
    default:           return false;
    }
}

bool ft_level_apply(FtStats* s, FtLevelChoice choice) {
    if(!ft_level_choice_available(s, choice)) return false;

    switch(choice) {
    case FT_UP_CHARGE:
        s->charge_max += FT_LEVEL_UP_CHARGE;
        if(s->charge_max > FT_CAP_CHARGE) s->charge_max = FT_CAP_CHARGE;
        break;
    case FT_UP_RAM:
        s->ram_max += FT_LEVEL_UP_RAM;
        if(s->ram_max > FT_CAP_RAM) s->ram_max = FT_CAP_RAM;
        break;
    case FT_UP_FLASH:
        s->flash_max += FT_LEVEL_UP_FLASH;
        if(s->flash_max > FT_CAP_FLASH) s->flash_max = FT_CAP_FLASH;
        break;
    default:
        return false;
    }

    /* Applying the choice is what actually levels you: ft_xp_gain reports how
     * many are *owed*, and they are spent one at a time as the player picks.
     * Without this the level never moved, so the level cap never bit and
     * every enemy stayed worth full XP forever. */
    s->level++;

    /* A level-up is also a full restore. */
    s->charge = s->charge_max;
    s->ram = s->ram_max;

    return true;
}

int16_t ft_xp_gain(FtStats* s, int16_t xp, int16_t level_cap) {
    if(xp < 0) xp = 0;
    if(xp > FT_XP_BATTLE_CAP) xp = FT_XP_BATTLE_CAP;

    if(s->level >= level_cap) return 0; /* capped players earn nothing */

    s->xp = (int16_t)(s->xp + xp);

    int16_t levels = 0;
    while(s->xp >= FT_XP_PER_LEVEL && (s->level + levels) < level_cap) {
        s->xp = (int16_t)(s->xp - FT_XP_PER_LEVEL);
        levels++;
    }

    /* At the cap, surplus XP is discarded rather than banked. */
    if((s->level + levels) >= level_cap) s->xp = 0;

    return levels;
}

int16_t ft_xp_award(int16_t enemy_level, int16_t player_level, int16_t base_xp) {
    if(base_xp <= 0) return 0;

    const int16_t deficit = (int16_t)(player_level - enemy_level);
    if(deficit <= 0) return base_xp;
    if(deficit >= 3) return 0;

    /* One third off per level under, rounded down, but never to zero while
     * the enemy is still worth fighting. */
    int32_t award = ((int32_t)base_xp * (3 - deficit)) / 3;
    if(award < 1) award = 1;

    return (int16_t)award;
}

bool ft_flash_can_install(const FtStats* s, int16_t cost) {
    if(cost < 0) return false;
    return (s->flash_used + cost) <= s->flash_max;
}

void ft_flash_install(FtStats* s, int16_t cost) {
    if(cost > 0) s->flash_used += cost;
}

void ft_flash_uninstall(FtStats* s, int16_t cost) {
    if(cost > 0) s->flash_used -= cost;
    if(s->flash_used < 0) s->flash_used = 0;
}
