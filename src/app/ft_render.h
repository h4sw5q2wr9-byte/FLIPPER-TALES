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
#include "../core/ft_guide.h"
#include "../core/ft_practice.h"
#include "../core/ft_quest.h"
#include "../core/ft_world.h"

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
    FT_PAUSE_POCKETS,
    FT_PAUSE_ORBS,
    FT_PAUSE_QUESTS,
    FT_PAUSE_SAVE,
    FT_PAUSE_GUIDE,
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

/* `orbs` is what is in hand, shown on the Orbs row so the player never has to
 * open the screen to find out there is nothing to place. */
void ft_render_pause(
    Canvas* canvas, uint8_t selected, bool tips_on, int16_t orbs, bool in_battle);

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

/* The field guide: a list of what has been met, and a page per entry. */
void ft_render_guide_list(Canvas* canvas, const FtGuide* g, uint8_t selected);
void ft_render_guide_entry(Canvas* canvas, FtEnemyId id);

/* The practice arena's setup screen. */
void ft_render_practice(Canvas* canvas, const FtPractice* p);

/* A yes/no gate in front of something irreversible. */
void ft_render_confirm(Canvas* canvas, const char* what, bool yes);

/* The orb screen: where the points earned by levelling and by quests sit, and
 * where they are moved between HP, MP and Cards. Reachable from the pause
 * menu at any time outside a fight, which is the whole point of it — nothing
 * placed here is ever final. */
void ft_render_orbs(Canvas* canvas, const FtStats* stats, uint8_t selected);

/* What has been asked of you, one row each. Read-only. */
void ft_render_quests(Canvas* canvas, const FtQuests* q, uint8_t selected);

/* What you are carrying, and room for how much more. */
void ft_render_pockets(
    Canvas* canvas, const FtPockets* p, uint8_t selected, const FtStats* stats);

/* Somebody talking: up to FT_QUEST_LINES lines in a box, and a prompt. */
void ft_render_talk(Canvas* canvas, const char* who, const FtQuestTalk* t);

void ft_render_battle(Canvas* canvas, const FtEncounter* e);

/* The closing/opening ring, over whatever is already on the canvas. Shared by
 * the hit transition and the wipe in and out of a fight. */
void ft_render_iris(Canvas* canvas, uint8_t amount);
void ft_render_help(Canvas* canvas, uint8_t page);

#endif /* FT_RENDER_H */
