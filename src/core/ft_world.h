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
#include "ft_item.h"
#include "ft_quest.h"
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
    FT_ENT_FOE,
    FT_ENT_NPC,  /* someone to talk to; `roster` carries their quest id */

    /* Wren, waiting at the end of the junction. Her own kind because she is
     * not a quest giver: talking to her is the middle of somebody else's
     * quest, and she leaves with you afterwards. */
    FT_ENT_WREN,

    /* Something growing. `roster` carries the item it bears.
     *
     * It is not always bearing: each visit rolls for it (see ft_world_bearing),
     * so walking past one is a look rather than a guaranteed apple. Picked
     * bare for the visit and back when you return, exactly like the foes —
     * which is what makes walking a cleared room again worth doing. */
    FT_ENT_TREE,

    /* Something somebody left. `roster` carries the item. Taken once and
     * gone for good, because a cache that refills is a vending machine. */
    FT_ENT_CACHE
} FtEntKind;

typedef struct {
    FtEntKind kind;
    uint8_t   tx, ty;
    uint8_t   roster; /* a foe's group, or an NPC's quest */
} FtEntity;

typedef struct {
    uint8_t tx, ty;
    uint8_t dest_room;
    uint8_t dest_tx, dest_ty;

    /* A way that is not always a way.
     *
     * `need_quest` is a quest id plus one, 0 for an exit that is simply
     * open. The exit works once that quest has reached `need_state`. This
     * covers both of Chapter 1's gates with one mechanism: the drop the
     * Courier has no reason to take (needs ACTIVE) and Weldhome's gate,
     * which Warden Coll holds shut (needs DONE). Neither needs a key, an
     * item or a tile of its own. */
    uint8_t need_quest;
    uint8_t need_state;
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

/* Which rooms are what.
 *
 * [0, 3]  the prologue
 * [4, 8]  one-room samples of the five chapters, each with its locked port
 * [9, 11] Chapter 1's Weldhome chain, which is a place rather than a sample
 *
 * Named because the tests assert different things about each band, and
 * "rooms 4 and up are area slices" stopped being true the moment Chapter 1
 * was appended after them. */
#define FT_ROOM_SLICE_FIRST 4
#define FT_ROOM_SLICE_LAST  8
#define FT_ROOM_CH1_FIRST   9

const FtRoom*   ft_room(uint8_t index);
uint8_t         ft_room_count(void);
const FtRoster* ft_roster(uint8_t index);

/* How many there are. The balance simulator walks all of them. */
#define FT_ROSTER_COUNT 13
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

    /* Counting down the notice beat. While this is running the group has
     * seen you and is not moving yet — see FT_FOE_NOTICE_MS. */
    uint16_t notice_ms;

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

    /* Set for one update when a foe's step lands on the player. Whoever
     * moved into whom decides who opens the fight. */
    bool ambushed;

    FtFoeState foes[FT_MAX_ROOM_ENTS];

    /* Which entities are gone, one bit per entity per room. */
    uint8_t cleared[FT_CLEARED_BYTES];

    /* The terminal last saved at. Being downed returns you here, so the save
     * point and the respawn point cannot drift apart. */
    uint8_t save_room, save_tx, save_ty;

    /* Carried between battles, since an encounter starts from scratch. */
    FtStats         stats;

    /* What has been met. Carried with the run, saved with it, and reset by
     * a loss like everything else. */
    FtGuide         guide;
    FtLoadout       loadout;

    /* What has been asked of you, and how far through it you are. */
    FtQuests        quests;

    /* What you are carrying. Capped, so topping up is a decision. */
    FtPockets       pockets;

    /* How many rooms have been entered this run. Rolls the trees: a tree
     * that always has an apple on it is a button, not a tree. */
    uint16_t        visits;

    /* Somebody walking with you.
     *
     * She steps into the tile you just left, every time you leave one, which
     * is all a conga line has ever been. Kept in the world rather than as an
     * entity because she is not part of any room — she is part of you until
     * she is dropped off. */
    bool      escort;
    FtStepper escort_mv;
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

/* Has this group just spotted the player and not started moving yet? The
 * renderer puts a mark over it for exactly this long. */
bool ft_world_foe_noticing(const FtWorld* w, uint8_t index);

/* Index of the NPC on the tile the player faces, else -1. An NPC is solid, so
 * this is what you get by walking into one. */
int ft_world_npc_ahead(const FtWorld* w);

/* Same, for the kid waiting at the end of the junction. */
int ft_world_wren_ahead(const FtWorld* w);

/* A tree or a cache on the tile you face and have not emptied, else -1. */
int ft_world_pick_ahead(const FtWorld* w);

/* Is this tree bearing anything this visit? Always true for a cache, which
 * is a thing somebody left rather than a thing that grows. False once it has
 * been picked. The renderer asks this too, so a bare tree looks bare. */
bool ft_world_bearing(const FtWorld* w, uint8_t index);

/* Take what is on it. Returns the item, or FT_ITEM_COUNT when there was
 * nothing there or nowhere to put it. */
FtItemId ft_world_pick(FtWorld* w, uint8_t index);

/* Is this exit usable yet? An exit the quests have not opened refuses, and
 * `ft_world_exit_refusal` says what the Courier thinks about that. */
bool        ft_world_exit_open(const FtWorld* w, const FtExit* x);
const char* ft_world_exit_refusal(const FtExit* x);

/* Start and stop somebody walking with you. */
void ft_world_escort_start(FtWorld* w);
void ft_world_escort_stop(FtWorld* w);

/* The exit under the player, or NULL. */
const FtExit* ft_world_exit_under(const FtWorld* w);

bool ft_world_terminal_near(const FtWorld* w);

#endif /* FT_WORLD_H */
