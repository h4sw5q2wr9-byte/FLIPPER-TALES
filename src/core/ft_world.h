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

    /* Wren. Her own kind because she is not a quest giver: talking to her is
     * the middle of somebody else's quest, and she leaves with you afterwards.
     *
     * `roster` says which Wren: FT_WREN_CAVE is where she is hiding until you
     * bring her home, FT_WREN_HOME is her back in Weldhome afterwards. Only
     * one of them is ever there (ft_world_wren_present). The first version
     * had only the cave one, and she stayed there for ever — go back down
     * after bringing her home and she was hiding in the cave again, telling
     * you to go away. */
    FT_ENT_WREN,

    /* Something growing. `roster` carries the item it bears, and the entity
     * sits on the trunk; the crown is the 3x2 of leaf tiles above it.
     *
     * You cannot see whether it is bearing. Stand under it and shake it: each
     * visit rolls whether anything is up there (see ft_world_bearing), so a
     * tree is a bit of luck rather than a pickup you can read from across the
     * room. The fruit used to be drawn hanging in the leaves, and at 8x8 a
     * round dark thing with a highlight in it is an eye. */
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

#define FT_WREN_CAVE 0u
#define FT_WREN_HOME 1u

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

    /* A way that is not there at all until somebody shows it to you.
     *
     * Zero for an ordinary exit. Otherwise one or more FT_REVEAL_* bits, and
     * until every one of them is set in FtWorld.revealed the exit does not
     * exist: no refusal, no art, nothing under your feet. The pit in the
     * Approach's long grass is one of these — you can stand on the tile it is
     * on and nothing happens, which is the point.
     *
     * Different from need_quest on purpose. A gated exit is a way you can
     * see and are refused; a hidden one is a way you do not know about. */
    uint8_t reveal;
} FtExit;

/* Things that have been shown to you, one bit each. Saved. */
#define FT_REVEAL_PIT 0x01u

/* Not a way anywhere: Echo, seen once across the gap in the Scrapline. It
 * lives in the same saved byte because it is the same kind of fact — a
 * thing that has been shown to you and must not be shown twice. */
#define FT_REVEAL_ECHO 0x02u

/* Where: the Scrapline's room, on the far edge of the gap where you cannot
 * follow. It notices you when it is on your screen, with room over its head
 * for what it says — never from off the edge of the view. */
#define FT_ECHO_ROOM FT_ROOM_SLICE_FIRST
#define FT_ECHO_TX   15
#define FT_ECHO_TY   4

typedef struct {
    const FtMap*    map;
    const FtExit*   exits;
    uint8_t         exit_count;
    const FtEntity* ents;
    uint8_t         ent_count;

    /* The area this room belongs to, whose name comes up when you walk in
     * from a different one. NULL for a path, which has no name of its own. */
    const char*     area;
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

/* Chapter 1's three rooms by name, because Hale walks between two of them
 * and the rules for where he goes are written in terms of rooms. */
#define FT_ROOM_APPROACH  FT_ROOM_CH1_FIRST
#define FT_ROOM_WELDHOME  (FT_ROOM_CH1_FIRST + 1)
#define FT_ROOM_HOLLOW    (FT_ROOM_CH1_FIRST + 2)
#define FT_ROOM_DEAD_LETTERS (FT_ROOM_CH1_FIRST + 3)

const FtRoom*   ft_room(uint8_t index);
uint8_t         ft_room_count(void);
const FtRoster* ft_roster(uint8_t index);

/* How many there are. The balance simulator walks all of them. */
#define FT_ROSTER_COUNT 17
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

/* Hale, the other guard on Weldhome's gate.
 *
 * He is the only person in the game who walks between rooms, so he lives in
 * the world rather than in a room's entity table, like the escort does. Five
 * things he can be doing, and the player never has to know which — every
 * change is triggered by where the player goes, not by anything they press:
 *
 *   POST    standing on the gate. Talk to him.
 *   LEAD    walking you to the pit: out of Weldhome, across the Approach and
 *           into the long grass. He waits whenever you fall behind.
 *   WAIT    standing beside the pit he found, until you come back up.
 *   FOLLOW  walking behind you, once you head off from the pit. Behind Wren,
 *           if she is with you.
 *   HOME    back in Weldhome, walking from the gate to his post. */
typedef enum {
    FT_HALE_POST = 0,
    FT_HALE_LEAD,
    FT_HALE_WAIT,
    FT_HALE_FOLLOW,
    FT_HALE_HOME
} FtHalePhase;

typedef struct {
    uint8_t   room;

    /* The last area whose name came up, and whether this room's is up now. */
    const char* area_named;
    bool        banner;

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

    /* What Wren called you, or FT_NAME_NONE before she has. */
    uint8_t name;

    /* FT_REVEAL_* bits: hidden ways somebody has shown you. */
    uint8_t revealed;

    /* Set for one update when something is revealed, so the app can play it
     * a sound without watching the bits itself. */
    bool revealed_now;

    /* The same, for the moment Echo sees you. */
    bool echo_now;

    /* The tree being shaken, and for how much longer. Not saved. */
    uint8_t  shake_tree;
    uint16_t shake_ms;

    /* Hale. See FtHalePhase. */
    uint8_t   hale;      /* FtHalePhase */
    uint8_t   hale_room;
    FtStepper hale_mv;
    FtFacing  hale_facing;

    /* He is jogging this step, to catch up. Not saved: a step is a fifth of
     * a second, and a reload always lands him standing still. */
    bool hale_hurry;

    /* How many times he has stopped to wait for you on this walk, so he
     * does not say the same thing every time. */
    uint8_t hale_waits;
    bool    hale_waiting;

    /* Something said out loud: an FtBark, who said it, and for how much
     * longer it hangs there. None of it is saved — it is a remark. */
    uint8_t  bark;
    uint8_t  bark_who; /* FtBarkWho */
    uint16_t bark_ms;

    /* Wren's walk-home chatter: time since she last said something, and
     * which of her lines is next. */
    uint16_t chatter_ms;
    uint8_t  chatter_at;

    /* Where a terminal that has just said something is, and which of
     * Hush's lines it says next. */
    uint8_t bark_tx, bark_ty;
    uint8_t hush_at;
} FtWorld;

typedef enum {
    FT_BARK_NOBODY = 0,
    FT_BARK_BY_HALE,
    FT_BARK_BY_WREN,
    FT_BARK_BY_TERMINAL, /* at (bark_tx, bark_ty) */
    FT_BARK_BY_ECHO      /* at (bark_tx, bark_ty) too */
} FtBarkWho;

/* How long a remark hangs over somebody's head, and how long Wren leaves
 * between them. Long enough to read twice; not so often that it is noise. */
#define FT_BARK_MS    2000u
#define FT_CHATTER_MS 5000u

void ft_world_init(FtWorld* w);
void ft_world_enter(FtWorld* w, uint8_t room, uint8_t tx, uint8_t ty);

/* Advance everything by dt. dx/dy are the held direction, -1/0/1. A step in
 * progress runs to completion regardless of input. */
void ft_world_update(FtWorld* w, int8_t dx, int8_t dy, uint32_t dt_ms);

const FtMap* ft_world_map(const FtWorld* w);

/* The area name to show on entering this room, or NULL when there is none:
 * a path, or another room of the area you were already in. The renderer
 * shows it for FT_AREA_BANNER_MS after entering. */
const char* ft_world_banner(const FtWorld* w);

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

/* Same, for Wren, wherever she is. */
int ft_world_wren_ahead(const FtWorld* w);

/* Is this Wren entity actually standing there right now? The cave one until
 * she is home (and not while she is walking with you); the home one after. */
bool ft_world_wren_present(const FtWorld* w, uint8_t index);

/* A cache on the tile you face and have not emptied, else -1. Trees are not
 * picked by facing them any more — see ft_world_tree_near. */
int ft_world_pick_ahead(const FtWorld* w);

/* A tree you are standing under, or whose trunk you are facing, else -1.
 * Bearing or bare: you do not know which until you shake it. */
int ft_world_tree_near(const FtWorld* w);

/* Shake it. Returns what fell, or FT_ITEM_COUNT when nothing did (it is bare
 * this visit, or your pockets are full and it stays up there). Starts the
 * canopy wiggle either way, because you did shake it. */
#define FT_SHAKE_MS 360u
FtItemId ft_world_shake(FtWorld* w, uint8_t index);

/* Is this tile part of the crown of the tree being shaken, and if so which
 * way is it leaning this frame (-1 or 1)? 0 when it is not moving. */
int8_t ft_world_shake_offset(const FtWorld* w, int32_t tx, int32_t ty);

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

/* ---- Hale ---- */

/* Start him walking you to the pit. Does nothing once the pit is found, or
 * while he is already somewhere other than his post. */
void ft_world_hale_lead(FtWorld* w);

/* Is Hale in this room, and where? False when he is somewhere else. */
bool ft_world_hale_here(const FtWorld* w);

/* How long his current step takes. Shorter than a walking step while he is
 * catching up; the renderer needs it to draw him between tiles. */
uint32_t ft_world_hale_step_ms(const FtWorld* w);

/* Is he standing on the tile the player faces, still enough to talk to? He
 * is solid while he stands (at his post, or by the pit) and walks through
 * nobody while he is on the move. */
bool ft_world_hale_ahead(const FtWorld* w);

/* Where he stands at his post, and beside the pit. Exposed for the tests,
 * which walk him there. */
void ft_world_hale_post(uint8_t* tx, uint8_t* ty);
void ft_world_hale_pitside(uint8_t* tx, uint8_t* ty);

/* Is there a way down on this tile that has been shown to you? The renderer
 * draws a hole here instead of the grass it was hidden in. */
bool ft_world_pit_at(const FtWorld* w, int32_t tx, int32_t ty);

/* Start and stop somebody walking with you. */
/* Whether Echo is standing across the gap right now: in the Scrapline, once
 * Wren is home (Coll has told you about it by then), until it has said its
 * line and gone. Never again after that. */
bool ft_world_echo_here(const FtWorld* w);

/* True for the one update in which Wren should stop you and name you: she
 * is walking you home, you have no name yet, and you have just finished a
 * step with her out of the hole and out of the long grass behind you — in
 * the Approach, or Weldhome if you somehow got there first. */
bool ft_world_naming_due(const FtWorld* w);

void ft_world_escort_start(FtWorld* w);
void ft_world_escort_stop(FtWorld* w);

/* The exit under the player, or NULL. A hidden exit nobody has shown you is
 * not an exit yet, so it is NULL too. */
const FtExit* ft_world_exit_under(const FtWorld* w);

bool ft_world_terminal_near(const FtWorld* w);

/* The terminal you are using says one of Hush's lines, over itself. */
void ft_world_terminal_speaks(FtWorld* w);

#endif /* FT_WORLD_H */
