#include "ft_map.h"

bool ft_tile_foreground(FtTile t) {
    return t == FT_TILE_LEAF || t == FT_TILE_ROOF || t == FT_TILE_TALL_GRASS;
}

bool ft_tile_solid(FtTile t) {
    switch(t) {
    case FT_TILE_WALL:
    case FT_TILE_VOID:
    case FT_TILE_CRATE:
    case FT_TILE_LOCK: /* until the iButton module opens it */
    case FT_TILE_SCRAP:
    case FT_TILE_PYLON:
    case FT_TILE_TRUNK:
    case FT_TILE_HUT:
    case FT_TILE_HUT_DOOR:
    case FT_TILE_ROCK:
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

/* Trunk counts as canopy for shaping purposes: it is the middle of the same
 * 3x3 block, so the leaf above it must not think it is on an edge. */
static bool canopy(const FtMap* m, int32_t tx, int32_t ty) {
    const FtTile t = ft_map_tile(m, tx, ty);
    return t == FT_TILE_LEAF || t == FT_TILE_TRUNK;
}

/* Which corner of a canopy this leaf is, if any. A leaf with no canopy above
 * and none to its left is the top-left of the block, and so on; a leaf with
 * canopy on both of a pair of axes is interior and keeps the plain art. */
static uint8_t leaf_art(const FtMap* m, int32_t tx, int32_t ty) {
    const bool up = canopy(m, tx, ty - 1);
    const bool down = canopy(m, tx, ty + 1);
    const bool left = canopy(m, tx - 1, ty);
    const bool right = canopy(m, tx + 1, ty);

    if(!up && !left) return FT_TILE_ART_LEAF_TL;
    if(!up && !right) return FT_TILE_ART_LEAF_TR;
    if(!down && !left) return FT_TILE_ART_LEAF_BL;
    if(!down && !right) return FT_TILE_ART_LEAF_BR;
    return (uint8_t)FT_TILE_LEAF;
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
    const FtTile ground = ft_map_tile(m, tx, ty);
    if(ground != FT_TILE_FLOOR && ground != FT_TILE_FROST &&
       ground != FT_TILE_STATIC) {
        return false;
    }

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
    case FT_TILE_GATE:
        return ft_map_side_passage(m, tx, ty) ? FT_TILE_ART_GATE_SIDE : (uint8_t)t;
    case FT_TILE_CABLE:
        return cable_is_vertical(m, tx, ty) ? FT_TILE_ART_CABLE_V : (uint8_t)t;
    case FT_TILE_LEAF:
        return leaf_art(m, tx, ty);
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
    /* Only the actor's feet collide. This is the standard top-down trick: it
     * lets a character's head pass in front of walls, so rooms feel deeper
     * than a flat grid.
     *
     * Kept in tile units rather than sprite units — how tall the art is is
     * the renderer's business, and core knowing about it is what let a stale
     * avatar height survive two redraws of the character. */
    const int32_t top = p.y + FT_TILE_PX - 4;
    const int32_t bottom = p.y + FT_TILE_PX - 1;
    const int32_t left = p.x;
    const int32_t right = p.x + FT_TILE_PX - 1;

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

    FtPos c = {focus.x + FT_TILE_PX / 2 - view_w / 2,
               focus.y + FT_TILE_PX / 2 - view_h / 2};

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
