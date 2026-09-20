#include "ft_rng.h"

/* xorshift32. Small, fast, and adequate for game randomness. */

void ft_rng_seed(FtRng* rng, uint32_t seed) {
    /* xorshift32 degenerates at zero, so fold it to a fixed nonzero seed. */
    rng->state = (seed == 0u) ? 0x9E3779B9u : seed;
}

uint32_t ft_rng_next(FtRng* rng) {
    uint32_t x = rng->state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng->state = x;
    return x;
}

uint32_t ft_rng_below(FtRng* rng, uint32_t bound) {
    if(bound == 0u) return 0u;

    /* Rejection sampling: discard the tail that would bias low values. */
    uint32_t limit = UINT32_MAX - (UINT32_MAX % bound);
    uint32_t r;
    do {
        r = ft_rng_next(rng);
    } while(r >= limit);
    return r % bound;
}

bool ft_rng_chance(FtRng* rng, uint32_t percent) {
    if(percent == 0u) return false;
    if(percent >= 100u) return true;
    return ft_rng_below(rng, 100u) < percent;
}
