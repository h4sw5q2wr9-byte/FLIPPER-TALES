/* The field guide: what you know about the things you have fought.
 *
 * One bit per enemy, set the first time one turns up in a battle. Nothing is
 * listed until it has been met, so the guide is a record of the run rather
 * than a manual shipped with it. */
#ifndef FT_GUIDE_H
#define FT_GUIDE_H

#include "ft_data.h"
#include "ft_encounter.h"
#include "ft_types.h"

/* A bitfield wide enough for every enemy, asserted in ft_guide.c. */
typedef struct {
    uint16_t seen;
} FtGuide;

void ft_guide_init(FtGuide* g);

/* Record every foe on the board. Called when a battle starts, not when it is
 * won: meeting something is what teaches you it exists. */
void ft_guide_note_encounter(FtGuide* g, const FtEncounter* e);

bool    ft_guide_knows(const FtGuide* g, FtEnemyId id);
uint8_t ft_guide_count(const FtGuide* g);

/* The nth enemy the guide knows, in table order. FT_ENEMY_COUNT if there is
 * no such entry, so a caller can walk without bounds-checking twice. */
FtEnemyId ft_guide_nth(const FtGuide* g, uint8_t n);

/* ---- Entry text --------------------------------------------------------
 *
 * Short lines for a 128px panel, built here rather than in the renderer so
 * the wording is testable and the width budget is enforced by the preview. */

/* "SH3 ENC JAM", or "-" for a plain enemy. */
void ft_guide_traits(FtEnemyId id, char* out, uint8_t cap);

/* One line naming what the attribute actually does to the player, or NULL
 * once there are no more. `n` walks 0, 1, 2... */
const char* ft_guide_note(FtEnemyId id, uint8_t n);

/* "Clamp  5  jam+cap" — an attack's name, power and what a guard can do
 * about it. Returns false past the last attack. */
bool ft_guide_attack_line(FtEnemyId id, uint8_t n, char* out, uint8_t cap);

#endif /* FT_GUIDE_H */
