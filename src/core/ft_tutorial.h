/* Contextual coaching.
 *
 * The hint is derived from battle state rather than tracked as its own state
 * machine: there is no step counter to get out of sync with the fight, and the
 * whole thing is testable by constructing an encounter and asking what it
 * would say. */
#ifndef FT_TUTORIAL_H
#define FT_TUTORIAL_H

#include "ft_encounter.h"

/* One short line for the player's current situation, or NULL when there is
 * nothing worth saying. Returns NULL for every state when coaching is off.
 *
 * Lines are at most 20 characters — the panel's width budget (DESIGN.md 5.1). */
const char* ft_tutorial_hint(const FtEncounter* e);

/* Longest hint the renderer must be able to place. Guarded by a test. */
#define FT_TUTORIAL_MAX_CHARS 20

#endif /* FT_TUTORIAL_H */
