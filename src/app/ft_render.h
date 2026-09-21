/* All canvas drawing for the battle screen. See DESIGN.md 5.
 *
 * This is the only file that knows about pixels; everything it draws comes
 * straight out of FtEncounter, which the core owns. The layout is verified on
 * a host machine by test/preview.c, which renders every phase to an image and
 * fails if anything lands off-panel. */
#ifndef FT_RENDER_H
#define FT_RENDER_H

#include <gui/gui.h>

#include "../core/ft_encounter.h"
#include "../core/ft_practice.h"

#define FT_SCREEN_W 128
#define FT_SCREEN_H 64

/* Horizontal bands. The arena and the skill check share the same space: they
 * never need to be on screen at once, and giving the skill check the full
 * middle is what makes the timing readable. */
#define FT_HEADER_H  9
#define FT_ARENA_Y   11
#define FT_ARENA_H   25
#define FT_STATUS_Y  37
#define FT_ACTION_Y  45

#define FT_HELP_PAGES 4

/* The pause menu, opened with Back. */
typedef enum {
    FT_PAUSE_RESUME = 0,
    FT_PAUSE_SAVE,
    FT_PAUSE_HELP,
    FT_PAUSE_TIPS,
    FT_PAUSE_DEBUG,
    FT_PAUSE_NEWGAME,
    FT_PAUSE_QUIT,
    FT_PAUSE_COUNT
} FtPauseItem;

/* How many rows of a list fit under the title. */
#define FT_MENU_VISIBLE 5

/* Every menu in the game is this one list: a title, rows, an optional value
 * on each row, and a scrollbar when there is more than fits. */
void ft_render_menu_list(
    Canvas*            canvas,
    const char*        title,
    const char* const* items,
    const char* const* values,
    uint8_t            count,
    uint8_t            selected);

void ft_render_pause(Canvas* canvas, uint8_t selected, bool tips_on);

/* Everything that exists to test the game rather than to play it. Kept
 * together behind one door so the pause menu stays the player's. */
typedef enum {
    FT_DEBUG_TRAVEL = 0, /* walk into any room without walking there */
    FT_DEBUG_PRACTICE,
    FT_DEBUG_HEAL,
    FT_DEBUG_XP,
    FT_DEBUG_CLEAR,
    FT_DEBUG_BACK,
    FT_DEBUG_COUNT
} FtDebugItem;

void ft_render_debug(Canvas* canvas, uint8_t selected, const char* room_name);

/* The practice arena's setup screen. */
void ft_render_practice(Canvas* canvas, const FtPractice* p);

/* A yes/no gate in front of something irreversible. */
void ft_render_confirm(Canvas* canvas, const char* what, bool yes);

/* The level-up screen: pick which stat the level goes into. `owed` is how
 * many more follow this one, so the player knows to expect them. */
void ft_render_levelup(
    Canvas* canvas, const FtStats* stats, uint8_t selected, int16_t owed);

void ft_render_battle(Canvas* canvas, const FtEncounter* e);

/* The closing/opening ring, over whatever is already on the canvas. Shared by
 * the hit transition and the wipe in and out of a fight. */
void ft_render_iris(Canvas* canvas, uint8_t amount);
void ft_render_help(Canvas* canvas, uint8_t page);

#endif /* FT_RENDER_H */
