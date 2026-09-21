#include "ft_map.h"

bool ft_tile_solid(FtTile t) {
    switch(t) {
    case FT_TILE_WALL:
    case FT_TILE_VOID:
    case FT_TILE_CRATE:
    case FT_TILE_LOCK: /* until the iButton module opens it */
        return true;
    default:
        return false;
    }
}

bool ft_tile_interactive(FtTile t) {
    return t == FT_TILE_DOOR || t == FT_TILE_TERM;
}

bool ft_map_side_passage(const FtMap* m, int32_t tx, int32_t ty) {
    const bool wall_above = ft_tile_solid(ft_map_tile(m, tx, ty - 1));
    const bool wall_below = ft_tile_solid(ft_map_tile(m, tx, ty + 1));
    const bool wall_left = ft_tile_solid(ft_map_tile(m, tx - 1, ty));
    const bool wall_right = ft_tile_solid(ft_map_tile(m, tx + 1, ty));

    /* Walls above and below mean the wall runs vertically, so the way through
     * is left-to-right: a side-on gap. Walls either side mean the opposite. */
    if(wall_above && wall_below && !(wall_left && wall_right)) return true;

    /* Ambiguous (a doorway in a corner, or standing alone) falls back to
     * front-facing, which is the one that reads as a door at all. */
    return false;
}

static bool cable_is_vertical(const FtMap* m, int32_t tx, int32_t ty) {
    const bool up = ft_map_tile(m, tx, ty - 1) == FT_TILE_CABLE;
    const bool down = ft_map_tile(m, tx, ty + 1) == FT_TILE_CABLE;
    const bool left = ft_map_tile(m, tx - 1, ty) == FT_TILE_CABLE;
    const bool right = ft_map_tile(m, tx + 1, ty) == FT_TILE_CABLE;

    /* Conduit follows its own run; a lone tile stays horizontal. */
    return (up || down) && !(left || right);
}

bool ft_map_scatter(const FtMap* m, int32_t tx, int32_t ty) {
    if(!m || m->scatter == 0u) return false;

    /* Only bare floor. Growing weeds through a crate or a doorway looks like
     * a bug rather than like nature. */
    if(ft_map_tile(m, tx, ty) != FT_TILE_FLOOR) return false;

    /* Cheap spatial hash: deterministic per tile, so the scatter is stable as
     * the camera scrolls rather than reseeding every frame. */
    uint32_t h = ((uint32_t)tx * 73856093u) ^ ((uint32_t)ty * 19349663u);
    h ^= h >> 13;
    h *= 0x5BD1E995u;
    h ^= h >> 15;

    return (h & 0xFFu) < m->scatter;
}

bool ft_map_has_shadow(const FtMap* m, int32_t tx, int32_t ty) {
    if(ft_tile_solid(ft_map_tile(m, tx, ty))) return false;
    return ft_tile_solid(ft_map_tile(m, tx, ty - 1));
}

uint8_t ft_map_art_index(const FtMap* m, int32_t tx, int32_t ty) {
    const FtTile t = ft_map_tile(m, tx, ty);

    switch(t) {
    case FT_TILE_WALL:
        /* Cap where the wall carries on downwards, face where it meets the
         * floor and the player is looking at its side. */
        return ft_tile_solid(ft_map_tile(m, tx, ty + 1)) ? FT_TILE_ART_WALL_TOP :
                                                           (uint8_t)t;
    case FT_TILE_DOOR:
        return ft_map_side_passage(m, tx, ty) ? FT_TILE_ART_DOOR_SIDE : (uint8_t)t;
    case FT_TILE_LOCK:
        return ft_map_side_passage(m, tx, ty) ? FT_TILE_ART_LOCK_SIDE : (uint8_t)t;
    case FT_TILE_CABLE:
        return cable_is_vertical(m, tx, ty) ? FT_TILE_ART_CABLE_V : (uint8_t)t;
    default:
        return (uint8_t)t;
    }
}

FtTile ft_map_tile(const FtMap* m, int32_t tx, int32_t ty) {
    /* Off-map is solid, so the world has edges without every caller checking. */
    if(!m || !m->tiles) return FT_TILE_WALL;
    if(tx < 0 || ty < 0 || tx >= (int32_t)m->w || ty >= (int32_t)m->h) return FT_TILE_WALL;

    const uint8_t v = m->tiles[(uint32_t)ty * m->w + (uint32_t)tx];
    return (v < FT_TILE_COUNT) ? (FtTile)v : FT_TILE_WALL;
}

bool ft_map_walkable(const FtMap* m, int32_t px, int32_t py) {
    return !ft_tile_solid(ft_map_tile(m, px / FT_TILE_PX, py / FT_TILE_PX));
}

bool ft_map_blocked(const FtMap* m, FtPos p) {
    /* Only the lower third of the avatar collides. This is the standard
     * top-down trick: it lets the character's head pass in front of walls, so
     * rooms feel deeper than a flat grid. */
    const int32_t top = p.y + FT_AVATAR_H - 4;
    const int32_t bottom = p.y + FT_AVATAR_H - 1;
    const int32_t left = p.x;
    const int32_t right = p.x + FT_AVATAR_W - 1;

    return !ft_map_walkable(m, left, top) || !ft_map_walkable(m, right, top) ||
           !ft_map_walkable(m, left, bottom) || !ft_map_walkable(m, right, bottom);
}

FtPos ft_map_move(const FtMap* m, FtPos from, int32_t dx, int32_t dy) {
    FtPos p = from;

    /* Axes resolved separately so a diagonal into a wall slides along it
     * instead of stopping dead. */
    if(dx != 0) {
        const FtPos t = {p.x + dx, p.y};
        if(!ft_map_blocked(m, t)) p = t;
    }
    if(dy != 0) {
        const FtPos t = {p.x, p.y + dy};
        if(!ft_map_blocked(m, t)) p = t;
    }

    return p;
}

FtPos ft_map_camera(const FtMap* m, FtPos focus) {
    const int32_t view_w = FT_VIEW_W * FT_TILE_PX;
    const int32_t view_h = FT_VIEW_H * FT_TILE_PX;

    FtPos c = {focus.x + FT_AVATAR_W / 2 - view_w / 2,
               focus.y + FT_AVATAR_H / 2 - view_h / 2};

    const int32_t max_x = (int32_t)m->w * FT_TILE_PX - view_w;
    const int32_t max_y = (int32_t)m->h * FT_TILE_PX - view_h;

    /* A map smaller than the viewport pins to the origin rather than going
     * negative and showing a band of off-map wall. */
    if(c.x > max_x) c.x = max_x;
    if(c.y > max_y) c.y = max_y;
    if(c.x < 0) c.x = 0;
    if(c.y < 0) c.y = 0;

    return c;
}
