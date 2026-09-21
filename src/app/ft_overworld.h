/* Overworld rendering: tilemap, avatar and camera.
 *
 * Like ft_render.c this is the only place that knows about pixels; the map
 * maths lives in core/ft_map.c and is host-tested. */
#ifndef FT_OVERWORLD_H
#define FT_OVERWORLD_H

#include <gui/gui.h>

#include "../core/ft_world.h"

/* How long the area name stays up after entering. */
#define FT_AREA_BANNER_MS 2200

/* A brief one-line message, drawn over the map. */
void ft_overworld_toast(Canvas* canvas, const char* text);

/* Draw the current room through a camera centred on the player: tiles, then
 * the foes still standing, then the avatar, then the area banner while it is
 * still due.
 *
 * Takes the world rather than loose parameters because it needs the room, the
 * position, the walk clock and which foes are already defeated — passing those
 * separately meant threading a callback just to ask the last question. */
void ft_overworld_render(Canvas* canvas, const FtWorld* w);


#endif /* FT_OVERWORLD_H */
