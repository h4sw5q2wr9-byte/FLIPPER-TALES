#include "ft_combat.h"

static const uint16_t FT_RATING_PCT[FT_RATING_COUNT] = {
    100, /* MISS      */
    110, /* NICE      */
    125, /* GOOD      */
    150, /* GREAT     */
    175, /* AMAZING   */
    200, /* EXCELLENT */
};

uint16_t ft_rating_pct(FtRating rating, bool ante_up) {
    if(rating == FT_RATING_MISS) return ante_up ? 0u : FT_RATING_PCT[FT_RATING_MISS];
    if(rating >= FT_RATING_COUNT) return FT_RATING_PCT[FT_RATING_MISS];
    return FT_RATING_PCT[rating];
}

FtGuard ft_guard_permitted(FtAttackClass klass, FtGuard attempted) {
    switch(klass) {
    case FT_CLASS_UNDODGEABLE:
        return FT_GUARD_NONE;
    case FT_CLASS_GUARDED:
        return (attempted == FT_GUARD_CAPTURE) ? FT_GUARD_JAM : attempted;
    case FT_CLASS_NORMAL:
    default:
        return attempted;
    }
}

int16_t ft_effective_pierce(const FtAttack* atk, const FtDefender* def) {
    int32_t pierce = atk->pierce;
    if(atk->pierce_half) {
        /* Half the shield, rounded up. */
        pierce += (def->shielded + 1) / 2;
    }
    return (int16_t)pierce;
}

/* Does this delivery method simply not work against these attributes? */
static bool ft_delivery_locked(FtDelivery delivery, uint32_t attrs, int8_t* ram_refund) {
    *ram_refund = 0;

    if(delivery == FT_DELIVERY_CONTACT && (attrs & FT_ATTR_AIRBORNE)) {
        /* Out of reach. The turn is spent. */
        return true;
    }
    if(delivery == FT_DELIVERY_BROADCAST && (attrs & FT_ATTR_ENCRYPTED)) {
        /* The broadcast is ignored, but the RAM is handed back so the turn
         * degrades into a Pass rather than being a pure loss. */
        *ram_refund = 1;
        return true;
    }
    return false;
}

FtHitResult ft_resolve_hit(const FtAttack* atk, const FtDefender* def, const FtHitParams* params) {
    FtHitResult res = {FT_HIT_OK, 0, false, false, false, 0};

    if(ft_delivery_locked(atk->delivery, def->attrs, &res.ram_refund)) {
        res.outcome = FT_HIT_LOCKED;
        return res;
    }

    const uint16_t pct = ft_rating_pct(params->rating, params->ante_up);
    if(pct == 0u) {
        res.outcome = FT_HIT_MISSED;
        return res;
    }

    int32_t raw = (int32_t)atk->base_power + params->atk_up - params->atk_down;
    if(raw < FT_MIN_RAW_DAMAGE) raw = FT_MIN_RAW_DAMAGE;
    raw = (raw * (int32_t)pct) / 100;

    int32_t reduction = (int32_t)def->shielded - ft_effective_pierce(atk, def);
    if(reduction < 0) reduction = 0;

    const int32_t effective = raw - reduction;
    if(effective <= 0) {
        res.outcome = FT_HIT_DEFLECTED;
        return res;
    }

    const FtGuard guard = ft_guard_permitted(atk->klass, params->guard);
    int32_t damage = effective;

    switch(guard) {
    case FT_GUARD_CAPTURE:
        damage = 0;
        res.perfect = true;
        res.countered = (atk->delivery == FT_DELIVERY_CONTACT);
        break;

    case FT_GUARD_JAM: {
        uint32_t reduce_pct = params->jam_reduction_pct ? params->jam_reduction_pct
                                                        : FT_JAM_REDUCTION_PCT;
        if(reduce_pct > 100u) reduce_pct = 100u;
        damage = (effective * (int32_t)(100u - reduce_pct)) / 100;
        break;
    }

    case FT_GUARD_NONE:
    default:
        break;
    }

    if(damage < 0) damage = 0;
    res.damage = (int16_t)damage;

    /* Jamming nullifies the payload; capturing avoids the attack entirely. */
    res.payload_applied = (atk->payload != FT_PAYLOAD_NONE) && (guard == FT_GUARD_NONE);

    return res;
}
