#include "ft_progress.h"

void ft_stats_init(FtStats* s) {
    s->charge = s->charge_max = FT_START_CHARGE;
    s->ram = s->ram_max = FT_START_RAM;
    s->power = FT_START_POWER;
    s->level = 1;
    s->xp = 0;
    s->orbs = 0;
    for(uint8_t i = 0; i < FT_UP_COUNT; i++) s->spent[i] = 0u;
}

int16_t ft_level_cap(int16_t chapters_completed) {
    if(chapters_completed < 0) chapters_completed = 0;
    return (int16_t)(FT_LEVEL_CAP_BASE + chapters_completed * FT_LEVEL_CAP_PER_CHAPTER);
}

bool ft_level_choice_available(const FtStats* s, FtLevelChoice choice) {
    switch(choice) {
    case FT_UP_CHARGE: return s->charge_max < FT_CAP_CHARGE;
    case FT_UP_RAM:    return s->ram_max < FT_CAP_RAM;
    case FT_UP_POWER:  return s->power < FT_CAP_POWER;
    default:           return false;
    }
}

void ft_level_take(FtStats* s) {
    s->level++;
    s->orbs = (int16_t)(s->orbs + FT_ORBS_PER_LEVEL);

    /* A level-up is also a full restore. */
    s->charge = s->charge_max;
    s->ram = s->ram_max;
}

int16_t ft_orb_step(FtLevelChoice choice) {
    switch(choice) {
    case FT_UP_CHARGE: return FT_LEVEL_UP_CHARGE;
    case FT_UP_RAM:    return FT_LEVEL_UP_RAM;
    case FT_UP_POWER:  return FT_LEVEL_UP_POWER;
    default:           return 0;
    }
}

bool ft_orb_spend(FtStats* s, FtLevelChoice choice) {
    if(s->orbs <= 0) return false;
    if(!ft_level_choice_available(s, choice)) return false;

    const int16_t step = ft_orb_step(choice);

    switch(choice) {
    case FT_UP_CHARGE:
        s->charge_max = (int16_t)(s->charge_max + step);
        if(s->charge_max > FT_CAP_CHARGE) s->charge_max = FT_CAP_CHARGE;
        /* Granted on the spot: moving an orb into HP mid-run is meant to be
         * a thing you can do when you are hurt, not a promise for later. */
        s->charge = (int16_t)(s->charge + step);
        if(s->charge > s->charge_max) s->charge = s->charge_max;
        break;
    case FT_UP_RAM:
        s->ram_max = (int16_t)(s->ram_max + step);
        if(s->ram_max > FT_CAP_RAM) s->ram_max = FT_CAP_RAM;
        s->ram = (int16_t)(s->ram + step);
        if(s->ram > s->ram_max) s->ram = s->ram_max;
        break;
    case FT_UP_POWER:
        s->power = (int16_t)(s->power + step);
        if(s->power > FT_CAP_POWER) s->power = FT_CAP_POWER;
        break;
    default:
        return false;
    }

    s->spent[choice]++;
    s->orbs--;
    return true;
}

bool ft_orb_can_refund(const FtStats* s, FtLevelChoice choice) {
    if(choice >= FT_UP_COUNT) return false;
    return s->spent[choice] > 0u;
}

bool ft_orb_refund(FtStats* s, FtLevelChoice choice) {
    if(!ft_orb_can_refund(s, choice)) return false;

    const int16_t step = ft_orb_step(choice);

    switch(choice) {
    case FT_UP_CHARGE:
        s->charge_max = (int16_t)(s->charge_max - step);
        if(s->charge_max < FT_START_CHARGE) s->charge_max = FT_START_CHARGE;
        if(s->charge > s->charge_max) s->charge = s->charge_max;
        /* Never to zero: taking an orb out should cost you a buffer, not the
         * run. Being downed is something a fight does. */
        if(s->charge < 1) s->charge = 1;
        break;
    case FT_UP_RAM:
        s->ram_max = (int16_t)(s->ram_max - step);
        if(s->ram_max < FT_START_RAM) s->ram_max = FT_START_RAM;
        if(s->ram > s->ram_max) s->ram = s->ram_max;
        break;
    case FT_UP_POWER:
        s->power = (int16_t)(s->power - step);
        if(s->power < FT_START_POWER) s->power = FT_START_POWER;
        break;
    default:
        return false;
    }

    s->spent[choice]--;
    s->orbs++;
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
