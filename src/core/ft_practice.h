/* The practice arena: pick a fight and play it, without walking to one.
 *
 * Everything about a practice match is decided here, in pure code, so the
 * setup can be tested and previewed rather than only tried on hardware. The
 * app owns the screen and the input; this owns what the settings mean.
 */
#ifndef FT_PRACTICE_H
#define FT_PRACTICE_H

#include "ft_encounter.h"

/* Rows on the setup screen, in the order they are drawn. */
typedef enum {
    FT_PRACTICE_FOES = 0,
    FT_PRACTICE_LEVEL,
    FT_PRACTICE_KIT,
    FT_PRACTICE_FIGHT,
    FT_PRACTICE_ROWS
} FtPracticeRow;

/* What you are fighting. FT_FOES_RANDOM rolls a fresh group every match, so
 * "give me something" is one press rather than a menu crawl. */
#define FT_FOES_RANDOM 0
#define FT_PRACTICE_GROUPS 8 /* RANDOM + the fixed line-ups below */

/* How much you bring. The point of the arena is trying the whole kit, so the
 * loaded options exist to make Signal, replays and stacked cards reachable
 * without grinding to them. */
typedef enum {
    FT_KIT_BASIC = 0, /* what you start the game with */
    FT_KIT_LOADED,    /* every card, one stack each */
    FT_KIT_MAX,       /* every card, stacked as far as it goes */
    FT_KIT_COUNT
} FtKit;

#define FT_PRACTICE_MAX_LEVEL 10

typedef struct {
    uint8_t row;   /* cursor */
    uint8_t group; /* FT_FOES_RANDOM, or 1 + index into the fixed line-ups */
    uint8_t level; /* 1..FT_PRACTICE_MAX_LEVEL */
    uint8_t kit;   /* FtKit */
    uint32_t seed; /* advanced every match so RANDOM keeps moving */
} FtPractice;

void ft_practice_init(FtPractice* p, uint32_t seed);

/* Move the cursor, and change the value on the row it is on. Both wrap. */
void ft_practice_move(FtPractice* p, int8_t delta);
void ft_practice_adjust(FtPractice* p, int8_t delta);

/* Labels for the current settings, short enough for the panel. */
const char* ft_practice_row_name(uint8_t row);
const char* ft_practice_value(const FtPractice* p, uint8_t row);

/* One line saying what the highlighted row does. */
const char* ft_practice_help(const FtPractice* p);

/* Build the match. Consumes a step of the seed, so calling it twice with
 * FT_FOES_RANDOM gives two different fights. */
void ft_practice_start(FtPractice* p, FtEncounter* e);

/* The loadout a kit means, exposed for the setup screen and for tests. */
void ft_practice_loadout(uint8_t kit, FtLoadout* lo);

#endif /* FT_PRACTICE_H */
