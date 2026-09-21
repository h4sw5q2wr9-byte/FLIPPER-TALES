/* Overworld map: tiles, collision and camera.
 *
 * Pure C99 like the rest of core — no canvas, no Flipper SDK — so the map
 * maths can be tested and the layouts previewed on a host machine. */
#ifndef FT_MAP_H
#define FT_MAP_H

#include "ft_types.h"

/* 8px tiles give a 16x8 viewport on a 128x64 panel: coarse enough to read at
 * one bit, fine enough that a room is more than a few steps across. */
#define FT_TILE_PX  8

/* The viewport, in tiles. The overworld renderer draws at 2x, so a 128x64
 * panel shows 8x4 of them — the world is still authored, stepped and
 * collided in 8px tiles; only the drawing is scaled. */
#define FT_VIEW_W   8
#define FT_VIEW_H   4

/* The overworld avatar is deliberately smaller than the battle sprite: a
 * 16x16 character would fill a quarter of the viewport's height. */
#define FT_AVATAR_W 8
#define FT_AVATAR_H 12

/* Movement is grid-based but animated: a step slides smoothly from one tile to
 * the next and cannot be interrupted part-way. That keeps the player aligned
 * to tiles — so doorways and foes line up without fighting the controls —
 * while still looking like walking rather than hopping. */
#define FT_STEP_MS      210 /* per tile; ~38 px/s */
#define FT_FOE_STEP_MS  240 /* still slower than the player, but not a crawl */
#define FT_FOE_THINK_MS 150
#define FT_FOE_ALERT    5   /* tiles: how far a foe can notice you */

/* How far a foe will drift from where it was placed. Without a leash an idle
 * room slowly empties as everything random-walks into a corner. */
#define FT_FOE_LEASH    4

typedef enum {
    FT_FACE_DOWN = 0,
    FT_FACE_UP,
    FT_FACE_LEFT,
    FT_FACE_RIGHT
} FtFacing;

typedef enum {
    FT_TILE_FLOOR = 0,
    FT_TILE_WALL,
    FT_TILE_VOID,
    FT_TILE_GRASS,
    FT_TILE_CABLE,
    FT_TILE_DOOR,
    FT_TILE_TERM,
    FT_TILE_LOCK,
    FT_TILE_CRATE,
    FT_TILE_LADDER,
    FT_TILE_COUNT
} FtTile;

/* Some tiles belong to a run and must face the way that run does — a door in a
 * horizontal wall is walked through vertically and reads front-on, while a
 * door in a vertical wall is walked through sideways and must read as a gap.
 *
 * Rather than make map authors pick the right tile (and get it wrong), the
 * renderer asks for an art index and the orientation is derived from the
 * neighbours. Map data keeps one byte per tile. */
#define FT_TILE_ART_DOOR_SIDE FT_TILE_COUNT
#define FT_TILE_ART_LOCK_SIDE (FT_TILE_COUNT + 1)
#define FT_TILE_ART_CABLE_V   (FT_TILE_COUNT + 2)

/* A wall shows its south-facing side where floor lies below it, and a flat cap
 * where the wall continues. The contrast between the two is what gives a wall
 * run apparent height on a flat grid. */
#define FT_TILE_ART_WALL_TOP  (FT_TILE_COUNT + 3)

/* Dithered band laid over whatever sits directly below a wall. */
#define FT_TILE_ART_SHADOW    (FT_TILE_COUNT + 4)

/* Weeds scattered over open floor, placed procedurally rather than authored. */
#define FT_TILE_ART_TUFT      (FT_TILE_COUNT + 5)

#define FT_TILE_ART_COUNT     (FT_TILE_COUNT + 6)

/* Maps are stored as one byte per tile, streamed from the SD card. Kept as a
 * borrowed pointer so a map is never copied into RAM wholesale. */
typedef struct {
    const uint8_t* tiles; /* w * h bytes, row major */
    uint16_t       w;
    uint16_t       h;
    const char*    name;

    /* How much loose greenery to scatter over open floor, 0-255 as a fraction
     * of 256. Zero indoors. Placing this procedurally keeps it out of the map
     * data, where hand-dotting every weed would be unreadable to edit. */
    uint8_t scatter;
} FtMap;

/* Pixel-space position. Sub-tile so movement is smooth rather than grid-locked. */
typedef struct {
    int32_t x;
    int32_t y;
} FtPos;

/* Tile at a tile coordinate. Off-map reads as wall, so the edge of the world
 * is solid without every caller bounds-checking. */
FtTile ft_map_tile(const FtMap* m, int32_t tx, int32_t ty);

/* Can the avatar stand with its feet on this pixel? */
bool ft_map_walkable(const FtMap* m, int32_t px, int32_t py);

/* Would the avatar's footprint collide at this pixel position? The footprint
 * is the lower part of the sprite only, so heads can overlap scenery above. */
bool ft_map_blocked(const FtMap* m, FtPos p);

/* Move with collision, resolving each axis separately so sliding along a wall
 * works instead of sticking. Returns the resolved position. */
FtPos ft_map_move(const FtMap* m, FtPos from, int32_t dx, int32_t dy);

/* Top-left pixel of the viewport that keeps `focus` centred, clamped so the
 * camera never shows outside the map. */
FtPos ft_map_camera(const FtMap* m, FtPos focus);

/* Which art to draw at this position, accounting for orientation. Always less
 * than FT_TILE_ART_COUNT. */
uint8_t ft_map_art_index(const FtMap* m, int32_t tx, int32_t ty);

/* Should a decorative tuft be drawn on this tile? Deterministic, so the same
 * tile always grows the same weed and nothing shimmers as the camera moves. */
bool ft_map_scatter(const FtMap* m, int32_t tx, int32_t ty);

/* Should a wall shadow be laid over this tile? True for walkable tiles with a
 * solid tile directly above. */
bool ft_map_has_shadow(const FtMap* m, int32_t tx, int32_t ty);

/* Is the passage through this tile horizontal — that is, does solid wall sit
 * above and below it? Used to orient doors and locked ports. */
bool ft_map_side_passage(const FtMap* m, int32_t tx, int32_t ty);

/* Does this tile stop movement? */
bool ft_tile_solid(FtTile t);

/* Does stepping onto this tile trigger something (door, terminal)? */
bool ft_tile_interactive(FtTile t);

#endif /* FT_MAP_H */
