/* Rolling Charge. See DESIGN.md 4.6.
 *
 * Damage does not apply instantly: Charge ticks toward a target over real
 * time. A lethal hit therefore leaves a window in which the player can still
 * act, heal out of it, finish the battle, or flee and keep what is on screen. */
#ifndef FT_ROLL_H
#define FT_ROLL_H

#include "ft_types.h"

typedef struct {
    int16_t  current;
    int16_t  target;
    uint32_t accum_ms;
} FtRoll;

void ft_roll_init(FtRoll* roll, int16_t charge);

/* Milliseconds per single point of Charge. Never returns 0. */
uint32_t ft_roll_interval_ms(int16_t shielded, bool defending, bool hard_mode);

/* Queue damage. Damage at or above FT_INSTANT_DAMAGE_THRESHOLD skips the roll. */
void ft_roll_apply_damage(FtRoll* roll, int16_t damage);

/* Healing applies immediately and raises the target, cancelling a brownout. */
void ft_roll_heal(FtRoll* roll, int16_t amount, int16_t charge_max);

/* Advance by dt_ms. Call only while the roll is running — it must be paused
 * during the thinking phase, so the player is never drained by their own
 * deliberation. */
void ft_roll_tick(FtRoll* roll, uint32_t dt_ms, uint32_t interval_ms);

/* Still settling toward the target. */
bool ft_roll_active(const FtRoll* roll);

/* Headed for zero but not there yet: the window in which the player can act. */
bool ft_roll_brownout(const FtRoll* roll);

/* Charge has actually reached zero. */
bool ft_roll_down(const FtRoll* roll);

#endif /* FT_ROLL_H */
