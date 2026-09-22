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
 * the wording is testable and the width budget is enforced by the preview.
 *
 * Everything here is written in words rather than codes. The page used to
 * open with "SH2 ENC FST" over "Close 5 keep", which is six facts nobody can
 * decode without the source: a guide that has to be looked up is not a
 * guide. */

/* Width budgets, in characters.
 *
 * A full-width row of the entry page takes FT_GUIDE_LINE_MAX; the two rows
 * that share their row with the sprite take FT_GUIDE_SHORT_MAX. These are
 * enforced by a test rather than by the preview, because draw_clipped does
 * its job silently — an over-long line is not drawn off-panel, it is drawn
 * with the end missing, and the layout checker sees nothing wrong with it. */
#define FT_GUIDE_LINE_MAX  20
#define FT_GUIDE_SHORT_MAX 16

/* "HP 14  Shield 2", or just the HP when nothing is shielded. */
void ft_guide_vitals(FtEnemyId id, char* out, uint8_t cap);

/* The one line worth reading: what to do about this thing.
 *
 * "Hit it with NFC", "Kill it first", "Leave it till last". Derived from the
 * attributes rather than authored per enemy, so a new enemy cannot ship
 * without advice and advice cannot drift from behaviour. */
const char* ft_guide_advice(FtEnemyId id);

/* Two or three words of the same, for the list's value column. */
const char* ft_guide_tag(FtEnemyId id);

/* One line naming what an attribute actually does to the player, or NULL
 * once there are no more. `n` walks 0, 1, 2... */
const char* ft_guide_note(FtEnemyId id, uint8_t n);

/* "5 touch blockable MP-": power, how it reaches you, whether a guard can
 * stop it, and what it leaves behind. Returns false past the last attack,
 * and immediately for anything that never takes a turn. */
bool ft_guide_attack_line(FtEnemyId id, uint8_t n, char* out, uint8_t cap);

/* How many attack lines this enemy actually has. Zero for a bulwark, which
 * carries an attack entry it never uses. */
uint8_t ft_guide_attack_count(FtEnemyId id);

#endif /* FT_GUIDE_H */
