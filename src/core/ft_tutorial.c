#include "ft_tutorial.h"

const char* ft_tutorial_hint(const FtEncounter* e) {
    if(!e->coach) return NULL;

    switch(e->phase) {
    case FT_PHASE_MENU:
        /* Call out a locked module the moment it is highlighted: the strike
         * through the label means nothing until someone says why. */
        if(!ft_encounter_action_available(e, (FtAction2)e->menu_index)) {
            const uint32_t attrs = ft_encounter_enemy(e)->attrs;
            if(attrs & FT_ATTR_AIRBORNE) return "It flies: use SUBGHZ";
            if(attrs & FT_ATTR_ENCRYPTED) return "Encrypted. Use NFC.";
            return "That one won't work.";
        }
        return "Pick a module + OK";

    case FT_PHASE_PLAYER_ACT:
        if(ft_encounter_in_ready(e)) return "Wait for the pips...";
        if(e->action_pressed) return "Locked in.";
        return "Tap OK in the black";

    case FT_PHASE_RESULT:
        switch(e->last_player_hit.outcome) {
        case FT_HIT_LOCKED:    return "Wrong tool. Swap.";
        case FT_HIT_DEFLECTED: return "Shield held. Hit up.";
        case FT_HIT_MISSED:    return "Too early or late.";
        default:               break;
        }
        if(e->last_rating == FT_RATING_EXCELLENT) return "Dead centre. Double!";
        if(e->last_rating == FT_RATING_MISS) return "Aim for the black.";
        return "Closer in = more.";

    case FT_PHASE_TELEGRAPH: {
        const FtAttack* atk = ft_encounter_incoming(e);
        if(atk == NULL) return NULL;

        if(atk->klass == FT_CLASS_UNDODGEABLE) return "No guard. Brace.";
        if(ft_encounter_in_ready(e)) {
            return (atk->klass == FT_CLASS_GUARDED) ? "Dots only: jam it." :
                                                      "Solid end = capture";
        }
        if(e->guard_pressed) return "Guard set.";
        return "Tap OK at the end";
    }

    case FT_PHASE_IMPACT:
        if(e->last_enemy_hit.captured) return "Kept it! See S pips";
        if(e->last_guard == FT_GUARD_JAM) return "Halved. Tap later.";
        if(e->last_enemy_hit.damage > 0) return "Missed. Tap later.";
        return NULL;

    case FT_PHASE_WIN:
    case FT_PHASE_LOSE:
    case FT_PHASE_DRAIN:
    default:
        return NULL;
    }
}
