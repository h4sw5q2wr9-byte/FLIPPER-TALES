/* Deterministic RNG. Seeded explicitly so battles replay identically,
 * which is what makes the headless balance simulator meaningful. */
#ifndef FT_RNG_H
#define FT_RNG_H

#include "ft_types.h"

typedef struct {
    uint32_t state;
} FtRng;

void     ft_rng_seed(FtRng* rng, uint32_t seed);
uint32_t ft_rng_next(FtRng* rng);

/* Uniform in [0, bound). Returns 0 when bound is 0. */
uint32_t ft_rng_below(FtRng* rng, uint32_t bound);

/* True with probability percent/100. */
bool ft_rng_chance(FtRng* rng, uint32_t percent);

#endif /* FT_RNG_H */
