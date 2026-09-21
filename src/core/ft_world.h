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

/* A roster is what one visible foe fights as. The whole group also *walks*
 * as a group out here, so what you see is what you are about to fight. */
typedef struct {
    uint8_t   count;
    FtEnemyId foes[FT_MAX_ENEMIES];
} FtRoster;

const FtRoom*   ft_room(uint8_t index);
uint8_t         ft_room_count(void);
const FtRoster* ft_roster(uint8_t index);

/* A tile-aligned actor mid-step. */
typedef struct {
    uint8_t  tx, ty;
    int8_t   dx, dy;   /* direction of the step in progress, 0 when idle */
    uint32_t step_ms;  /* elapsed within the current step */
} FtStepper;

typedef struct {
    FtStepper mv;
    uint32_t  think_ms;
    bool      alive;

    /* Where this one was placed. It drifts around here rather than wandering
     * off, so a room keeps its shape. */
    uint8_t home_tx, home_ty;

    /* Its own wander state, so foes in a room do not move in lockstep. */
    uint32_t seed;

    /* Alerted foes head for the player. Alert is shared across the room: one
     * of them noticing you brings the rest. */
    bool alert;
} FtFoeState;

typedef struct {
    uint8_t   room;
    FtStepper mv;
    FtFacing  facing;
    uint32_t  walk_ms; /* walk cycle, keeps running across steps */
    uint32_t  area_ms; /* since entering the room, for the name banner */

    /* Set for one update when a step finishes, so the app can react to what
     * was landed on without polling every frame. */
    bool arrived;

    FtFoeState foes[FT_MAX_ROOM_ENTS];

    /* Which entities are gone, one bit per entity per room. */
    uint8_t cleared[8];

    /* Carried between battles, since an encounter starts from scratch. */
    FtStats         stats;
    FtSignalLibrary lib;
    FtLoadout       loadout;
} FtWorld;

void ft_world_init(FtWorld* w);
void ft_world_enter(FtWorld* w, uint8_t room, uint8_t tx, uint8_t ty);

/* Advance everything by dt. dx/dy are the held direction, -1/0/1. A step in
 * progress runs to completion regardless of input. */
void ft_world_update(FtWorld* w, int8_t dx, int8_t dy, uint32_t dt_ms);

const FtMap* ft_world_map(const FtWorld* w);

/* Pixel position of an actor, interpolated through its current step. */
FtPos ft_stepper_pos(const FtStepper* s, uint32_t step_ms_total);

bool ft_world_moving(const FtWorld* w);

bool ft_world_entity_gone(const FtWorld* w, uint8_t index);
void ft_world_clear_entity(FtWorld* w, uint8_t index);

/* Index of a living foe sharing the player's tile, else -1. */
int ft_world_foe_contact(const FtWorld* w);

/* Index of a living foe on the tile the player faces, else -1. */
int ft_world_foe_ahead(const FtWorld* w);

/* The exit under the player, or NULL. */
const FtExit* ft_world_exit_under(const FtWorld* w);

bool ft_world_terminal_near(const FtWorld* w);

#endif /* FT_WORLD_H */
