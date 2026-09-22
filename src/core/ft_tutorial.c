#include "ft_tutorial.h"

const char* ft_tutorial_hint(const FtEncounter* e) {
    if(!e->coach) return NULL;

    switch(e->phase) {
    case FT_PHASE_MENU:
        /* The description row under the menu already names what each action
         * does and why a refused one is refused, so the coach must not repeat
         * it. It spends its line on the controls instead. */
        return "LEFT/RIGHT, then OK";

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

        if(atk->klass == FT_CLASS_UNDODGEABLE) return "No jam. PROTECT it.";
        if(ft_encounter_in_ready(e)) {
            if(e->deflect_armed) return "Block = send it back";
            return (atk->klass == FT_CLASS_GUARDED) ? "Dots only: jam it." :
                                                      "Solid end = perfect";
        }
        if(e->guard_pressed) return "Guard set.";
        return "Tap OK at the end";
    }

    case FT_PHASE_IMPACT:
        /* What the deflect did outranks what the block did: the bounce is
         * the thing the player spent a bar and a turn on. */
        if(e->last_deflect_fired) {
            return (e->last_guard == FT_GUARD_CAPTURE) ? "Full bounce back!" :
                                                         "Half back. Aim late.";
        }
        if(e->deflect_armed) return "Missed it. Bar wasted.";
        if(e->last_enemy_hit.perfect) return "Perfect. No damage.";
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
