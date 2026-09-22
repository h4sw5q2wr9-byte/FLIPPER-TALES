/* Damage resolution. See DESIGN.md 4.2-4.5. */
#ifndef FT_COMBAT_H
#define FT_COMBAT_H

#include "ft_types.h"

/* A single incoming or outgoing attack. */
typedef struct {
    uint16_t      id;             /* stable id, for the field guide */
    int16_t       base_power;
    uint8_t       pierce;         /* flat shield ignored */
    bool          pierce_half;    /* additionally ignore half the shield,
                                     rounded up (NFC's property) */
    FtDelivery    delivery;
    FtAttackClass klass;
    FtPayload     payload;
} FtAttack;

/* Whoever is being hit. The player simply has attrs == 0. */
typedef struct {
    int16_t  shielded;
    uint32_t attrs;
} FtDefender;

typedef struct {
    int16_t  atk_up;
    int16_t  atk_down;
    FtRating rating;
    bool     ante_up;           /* a missed action command deals nothing */
    FtGuard  guard;             /* what the defender attempted */
    uint8_t  jam_reduction_pct; /* 0 means use FT_JAM_REDUCTION_PCT */
} FtHitParams;

typedef enum {
    FT_HIT_OK = 0,
    FT_HIT_MISSED,    /* action command missed under Ante Up */
    FT_HIT_DEFLECTED, /* shield met or exceeded the incoming power */
    FT_HIT_LOCKED     /* wrong delivery for the target's attributes */
} FtHitOutcome;

typedef struct {
    FtHitOutcome outcome;
    int16_t      damage;
    bool         payload_applied;
    bool         perfect;    /* a capture-grade block: frame-perfect timing */
    bool         countered;   /* capture against a contact attack counters it */
    int8_t       ram_refund;  /* ENCRYPTED refunds RAM, making the turn a Pass */
} FtHitResult;

/* Rating multiplier as a percentage. A miss is 100% normally, 0% under Ante Up. */
uint16_t ft_rating_pct(FtRating rating, bool ante_up);

/* Clamp an attempted guard to what the attack class permits:
 * UNDODGEABLE refuses everything, GUARDED downgrades CAPTURE to JAM. */
FtGuard ft_guard_permitted(FtAttackClass klass, FtGuard attempted);

/* Total shield ignored by this attack against this defender. */
int16_t ft_effective_pierce(const FtAttack* atk, const FtDefender* def);

FtHitResult ft_resolve_hit(const FtAttack* atk, const FtDefender* def, const FtHitParams* params);

#endif /* FT_COMBAT_H */
