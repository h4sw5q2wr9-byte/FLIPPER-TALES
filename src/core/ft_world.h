/* The overworld: which room you are in, where you stand, what is in it.
 *
 * Pure C99 like the rest of core. The app feeds it input and dt and renders
 * what it holds; every rule about walking, doors, contact and defeat lives
 * here so it can be tested without a device. */
#ifndef FT_WORLD_H
#define FT_WORLD_H

#include "ft_data.h"
#include "ft_guide.h"
#include "ft_map.h"
#include "ft_progress.h"
#include "ft_signal.h"

#define FT_MAX_ROOM_ENTS 6

/* One bit per entity per room, for what has been beaten or taken.
 *
 * Sized with headroom on purpose: at 8 bytes the nine rooms below needed 54
 * of the 64 bits, and the eleventh room would have silently stopped being
 * recorded — a boss that quietly respawns, found much later and blamed on
 * anything but this. ft_world.c asserts the fit at compile time. */
#define FT_CLEARED_BYTES 16
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

/* How many there are. The balance simulator walks all of them. */
#define FT_ROSTER_COUNT 12
uint8_t ft_roster_count(void);

/* A tile-aligned actor mid-step. */
typedef struct {
    uint8_t  tx, ty;
    int8_t   dx, dy;   /* direction of the step in progress, 0 when idle */
    uint32_t step_ms;  /* elapsed within the current step */
} FtStepper;

/* One walker. A roster of three walks the room as three of these, each with
 * its own step, think clock and wander seed — drawing one leader with two
 * sprites pinned at fixed offsets made a group read as a single object being
 * dragged around, which is not what standing in front of three things looks
 * like. */
typedef struct {
    FtStepper mv;
    uint32_t  think_ms;

    /* Where this one was placed. It drifts around here rather than wandering
     * off, so a group keeps its shape and a room keeps its shape. */
    uint8_t home_tx, home_ty;

    /* Its own wander state, so walkers do not move in lockstep. */
    uint32_t seed;
} FtFoeWalker;

/* One encounter marker: the thing you fight, made of one or more walkers. */
typedef struct {
    bool    alive;

    /* Alerted foes head for the player. Alert is shared across the whole
     * room: one of them noticing you brings the rest. */
    bool    alert;

    uint8_t count; /* walkers, from the roster */
    FtFoeWalker w[FT_MAX_ENEMIES];
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
    uint8_t cleared[FT_CLEARED_BYTES];

    /* The terminal last saved at. Being downed returns you here, so the save
     * point and the respawn point cannot drift apart. */
    uint8_t save_room, save_tx, save_ty;

    /* Carried between battles, since an encounter starts from scratch. */
    FtStats         stats;
    FtSignalLibrary lib;

    /* What has been met. Carried with the run, saved with it, and reset by
     * a loss like everything else. */
    FtGuide         guide;
    FtLoadout       loadout;
} FtWorld;

void ft_world_init(FtWorld* w);
void ft_world_enter(FtWorld* w, uint8_t room, uint8_t tx, uint8_t ty);

/* Advance everything by dt. dx/dy are the held direction, -1/0/1. A step in
 * progress runs to completion regardless of input. */
void ft_world_update(FtWorld* w, int8_t dx, int8_t dy, uint32_t dt_ms);

const FtMap* ft_world_map(const FtWorld* w);

/* Pixel position of an actor's *tile*, interpolated through its current step.
 *
 * This used to subtract the difference between the old avatar's height and a
 * tile, so the caller could draw top-aligned. The renderers bottom-align
 * their own sprites now, so that offset was being applied twice and every
 * actor floated half a tile above the ground it was standing on. Core does
 * not know how tall anything is drawn. */
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
