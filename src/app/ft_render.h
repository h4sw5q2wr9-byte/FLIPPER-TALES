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

#define FT_SCREEN_W 128
#define FT_SCREEN_H 64

/* Horizontal bands. The arena and the skill check share the same space: they
 * never need to be on screen at once, and giving the skill check the full
 * middle is what makes the timing readable. */
#define FT_HEADER_H  9
#define FT_ARENA_Y   11
#define FT_ARENA_H   25
#define FT_STATUS_Y  37
#define FT_ACTION_Y  46

void ft_render_battle(Canvas* canvas, const FtEncounter* e);
void ft_render_help(Canvas* canvas);

#endif /* FT_RENDER_H */
