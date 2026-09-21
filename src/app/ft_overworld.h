/* Overworld rendering: tilemap, avatar and camera.
 *
 * Like ft_render.c this is the only place that knows about pixels; the map
 * maths lives in core/ft_map.c and is host-tested. */
#ifndef FT_OVERWORLD_H
#define FT_OVERWORLD_H

#include <gui/gui.h>

#include "../core/ft_map.h"

typedef enum {
    FT_FACE_DOWN = 0,
    FT_FACE_UP,
    FT_FACE_LEFT,
    FT_FACE_RIGHT
} FtFacing;

/* How long the area name stays up after entering. */
#define FT_AREA_BANNER_MS 2200

/* Draw the map through a camera centred on the player, then the avatar, then
 * the area banner while it is still due.
 *
 * `step_ms` drives the walk cycle; `area_ms` is time since entering the area. */
void ft_overworld_render(
    Canvas*      canvas,
    const FtMap* map,
    FtPos        player,
    FtFacing     facing,
    uint32_t     step_ms,
    bool         moving,
    uint32_t     area_ms);

#endif /* FT_OVERWORLD_H */
