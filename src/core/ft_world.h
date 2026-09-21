/* The overworld: which room you are in, where you stand, what is in it.
 *
 * Pure C99 like the rest of core. The app feeds it input and dt and renders
 * what it holds; every rule about walking, doors, contact and defeat lives
 * here so it can be tested without a device. */
#ifndef FT_WORLD_H
#define FT_WORLD_H

#include "ft_data.h"
#include "ft_map.h"
#include "ft_progress.h"
#include "ft_signal.h"

#define FT_MAX_ROOM_ENTS 6
#define FT_MAX_ROOM_EXITS 4

typedef enum {
    FT_ENT_NONE = 0,
    FT_ENT_FOE
} FtEntKind;

typedef struct {
    FtEntKind kind;
    uint8_t   tx, ty;
    uint8_t   roster; /* the group this one fights as */
} FtEntity;

/* A door and where it leads. */
typedef struct {
    uint8_t tx, ty;
    uint8_t dest_room;
    uint8_t dest_tx, dest_ty;
} FtExit;

typedef struct {
    const FtMap*    map;
    const FtExit*   exits;
    uint8_t         exit_count;
    const FtEntity* ents;
    uint8_t         ent_count;
} FtRoom;

/* A roster is what one visible foe actually fights as — the sprite you can
 * see, plus friends you cannot. */
typedef struct {
    uint8_t   count;
    FtEnemyId foes[FT_MAX_ENEMIES];
} FtRoster;

const FtRoom*   ft_room(uint8_t index);
uint8_t         ft_room_count(void);
const FtRoster* ft_roster(uint8_t index);

typedef struct {
    uint8_t  room;
    FtPos    pos;
    FtFacing facing;
    bool     moving;
    uint32_t step_ms;    /* walk cycle */
    uint32_t walk_accum; /* sub-pixel movement, in thousandths of a pixel */
    uint32_t area_ms; /* since entering the room, for the name banner */

    /* Which entities are gone, one bit per entity per room. Defeated foes stay
     * down for the visit rather than reappearing behind you. */
    uint8_t cleared[8];

    /* Carried between battles, since an encounter starts from scratch. */
    FtStats         stats;
    FtSignalLibrary lib;
    FtLoadout       loadout;
} FtWorld;

void ft_world_init(FtWorld* w);

/* Move to a room and stand on a tile. Resets the area banner. */
void ft_world_enter(FtWorld* w, uint8_t room, uint8_t tx, uint8_t ty);

/* Advance time and walk. dx/dy are -1, 0 or 1; facing follows the last
 * non-zero direction even when movement is blocked. */
void ft_world_walk(FtWorld* w, int8_t dx, int8_t dy, uint32_t dt_ms);

const FtMap* ft_world_map(const FtWorld* w);

bool ft_world_entity_gone(const FtWorld* w, uint8_t index);
void ft_world_clear_entity(FtWorld* w, uint8_t index);

/* Index of a living foe whose tile the player is standing on, else -1. */
int ft_world_foe_contact(const FtWorld* w);

/* Index of a living foe on the tile the player faces, else -1. This is the
 * one you can strike first. */
int ft_world_foe_ahead(const FtWorld* w);

/* The exit under the player's feet, or NULL. */
const FtExit* ft_world_exit_under(const FtWorld* w);

/* Is the player standing on, or facing, a terminal? */
bool ft_world_terminal_near(const FtWorld* w);

/* Tile the player's feet occupy. */
void ft_world_foot_tile(const FtWorld* w, int32_t* tx, int32_t* ty);

#endif /* FT_WORLD_H */
