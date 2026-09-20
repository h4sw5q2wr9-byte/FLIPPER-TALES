/* All canvas drawing for the battle screen. See DESIGN.md 5.
 *
 * This is the only place that knows about pixels; everything it draws is read
 * straight out of FtEncounter, which the core owns. */
#ifndef FT_RENDER_H
#define FT_RENDER_H

#include <gui/gui.h>

#include "../core/ft_encounter.h"

/* Screen regions, 128x64 (DESIGN.md 5). */
#define FT_SCREEN_W 128
#define FT_SCREEN_H 64

#define FT_STRIP_H   12 /* enemy name and attributes */
#define FT_SCENE_Y   12
#define FT_SCENE_H   32
#define FT_BARS_Y    44
#define FT_MENU_Y    54

void ft_render_battle(Canvas* canvas, const FtEncounter* e);

#endif /* FT_RENDER_H */
