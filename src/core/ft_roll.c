#include "ft_roll.h"

void ft_roll_init(FtRoll* roll, int16_t charge) {
    roll->current = charge;
    roll->target = charge;
    roll->accum_ms = 0;
}

uint32_t ft_roll_interval_ms(int16_t shielded, bool defending, bool hard_mode) {
    if(shielded < 0) shielded = 0;

    /* Each point of shield slows the drain by 10%. */
    uint32_t interval = ((uint32_t)FT_ROLL_BASE_INTERVAL_MS * (100u + 10u * (uint32_t)shielded)) / 100u;

    if(defending) interval *= 4u;  /* -75% roll speed */
    if(hard_mode) interval /= 2u;  /* Hard Mode doubles the roll speed */

    return interval ? interval : 1u;
}

void ft_roll_apply_damage(FtRoll* roll, int16_t damage) {
    if(damage <= 0) return;

    roll->target -= damage;
    if(roll->target < 0) roll->target = 0;

    /* Nothing survives a hit this large, so skip the drama. */
    if(damage >= FT_INSTANT_DAMAGE_THRESHOLD) {
        roll->current = roll->target;
        roll->accum_ms = 0;
    }
}

void ft_roll_heal(FtRoll* roll, int16_t amount, int16_t charge_max) {
    if(amount <= 0) return;

    roll->target += amount;
    if(roll->target > charge_max) roll->target = charge_max;

    /* Healing is immediate, so a brownout can be escaped by out-healing it. */
    roll->current += amount;
    if(roll->current > charge_max) roll->current = charge_max;
    if(roll->current > roll->target) roll->current = roll->target;
}

void ft_roll_tick(FtRoll* roll, uint32_t dt_ms, uint32_t interval_ms) {
    if(roll->current <= roll->target) {
        roll->accum_ms = 0;
        return;
    }
    if(interval_ms == 0u) interval_ms = 1u;

    roll->accum_ms += dt_ms;

    const uint32_t steps = roll->accum_ms / interval_ms;
    if(steps == 0u) return;
    roll->accum_ms -= steps * interval_ms;

    const int32_t remaining = (int32_t)roll->current - (int32_t)roll->target;
    const int32_t drop = ((int32_t)steps < remaining) ? (int32_t)steps : remaining;

    roll->current -= (int16_t)drop;
}

bool ft_roll_active(const FtRoll* roll) {
    return roll->current > roll->target;
}

bool ft_roll_brownout(const FtRoll* roll) {
    return roll->target <= 0 && roll->current > 0;
}

bool ft_roll_down(const FtRoll* roll) {
    return roll->current <= 0;
}
