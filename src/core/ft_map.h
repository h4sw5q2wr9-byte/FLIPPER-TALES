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

/* Movement is grid-based but animated: a step slides smoothly from one tile to
 * the next and cannot be interrupted part-way. That keeps the player aligned
 * to tiles — so doorways and foes line up without fighting the controls —
 * while still looking like walking rather than hopping. */
#define FT_STEP_MS      210 /* per tile; ~38 px/s */
#define FT_FOE_STEP_MS  240 /* still slower than the player, but not a crawl */
#define FT_FOE_THINK_MS 150
#define FT_FOE_ALERT    4   /* walking tiles: how far a foe can notice you */

/* The beat between being seen and being chased.
 *
 * A foe that starts walking on the same frame it notices you gives the player
 * nothing to react to: the first thing you know about it is that it is
 * already moving. Half a second of standing still, with a mark over its head,
 * turns "you were caught" into "you were spotted, and you had a moment" —
 * which is the difference between a chase and an ambush. */
#define FT_FOE_NOTICE_MS 500

/* How often a tree is bearing when you walk into the room.
 *
 * Not always. A tree that always has an apple on it is a button you press on
 * the way past; one that sometimes does is a thing you look at. */
#define FT_TREE_BEARING_PCT 55

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

    /* Area flavour. A chapter that looks like the last one is not a place,
     * it is the same corridor with a different name over the door, so each
     * area gets one ground or obstacle of its own. */
    FT_TILE_SCRAP,  /* The Scrapline: heaped wreckage, solid */
    FT_TILE_FROST,  /* Cold Storage: rimed floor, walkable */
    FT_TILE_PYLON,  /* Signal Hill: a mast base, solid */
    FT_TILE_STATIC, /* The Deadzone: floor under interference, walkable */

    /* A way somebody is holding shut. Walkable — whether you may pass is a
     * quest's business (FtExit.need_quest), not the tile's — but it reads as
     * a gate rather than a door, so a route that is closed to you looks
     * closed to you. */
    FT_TILE_GATE,

    /* ---- Things with a top and a bottom -------------------------------
     *
     * A tree used to be one 16x16 sprite standing on one tile, which is why
     * it looked like a lollipop: real trees in this genre are built out of
     * tiles, with a trunk you bump into and a canopy you walk *behind*.
     *
     * The canopy is walkable and drawn in a second pass over the actors, so
     * the player passes under it. The art has gaps, and blit only sets black
     * pixels, so whoever is behind it shows through the leaves rather than
     * disappearing. */
    FT_TILE_TRUNK, /* solid; the bit you bump into */
    FT_TILE_LEAF,  /* walkable, drawn in front */

    /* The same trick for buildings: a wall you bump into and a roof you walk
     * behind, which is what gives a village any depth at all. */
    FT_TILE_HUT,   /* solid wall */
    FT_TILE_ROOF,  /* walkable, drawn in front */

    /* A doorway in a hut wall. Solid, because it is scenery: nothing in this
     * game goes indoors. It exists because a row of houses with no doors in
     * them reads as crates with roofs on, and a village has to read as a
     * place people live before anyone will believe the people. */
    FT_TILE_HUT_DOOR,

    /* Grass up to your knees. Walkable and drawn in front, like a canopy, so
     * you wade through it with your legs hidden — and nothing happens in it.
     * It is not an encounter zone and never will be: it is where something
     * is hidden, which is a different promise. */
    FT_TILE_TALL_GRASS,

    /* Underground. Rock is a cave's wall — solid, and rough, so a cave never
     * reads as the same thing as a hull. Cave floor is the ground under it. */
    FT_TILE_ROCK,
    FT_TILE_CAVE,

    /* The Scrapline. A drawbridge is solid here — a gap — and the world lets
     * you onto it once its room's bridge is down (ft_world_bridge_down). A
     * receiver is what you point Infrared at to bring it down. A relay is
     * the district's mast, which Hush switched off. */
    FT_TILE_BRIDGE,
    FT_TILE_RECEIVER,
    FT_TILE_RELAY,

    /* Cold Storage. A door nobody drew: solid and drawn as wall until RFID
     * reads it (ft_world_secret_found), then a door like any other. And an
     * archive cabinet, which is where a record is read. */
    FT_TILE_SECRET,
    FT_TILE_ARCHIVE,

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

/* A gate in a vertical wall, same idea as the side door. */
#define FT_TILE_ART_GATE_SIDE (FT_TILE_COUNT + 6)

/* The four outer corners of a canopy, rounded off. A tree is stamped as a 3x3
 * block of one leaf tile, and a 3x3 block of anything is a square; a dark
 * square standing in a field reads as a building, not as a tree. Picked from
 * the neighbours here rather than authored, so maps stay one byte per tile. */
#define FT_TILE_ART_LEAF_TL   (FT_TILE_COUNT + 7)
#define FT_TILE_ART_LEAF_TR   (FT_TILE_COUNT + 8)
#define FT_TILE_ART_LEAF_BL   (FT_TILE_COUNT + 9)
#define FT_TILE_ART_LEAF_BR   (FT_TILE_COUNT + 10)

/* An opening in the ground, drawn over whatever tile a hidden exit stands on
 * once somebody has shown you it is there. Art only: the map keeps the tile
 * it had, so the hole can be hidden in plain sight until then. */
#define FT_TILE_ART_PIT       (FT_TILE_COUNT + 11)

#define FT_TILE_ART_COUNT     (FT_TILE_COUNT + 12)

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

/* Drawn over the actors rather than under them, so you can walk behind it.
 * The art has gaps and the blit only sets black pixels, so you are seen
 * through the leaves rather than swallowed by them. */
bool ft_tile_foreground(FtTile t);

/* Does stepping onto this tile trigger something (door, terminal)? */
bool ft_tile_interactive(FtTile t);

#endif /* FT_MAP_H */
