#include "ft_world.h"

#include "../app/ft_maps.h"

/* ---- Content ---------------------------------------------------------- */

/* One visible foe means a whole group in battle, which is what makes the
 * broadcast-versus-contact choice matter out here too. */
static const FtRoster FT_ROSTERS[] = {
    /* [0-3] The prologue, one lesson at a time. */
    {1, {FT_ENEMY_PARCEL_RUNNER, 0, 0}},
    {2, {FT_ENEMY_PARCEL_RUNNER, FT_ENEMY_PARCEL_RUNNER, 0}},
    {2, {FT_ENEMY_LAMPLIGHTER, FT_ENEMY_PARCEL_RUNNER, 0}},
    {3, {FT_ENEMY_PARCEL_RUNNER, FT_ENEMY_LAMPLIGHTER, FT_ENEMY_CURFEW_LOCK}},

    /* [4-8] One per area. Each pairs its own enemy with something from the
     * prologue, so a fight is the new idea plus a thing you already know how
     * to handle rather than two puzzles at once. */
    {2, {FT_ENEMY_SWEEPER, FT_ENEMY_PARCEL_RUNNER, 0}},           /* Scrapline */
    {3, {FT_ENEMY_SWEEPER, FT_ENEMY_SWEEPER,
         FT_ENEMY_PARCEL_RUNNER}},                                      /* Scrapline, heavier */
    {2, {FT_ENEMY_CHILLER, FT_ENEMY_PARCEL_RUNNER, 0}},              /* Cold Storage */
    {2, {FT_ENEMY_TICKET_DRONE, FT_ENEMY_CURFEW_LOCK, 0}},               /* Turnstile */
    {2, {FT_ENEMY_LOUDHAILER, FT_ENEMY_LAMPLIGHTER, 0}},              /* Signal Hill */
    /* The Shusher is the whole fight: ENCRYPTED, a jammer, and both its
     * attacks unguardable-for-capture. Pairing it with a FAST Crawler as
     * well put the Deadzone at 28% for an average player — two stars, no
     * room to learn either. A plain Packet gives it a partner without
     * giving it a second mechanic. */
    {2, {FT_ENEMY_SHUSHER, FT_ENEMY_PARCEL_RUNNER, 0}},              /* Deadzone */

    /* [10-11] The shape-changers. Order matters for both: the wall stands in
     * slot 0 so it is literally in front, and the sleeper sits in the last
     * slot so clearing the row is what wakes it. */
    {3, {FT_ENEMY_QUEUE_BARRIER, FT_ENEMY_LAMPLIGHTER, FT_ENEMY_PARCEL_RUNNER}},
    {3, {FT_ENEMY_PARCEL_RUNNER, FT_ENEMY_SWEEPER, FT_ENEMY_NIGHT_SHIFT}},

    /* [12] The Hollow. Chapter 1's set-piece, and the first fight the
     * game asks you to win for somebody else: a wall in front so you cannot
     * reach past it, something fast behind it, and something ordinary.
     *
     * Measured at 46/83/97 across the three skill levels, which puts it level
     * with the hardest roster in the game and still lets a careful player
     * through. Pairing the wall with two AIRBORNE foes instead read 19/52/78:
     * the wall blocks the broadcast and the flyers refuse contact, so the
     * fight had two locks and no key. A story beat nobody can clear is not a
     * story beat. */
    {3, {FT_ENEMY_QUEUE_BARRIER, FT_ENEMY_SWEEPER, FT_ENEMY_PARCEL_RUNNER}},

    /* [13-16] The rest of the Hollow, built up to [12] rather than straight
     * into it. Prologue machines, because the Hollow is where they dump what
     * they "return" — Parcel Runners and the Lamplighters that light the way
     * down — in groups that ask more of you each time:
     *
     *   [13] two Runners: the warm-up at the foot of the ladder
     *   [14] a Lamplighter and a Runner: one you can reach, one you cannot
     *   [15] two Lamplighters at the ladder down: contact is useless, so
     *        this is the fight that asks whether you have MP to spend
     *   [16] two Runners and a Night Shift asleep among the parcels — clear
     *        the Runners and it clocks in, the hardest thing in the caves
     *        short of the guards. */
    {2, {FT_ENEMY_PARCEL_RUNNER, FT_ENEMY_PARCEL_RUNNER, 0}},
    {2, {FT_ENEMY_LAMPLIGHTER, FT_ENEMY_PARCEL_RUNNER, 0}},
    {2, {FT_ENEMY_LAMPLIGHTER, FT_ENEMY_LAMPLIGHTER, 0}},
    {3, {FT_ENEMY_PARCEL_RUNNER, FT_ENEMY_PARCEL_RUNNER, FT_ENEMY_NIGHT_SHIFT}},

    /* [17] Echo, alone, on the spit in front of the Scrapline's relay. */
    {1, {FT_ENEMY_ECHO, 0, 0}},
};
#define ROSTER_COUNT (sizeof(FT_ROSTERS) / sizeof(FT_ROSTERS[0]))

/* Kept in step with the table by the compiler rather than by memory. */
typedef char ft_roster_count_matches[(ROSTER_COUNT == FT_ROSTER_COUNT) ? 1 : -1];

/* [1] Wake: a terminal, the way out, and the one person who asks you for
 * anything. No foe — the first room teaches walking, saving and talking,
 * nothing else. */
static const FtExit CB1_EXITS[] = {
    {17, 4, 1, 1, 2, 0, 0, 0},
};
static const FtEntity CB1_ENTS[] = {
    /* Two tiles from where you wake up, off the line to the door, so you
     * meet them by choice rather than by walking into them. */
    {FT_ENT_NPC, 6, 2, FT_QUEST_CLEAN_RUN},

    /* The first tree, in the first room, in sight of the terminal that
     * teaches saving. Picking it is how you learn picking exists.
     *
     * The entity sits ON the trunk tile, which is solid — so you face it and
     * press OK, exactly like talking to somebody. The tree itself is map
     * tiles now (a trunk you bump into, a canopy you walk behind); this is
     * only the fruit hanging in it. */
    {FT_ENT_TREE, 9, 7, FT_ITEM_APPLE},
};

/* [2] Boot Corridor: the first encounter, placed far enough right that it is
 * seen well before it is reached. */
static const FtExit CB2_EXITS[] = {
    {0, 2, 0, 16, 4, 0, 0, 0},
    {19, 4, 2, 1, 2, 0, 0, 0},
};
static const FtEntity CB2_ENTS[] = {
    /* In the middle of the corridor, as far from one door as from the other:
     * the Keeper's favour walks this room both ways, and at (11,4) it stood
     * three tiles nearer the east door, so coming back you arrived almost
     * on top of it. test_clean_run_both_ways holds the two directions level. */
    {FT_ENT_FOE, 9, 4, 0},
};

/* [3] The Drop: upper shelf, ladder down, terminal on the lower floor. */
static const FtExit CB3_EXITS[] = {
    {0, 2, 1, 18, 4, 0, 0, 0},
    {17, 10, 3, 1, 2, 0, 0, 0},
};
static const FtEntity CB3_ENTS[] = {
    /* The shelf's foe roams the far end of the shelf, away from the ladder.
     * At (9,2) it wandered to the top of the ladder, which is fine going
     * out — you see it from the door and wait — and a trap coming back,
     * where you climb up blind into it. A careful player was caught there
     * on the way home 286 times in 300. */
    {FT_ENT_FOE, 14, 2, 1},
    {FT_ENT_FOE, 6, 11, 2},
    {FT_ENT_TREE, 8, 9, FT_ITEM_APPLE},
};

/* [4] Cold Gate: a group standing in the exit, the way an area ends. */
static const FtExit CB4_EXITS[] = {
    {0, 2, 2, 16, 10, 0, 0, 0},
    {17, 8, 9, 1, 5, 0, 0, 0}, /* out of the prologue, into Chapter 1 */
};
static const FtEntity CB4_ENTS[] = {
    {FT_ENT_FOE, 13, 8, 3},

    /* Left on the floor of the last prologue room, before the area ends:
     * somebody was here before the Silence and did not come back for it.
     * Not inside the locked chamber, which is sealed by design — the
     * reachability test caught that one. */
    {FT_ENT_CACHE, 3, 8, FT_ITEM_RATION},
};

/* ---- Chapter 1: Weldhome ----------------------------------------------
 *
 * See STORY.md 6. The shape: a fork you walk past, a gate that will not
 * open, and the fork again — now with a reason.
 *
 * Nothing here is locked with a key. The drop south refuses because the
 * Courier has no reason to take it, and Weldhome's gate refuses because
 * Warden Coll is holding it. Both are the same field in FtExit. */

/* [10] The Approach. Three ways out, and you can only see two of them: the
 * third is a pit in the long grass that Hale has to show you. */
static const FtExit AP1_EXITS[] = {
    {0, 5, 3, 16, 8, 0, 0, 0},
    {23, 5, 10, 1, 5, 0, 0, 0},

    /* The pit, in the long grass. Not there at all until Hale has walked you
     * to it — not refused, not drawn, not anything. */
    {11, 8, 11, 3, 2, 0, 0, FT_REVEAL_PIT},
};
static const FtEntity AP1_ENTS[] = {
    /* Both a step off the path, at either end of it. Fruit is worth leaving
     * the road for and never worth a detour you cannot see the end of. */
    {FT_ENT_TREE, 3, 4, FT_ITEM_APPLE},
    {FT_ENT_TREE, 19, 9, FT_ITEM_APPLE},
};

/* [11] Weldhome Gate. A real gate tile, so a way that is shut against you
 * does not look like an ordinary doorway you have not tried yet. */
static const FtExit WH1_EXITS[] = {
    {0, 5, 9, 22, 5, 0, 0, 0},
    {23, 5, 4, 1, 2, (uint8_t)FT_QUEST_WREN + 1u, (uint8_t)FT_QUEST_DONE, 0},
};
static const FtEntity WH1_ENTS[] = {
    /* Beside the gate, not in front of it.
     *
     * She used to stand on (18,4), which is the only tile that touches the
     * gate — so once the quest opened it, she was still bodily in the way
     * and the reward for the whole chapter was a wall with a person on it.
     * NPCs are solid; a guard has to guard from the side. */
    {FT_ENT_NPC, 22, 4, (uint8_t)FT_QUEST_WREN},

    /* A village grows things and keeps a cell spare. This is the stock-up
     * before the hardest fight in the game, two rooms away. */
    {FT_ENT_TREE, 4, 8, FT_ITEM_APPLE},
    {FT_ENT_TREE, 20, 8, FT_ITEM_APPLE},
    {FT_ENT_CACHE, 13, 8, FT_ITEM_CELL},

    /* Wren, home, on the porch of the house by the gate — once she is. */
    {FT_ENT_WREN, 14, 4, FT_WREN_HOME},
};

/* [12] The Hollow, upper. Three fights winding east to the ladder down, and
 * a pack somebody dropped in the side pocket. */
static const FtExit EJ1_EXITS[] = {
    /* First: the ladder back up to the hole in the roof. You come out beside
     * the pit, on the other side of it from wherever Hale is standing. */
    {3, 1, 9, 12, 8, 0, 0, 0},

    /* And the ladder down, to Dead Letters. */
    {26, 12, FT_ROOM_DEAD_LETTERS, 3, 2, 0, 0, 0},
};
static const FtEntity EJ1_ENTS[] = {
    {FT_ENT_FOE, 8, 4, 13},   /* at the foot of the ladder */
    {FT_ENT_FOE, 18, 4, 14},  /* half way along */
    {FT_ENT_FOE, 23, 10, 15}, /* at the ladder down */

    /* In the side pocket off the first cavern. Whether you spend it now or
     * save it for the guards is the first real pocket decision the game
     * asks. */
    {FT_ENT_CACHE, 7, 12, FT_ITEM_RATION},
};

/* [13] Dead Letters, the lower Hollow. A terminal at the foot of the ladder,
 * something asleep in the parcel heaps, the only passage east guarded the way
 * things guard — a wall in front — and Wren at the far end. */
static const FtExit EJ2_EXITS[] = {
    {3, 1, FT_ROOM_HOLLOW, 25, 12, 0, 0, 0},
};
static const FtEntity EJ2_ENTS[] = {
    {FT_ENT_FOE, 8, 6, 16},  /* the Night Shift and its two Runners */
    {FT_ENT_FOE, 16, 8, 12}, /* in the passage: there is no way round */

    /* At the far end of the east cavern, as far from the ladder as the caves
     * go. */
    {FT_ENT_WREN, 26, 3, FT_WREN_CAVE},

    /* Behind the guards: a reward for getting there, and the walk home. */
    {FT_ENT_CACHE, 22, 12, FT_ITEM_CELL},
};

/* ---- Chapter 1, part 2: the Scrapline ------------------------------------
 *
 * STORY.md §6. A town, a room of broken spans, and the relay. */

/* [13] The Scrapline. Ma Rivet stands in front of her shack, beside the
 * road, where nobody can walk through town without being shouted at. */
static const FtExit SC1_EXITS[] = {
    {0, 5, FT_ECHO_ROOM, 20, 8, 0, 0, 0},
    {25, 5, FT_ROOM_SPANS, 1, 4, 0, 0, 0},
};
static const FtEntity SC1_ENTS[] = {
    {FT_ENT_NPC, 10, 4, FT_QUEST_RIVET},
    {FT_ENT_CACHE, 23, 8, FT_ITEM_RATION},
};

/* [14] The Fallen Spans. A Sweeper crew either side of the gap: the one on
 * this side is between you and the edge, the one on the far side is what
 * the bridge lets you meet. */
static const FtExit SC2_EXITS[] = {
    {0, 4, FT_ROOM_SCRAPLINE, 24, 5, 0, 0, 0},
    {25, 4, FT_ROOM_RELAY, 1, 4, 0, 0, 0},
};
static const FtEntity SC2_ENTS[] = {
    {FT_ENT_FOE, 5, 2, 4},
    {FT_ENT_FOE, 19, 3, 5},
    {FT_ENT_CACHE, 22, 7, FT_ITEM_CELL},
};

/* [15] The Relay. Echo stands on the spit, the only way to the mast. */
static const FtExit SC3_EXITS[] = {
    {0, 4, FT_ROOM_SPANS, 24, 4, 0, 0, 0},
};
static const FtEntity SC3_ENTS[] = {
    {FT_ENT_FOE, 19, 4, 17},
};


/* ---- Concept slices ---------------------------------------------------
 *
 * One room from each of the five chapters, chained on past the prologue so
 * they can be walked rather than read about. They are a *sample* of each
 * area, not the area: enough to establish what the place looks like, what
 * is in it, and what the chapter's module would be for.
 *
 * Every one carries a locked port, because the progression is the point —
 * the gate you cannot open yet is the thing that makes each area a promise
 * rather than a backdrop. See DESIGN.md "The world". */

/* [5] The Scrapline. Wreckage, and a span of missing floor with a terminal
 * stranded on the far side: Infrared is line-of-sight, so what the module
 * buys you here is reaching across a gap you cannot walk. */
static const FtExit SL1_EXITS[] = {
    {0, 2, 3, 16, 8, 0, 0, 0},

    /* On to the Scrapline itself. This used to lead to the next sample
     * room; the samples after it are only reachable from the debug menu
     * now, until their chapters replace them. */
    {21, 8, FT_ROOM_SCRAPLINE, 1, 5, 0, 0, 0},
};
static const FtEntity SL1_ENTS[] = {
    {FT_ENT_FOE, 8, 6, 4},
    {FT_ENT_FOE, 11, 3, 5},
};

/* [6] Cold Storage. A sealed cell in the middle of a frosted floor, with no
 * visible way in: RFID reads through walls, so the module finds the door
 * that was never drawn. */
static const FtExit CS1_EXITS[] = {
    {0, 2, 4, 20, 8, 0, 0, 0},
    {21, 8, 6, 1, 2, 0, 0, 0},
};
static const FtEntity CS1_ENTS[] = {
    {FT_ENT_FOE, 17, 2, 6},
    {FT_ENT_FOE, 4, 7, 6},
};

/* [7] The Turnstile. Ranks of locked ports across the only route through.
 * This is the wall the chapter is named after, and you can walk up to it
 * long before the iButton exists. */
static const FtExit TS1_EXITS[] = {
    {0, 2, 5, 20, 8, 0, 0, 0},
    {21, 8, 7, 1, 2, 0, 0, 0},
};
static const FtEntity TS1_ENTS[] = {
    {FT_ENT_FOE, 15, 4, 7},
    {FT_ENT_FOE, 4, 2, 10}, /* the wall, in a room about things in the way */
};

/* [8] Signal Hill. Pylons and dead cable runs on a terrace, with the ladder
 * up sitting behind a run that carries nothing: GPIO powers what is already
 * there. */
static const FtExit SH1_EXITS[] = {
    {0, 2, 6, 20, 8, 0, 0, 0},
    {21, 8, 8, 1, 2, 0, 0, 0},
};
static const FtEntity SH1_ENTS[] = {
    {FT_ENT_FOE, 15, 1, 8},
    {FT_ENT_FOE, 5, 6, 8},
};

/* [9] The Deadzone. Interference over everything and nothing that reads
 * straight. BLE pairs with devices and moves them, so the crates in the way
 * are the way through. The chain loops back to the start from here: past
 * this is Chapter 1, which does not exist yet. */
static const FtExit DZ1_EXITS[] = {
    {0, 2, 7, 20, 8, 0, 0, 0},
    {21, 8, 0, 3, 4, 0, 0, 0},
};
static const FtEntity DZ1_ENTS[] = {
    {FT_ENT_FOE, 9, 4, 9},
    {FT_ENT_FOE, 17, 7, 11}, /* the sleeper, at the far end */
};

/* The last field is the AREA the room belongs to — the name that comes up
 * when you walk into it from somewhere else. Rooms in one area share it, so
 * walking from one Cold Boot room to the next says nothing, and a path
 * between places has none at all: "not every small map needs a name, it is
 * just a path." */
static const char* const AREA_COLD_BOOT = "Cold Boot";
static const char* const AREA_WELDHOME = "Weldhome";
static const char* const AREA_HOLLOW = "The Hollow";
static const char* const AREA_SCRAPLINE = "The Scrapline";

static const FtRoom FT_ROOMS[] = {
    {&FT_MAP_CB1, CB1_EXITS, 1, CB1_ENTS, 2, AREA_COLD_BOOT, 0},
    {&FT_MAP_CB2, CB2_EXITS, 2, CB2_ENTS, 1, AREA_COLD_BOOT, 0},
    {&FT_MAP_CB3, CB3_EXITS, 2, CB3_ENTS, 3, AREA_COLD_BOOT, 0},
    {&FT_MAP_CB4, CB4_EXITS, 2, CB4_ENTS, 2, AREA_COLD_BOOT, 0},
    {&FT_MAP_SL1, SL1_EXITS, 2, SL1_ENTS, 2, AREA_SCRAPLINE, 0},
    {&FT_MAP_CS1, CS1_EXITS, 2, CS1_ENTS, 2, "Cold Storage", 0},
    {&FT_MAP_TS1, TS1_EXITS, 2, TS1_ENTS, 2, "The Turnstile", 0},
    {&FT_MAP_SH1, SH1_EXITS, 2, SH1_ENTS, 2, "Signal Hill", 0},
    {&FT_MAP_DZ1, DZ1_EXITS, 2, DZ1_ENTS, 2, "The Deadzone", 0},
    {&FT_MAP_AP1, AP1_EXITS, 3, AP1_ENTS, 2, NULL, 0}, /* a path */
    {&FT_MAP_WH1, WH1_EXITS, 2, WH1_ENTS, 5, AREA_WELDHOME, 0},
    {&FT_MAP_EJ1, EJ1_EXITS, 2, EJ1_ENTS, 4, AREA_HOLLOW, 0},
    {&FT_MAP_EJ2, EJ2_EXITS, 1, EJ2_ENTS, 4, AREA_HOLLOW, 0},
    {&FT_MAP_SC1, SC1_EXITS, 2, SC1_ENTS, 2, AREA_SCRAPLINE, 0},
    {&FT_MAP_SC2, SC2_EXITS, 2, SC2_ENTS, 3, AREA_SCRAPLINE, FT_REVEAL_BRIDGE_SPANS},
    {&FT_MAP_SC3, SC3_EXITS, 1, SC3_ENTS, 1, AREA_SCRAPLINE, FT_REVEAL_BRIDGE_RELAY},
};
#define ROOM_COUNT (sizeof(FT_ROOMS) / sizeof(FT_ROOMS[0]))

/* Every room's every entity must have a bit to live in. Overflowing this is
 * silent at runtime — clear_entity simply returns and the thing comes back —
 * so it fails the build instead. */
typedef char ft_cleared_bits_fit
    [(FT_CLEARED_BYTES * 8u >= ROOM_COUNT * FT_MAX_ROOM_ENTS) ? 1 : -1];

const FtRoom* ft_room(uint8_t index) {
    return &FT_ROOMS[index < ROOM_COUNT ? index : 0];
}

uint8_t ft_room_count(void) {
    return (uint8_t)ROOM_COUNT;
}

uint8_t ft_roster_count(void) {
    return (uint8_t)ROSTER_COUNT;
}

const FtRoster* ft_roster(uint8_t index) {
    return &FT_ROSTERS[index < ROSTER_COUNT ? index : 0];
}

/* ---- Stepping --------------------------------------------------------- */

FtPos ft_stepper_pos(const FtStepper* s, uint32_t step_ms_total) {
    FtPos p = {(int32_t)s->tx * FT_TILE_PX, (int32_t)s->ty * FT_TILE_PX};

    if(s->dx || s->dy) {
        /* Interpolate across the step so movement reads as walking rather
         * than snapping tile to tile. */
        const int32_t travelled =
            ((int32_t)s->step_ms * FT_TILE_PX) / (int32_t)step_ms_total;
        p.x += s->dx * travelled;
        p.y += s->dy * travelled;
    }

    return p;
}

static bool step_target_free(const FtMap* m, uint8_t tx, uint8_t ty, int8_t dx, int8_t dy) {
    return !ft_tile_solid(ft_map_tile(m, (int32_t)tx + dx, (int32_t)ty + dy));
}

/* The player's own rule: the same as everybody's, except that a drawbridge
 * that has come down is floor. Only for the player — a foe on the far side
 * of a gap stays on the far side, bridge or no bridge. */
static bool bridge_down_in(const FtWorld* w) {
    const uint8_t bit = ft_room(w->room)->bridge;
    return bit != 0u && (w->revealed & bit) == bit;
}

static bool player_step_free(const FtWorld* w, const FtMap* m, int8_t dx, int8_t dy) {
    const FtTile t = ft_map_tile(m, (int32_t)w->mv.tx + dx, (int32_t)w->mv.ty + dy);
    if(t == FT_TILE_BRIDGE) return bridge_down_in(w);
    return !ft_tile_solid(t);
}

/* Advance a step, returning true on the tick it completes. */
static bool step_advance(FtStepper* s, uint32_t dt_ms, uint32_t total_ms) {
    if(!s->dx && !s->dy) return false;

    s->step_ms += dt_ms;
    if(s->step_ms < total_ms) return false;

    s->tx = (uint8_t)((int32_t)s->tx + s->dx);
    s->ty = (uint8_t)((int32_t)s->ty + s->dy);
    s->dx = 0;
    s->dy = 0;
    s->step_ms = 0;

    return true;
}

/* ---- State ------------------------------------------------------------ */

const FtMap* ft_world_map(const FtWorld* w) {
    return ft_room(w->room)->map;
}

bool ft_world_moving(const FtWorld* w) {
    return w->mv.dx != 0 || w->mv.dy != 0;
}

/* One bit per entity per room. */
static uint16_t cleared_bit(uint8_t room, uint8_t index) {
    return (uint16_t)((room * FT_MAX_ROOM_ENTS) + index);
}

bool ft_world_entity_gone(const FtWorld* w, uint8_t index) {
    const uint16_t bit = cleared_bit(w->room, index);
    if(bit >= sizeof(w->cleared) * 8u) return false;
    return (w->cleared[bit / 8u] & (1u << (bit % 8u))) != 0u;
}

void ft_world_clear_entity(FtWorld* w, uint8_t index) {
    const uint16_t bit = cleared_bit(w->room, index);
    if(bit >= sizeof(w->cleared) * 8u) return;

    w->cleared[bit / 8u] |= (uint8_t)(1u << (bit % 8u));
    if(index < FT_MAX_ROOM_ENTS) w->foes[index].alive = false;
}

/* Where a roster's walkers stand relative to their marker, in preference
 * order. Loose, not a formation: they wander off it immediately. The list is
 * long because a marker can stand in a corridor — Cold Gate's group blocks a
 * two-tile-wide exit — and a blocked spot that fell back to the marker put
 * the whole group on one tile, which is the welded-together look again. */
static const int8_t SPREAD[][2] = {
    {0, 0},  {-1, 1}, {1, 1},  {-1, 0}, {1, 0},
    {0, 1},  {0, -1}, {-1, -1}, {1, -1}, {-2, 0}, {2, 0},
};
#define SPREAD_COUNT (sizeof(SPREAD) / sizeof(SPREAD[0]))

static void hale_on_enter(FtWorld* w, uint8_t from, uint8_t to, uint8_t tx, uint8_t ty);

void ft_world_enter(FtWorld* w, uint8_t room, uint8_t tx, uint8_t ty) {
    const uint8_t from = w->room;
    w->room = (room < ft_room_count()) ? room : 0u;
    w->visits++;

    hale_on_enter(w, from, w->room, tx, ty);

    /* Walking into a room repopulates it.
     *
     * Beaten foes used to stay beaten forever, so a cleared corridor was a
     * cleared corridor and backtracking was free. Now the room is as
     * dangerous on the way back as it was on the way in, which is what stops
     * "go round again" being the answer to everything. The bitfield stays —
     * it is how anything *permanent* will be remembered — but foes are not
     * permanent. */
    const FtRoom* fresh = ft_room(w->room);
    for(uint8_t i = 0; i < fresh->ent_count && i < FT_MAX_ROOM_ENTS; i++) {
        /* Foes and trees both come back; a cache does not. Something that
         * grows is a reason to walk a cleared room again, and something
         * somebody left is a reason to have gone there once. */
        const FtEntKind k = fresh->ents[i].kind;
        if(k != FT_ENT_FOE && k != FT_ENT_TREE) continue;

        /* A boss is the one foe that is permanent: beaten once is beaten.
         * Echo standing back on the spit every time you came to the relay
         * would undo the scene where it got away. */
        if(k == FT_ENT_FOE) {
            const FtRoster* ro = ft_roster(fresh->ents[i].roster);
            if(ro->count > 0u && (FT_ENEMIES[ro->foes[0]].attrs & FT_ATTR_BOSS)) continue;
        }

        const uint16_t bit = cleared_bit(w->room, i);
        if(bit < sizeof(w->cleared) * 8u) {
            w->cleared[bit / 8u] &= (uint8_t)~(1u << (bit % 8u));
        }
    }

    w->mv.tx = tx;
    w->mv.ty = ty;
    w->mv.dx = 0;
    w->mv.dy = 0;
    w->mv.step_ms = 0;

    w->facing = FT_FACE_DOWN;
    w->walk_ms = 0;
    w->area_ms = 0;

    /* The name comes up when you walk into an area you were not already in.
     * A path has no name and leaves the last one standing, so walking out of
     * Cold Boot along the Approach and into Weldhome names Weldhome, and
     * walking back names Cold Boot again. */
    {
        const char* area = ft_room(w->room)->area;
        w->banner = (area != NULL && area != w->area_named);
        if(area != NULL) w->area_named = area;
    }
    w->arrived = false;
    w->ambushed = false;

    /* Arriving somewhere is the only thing a quest watches for on its own;
     * everything else it hears about from the app. */
    ft_quest_enter_room(&w->quests, w->room);

    /* She comes through the door with you, standing where you are until you
     * take your first step. */
    if(w->escort) {
        w->escort_mv.tx = tx;
        w->escort_mv.ty = ty;
        w->escort_mv.dx = 0;
        w->escort_mv.dy = 0;
        w->escort_mv.step_ms = 0;
    }

    /* Foes start where the room says, and are alive unless already beaten. */
    const FtRoom* r = ft_room(w->room);
    for(uint8_t i = 0; i < FT_MAX_ROOM_ENTS; i++) {
        FtFoeState* f = &w->foes[i];

        f->alive = false;
        f->alert = false;
        f->notice_ms = 0;
        f->stun_ms = 0;
        f->count = 0;

        if(i >= r->ent_count || r->ents[i].kind != FT_ENT_FOE) continue;

        const FtRoster* roster = ft_roster(r->ents[i].roster);
        f->alive = !ft_world_entity_gone(w, i);
        f->count = roster->count ? roster->count : 1u;
        if(f->count > FT_MAX_ENEMIES) f->count = FT_MAX_ENEMIES;

        for(uint8_t m = 0; m < f->count; m++) {
            FtFoeWalker* k = &f->w[m];

            /* Spread the group around the marker, taking the first open spot
             * nobody in this group has claimed, so three of them never start
             * life stacked on one tile. */
            k->home_tx = r->ents[i].tx;
            k->home_ty = r->ents[i].ty;

            for(size_t sp = 0; sp < SPREAD_COUNT; sp++) {
                const int32_t cx = (int32_t)r->ents[i].tx + SPREAD[sp][0];
                const int32_t cy = (int32_t)r->ents[i].ty + SPREAD[sp][1];
                if(cx < 0 || cy < 0) continue;

                const uint8_t hx = (uint8_t)cx, hy = (uint8_t)cy;
                if(ft_tile_solid(ft_map_tile(r->map, hx, hy))) continue;

                bool taken = false;
                for(uint8_t o = 0; o < m; o++) {
                    if(f->w[o].home_tx == hx && f->w[o].home_ty == hy) taken = true;
                }
                if(taken) continue;

                k->home_tx = hx;
                k->home_ty = hy;
                break;
            }
            k->mv.tx = k->home_tx;
            k->mv.ty = k->home_ty;
            k->mv.dx = 0;
            k->mv.dy = 0;
            k->mv.step_ms = 0;

            /* Stagger the think clocks, or the group steps in unison however
             * separate its positions are. */
            k->think_ms = (uint32_t)((i * 3u + m) * 53u);

            /* Each walker wanders on its own stream. Sharing one made a group
             * shuffle identically, which reads as a single organism. */
            k->seed = 0x2545F491u ^ ((uint32_t)room * 2654435761u) ^
                      ((uint32_t)i * 40503u) ^ ((uint32_t)m * 2246822519u) ^
                      ((uint32_t)r->ents[i].tx << 8);
        }
    }
}

void ft_world_init(FtWorld* w) {
    for(size_t i = 0; i < sizeof(w->cleared); i++) w->cleared[i] = 0u;

    ft_stats_init(&w->stats);
    ft_guide_init(&w->guide);
    ft_quests_init(&w->quests);
    ft_pockets_init(&w->pockets);
    w->visits = 0;
    w->escort = false;
    w->name = FT_NAME_NONE;

    /* Nothing shown yet, and Hale on his gate. Set before the first enter
     * below, because entering is what moves him between rooms. */
    w->room = 0;
    w->area_named = NULL;
    w->banner = false;
    w->revealed = 0;
    w->revealed_now = false;
    w->echo_now = false;
    w->hale = (uint8_t)FT_HALE_POST;
    w->hale_room = FT_ROOM_WELDHOME;
    ft_world_hale_post(&w->hale_mv.tx, &w->hale_mv.ty);
    w->hale_mv.dx = 0;
    w->hale_mv.dy = 0;
    w->hale_mv.step_ms = 0;
    w->hale_facing = FT_FACE_DOWN;
    w->hale_hurry = false;
    w->shake_tree = 0;
    w->shake_ms = 0;
    w->hale_waits = 0;
    w->hale_waiting = false;
    w->bark = 0;
    w->bark_who = (uint8_t)FT_BARK_NOBODY;
    w->bark_ms = 0;
    w->chatter_ms = 0;
    w->chatter_at = 0;
    w->bark_tx = 0;
    w->bark_ty = 0;
    w->hush_at = 0;

    /* World stats are authoritative: a battle copies them in rather than
     * building its own. */
    w->stats.charge = w->stats.charge_max;

    /* The first room's terminal is where a new run starts and, until you save
     * somewhere else, where being downed puts you back. */
    w->save_room = 0;
    w->save_tx = 3;
    w->save_ty = 3;

    ft_world_enter(w, 0, 3, 4);
}

/* ---- Foes -------------------------------------------------------------- */

static int32_t abs32(int32_t v) {
    return v < 0 ? -v : v;
}

/* Each walker's own xorshift, so a group does not shuffle in unison. */
static uint32_t foe_rand(FtFoeWalker* k) {
    uint32_t x = k->seed ? k->seed : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    k->seed = x;
    return x;
}

/* Can this walker see the player? */

/* ---- Chasing ----------------------------------------------------------
 *
 * A flow field: breadth-first from the player's tile outward, so every
 * walkable tile knows how many steps it is from them. A chaser then just
 * walks downhill.
 *
 * The previous version was a greedy step with a few fallbacks, which is not
 * pathfinding — it cannot route around anything longer than itself. Cold
 * Storage's sealed cell, the Turnstile's ranks and the Deadzone's voids all
 * defeated it: a foe would walk into the wall between you and it until you
 * left. One search per room per player-tile serves every foe in it, which is
 * why this is affordable at all.
 *
 * The buffers are file statics rather than part of FtWorld: they are scratch,
 * they are rebuilt from scratch every time they are used, and FtWorld gets
 * copied around (saves, tests) where another kilobyte and a half would be
 * carried for nothing. */
#define FT_FLOW_TILES 512
#define FT_FLOW_FAR   255

static uint8_t  g_flow[FT_FLOW_TILES];
static uint16_t g_flow_queue[FT_FLOW_TILES];
static const FtMap* g_flow_map;
static uint8_t  g_flow_px, g_flow_py;
static bool     g_flow_valid;

static void flow_build(const FtMap* m, uint8_t px, uint8_t py) {
    g_flow_valid = false;

    const uint32_t cells = (uint32_t)m->w * m->h;
    if(cells == 0u || cells > FT_FLOW_TILES) return; /* too big: chase greedily */
    if(ft_tile_solid(ft_map_tile(m, px, py))) return;

    for(uint32_t i = 0; i < cells; i++) g_flow[i] = FT_FLOW_FAR;

    uint32_t head = 0, tail = 0;
    const uint32_t start = (uint32_t)py * m->w + px;

    g_flow[start] = 0;
    g_flow_queue[tail++] = (uint16_t)start;

    static const int8_t STEP[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

    while(head < tail) {
        const uint32_t cell = g_flow_queue[head++];
        const int32_t cx = (int32_t)(cell % m->w);
        const int32_t cy = (int32_t)(cell / m->w);
        const uint8_t d = g_flow[cell];

        if(d == FT_FLOW_FAR - 1u) continue; /* do not overflow the counter */

        for(uint8_t k = 0; k < 4u; k++) {
            const int32_t nx = cx + STEP[k][0], ny = cy + STEP[k][1];
            if(nx < 0 || ny < 0 || nx >= (int32_t)m->w || ny >= (int32_t)m->h) continue;

            const uint32_t n = (uint32_t)ny * m->w + (uint32_t)nx;
            if(g_flow[n] != FT_FLOW_FAR) continue;
            if(ft_tile_solid(ft_map_tile(m, nx, ny))) continue;

            g_flow[n] = (uint8_t)(d + 1u);
            g_flow_queue[tail++] = (uint16_t)n;
        }
    }

    g_flow_map = m;
    g_flow_px = px;
    g_flow_py = py;
    g_flow_valid = true;
}

/* Rebuild only when it would say something different: the player has moved to
 * another tile, or this is another room. Once per step, not once per frame. */
static void flow_refresh(const FtMap* m, uint8_t px, uint8_t py) {
    if(g_flow_valid && g_flow_map == m && g_flow_px == px && g_flow_py == py) return;
    flow_build(m, px, py);
}

static uint8_t flow_at(const FtMap* m, int32_t tx, int32_t ty) {
    if(!g_flow_valid || g_flow_map != m) return FT_FLOW_FAR;
    if(tx < 0 || ty < 0 || tx >= (int32_t)m->w || ty >= (int32_t)m->h) return FT_FLOW_FAR;

    return g_flow[(uint32_t)ty * m->w + (uint32_t)tx];
}

/* Whether a walker notices you: you are within FT_FOE_ALERT steps of it by
 * a route it could actually walk.
 *
 * It used to be a straight-line count that ignored walls, so a foe on The
 * Drop's upper shelf "saw" you through the terrace wall on the floor below,
 * ran to the ladder and met you there. Going out you never pass under it;
 * coming back you must — a careful player was caught on the way home 286
 * times in 300 and never on the way out. Measured by walking distance, a
 * wall is a wall in both directions. A room too big for the flow field
 * falls back to the straight count. */
static bool foe_spots(const FtWorld* w, const FtFoeWalker* k, const FtMap* map) {
    if(g_flow_valid && g_flow_map == map && g_flow_px == w->mv.tx && g_flow_py == w->mv.ty) {
        return flow_at(map, k->mv.tx, k->mv.ty) <= FT_FOE_ALERT;
    }
    const int32_t dx = (int32_t)w->mv.tx - (int32_t)k->mv.tx;
    const int32_t dy = (int32_t)w->mv.ty - (int32_t)k->mv.ty;
    return (abs32(dx) + abs32(dy)) <= FT_FOE_ALERT;
}

/* Is another walker standing on, or stepping into, this tile? Without this
 * three wanderers converge and sit on top of each other, which puts the
 * group straight back to looking like one object. */
static bool walker_occupied(const FtWorld* w, const FtFoeWalker* self,
                            int32_t tx, int32_t ty) {
    const FtRoom* r = ft_room(w->room);

    for(uint8_t i = 0; i < r->ent_count && i < FT_MAX_ROOM_ENTS; i++) {
        const FtFoeState* f = &w->foes[i];
        if(!f->alive) continue;

        for(uint8_t m = 0; m < f->count; m++) {
            const FtFoeWalker* k = &f->w[m];
            if(k == self) continue;

            if((int32_t)k->mv.tx == tx && (int32_t)k->mv.ty == ty) return true;
            if((int32_t)k->mv.tx + k->mv.dx == tx &&
               (int32_t)k->mv.ty + k->mv.dy == ty) {
                return true;
            }
        }
    }
    return false;
}

static void foe_think(FtWorld* w, FtFoeWalker* k, bool alert, const FtMap* map) {
    FtFoeWalker* f = k;
    int8_t sx = 0, sy = 0;

    if(alert) {
        /* Downhill on the flow field, with a jitter that lets a walker take
         * an equal-length neighbour instead: several chasers on one corridor
         * otherwise queue into a single column. */
        static const int8_t STEP[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

        const uint8_t here = flow_at(map, f->mv.tx, f->mv.ty);
        const uint8_t roll = (uint8_t)(foe_rand(f) & 3u);

        uint8_t best = here;
        int8_t bx = 0, by = 0;

        for(uint8_t step = 0; step < 4u; step++) {
            const uint8_t j = (uint8_t)((step + roll) & 3u);
            const int8_t cx = STEP[j][0], cy = STEP[j][1];

            const int32_t nx = (int32_t)f->mv.tx + cx, ny = (int32_t)f->mv.ty + cy;
            const uint8_t d = flow_at(map, nx, ny);

            if(d >= best) continue;
            if(!step_target_free(map, f->mv.tx, f->mv.ty, cx, cy)) continue;
            if(walker_occupied(w, f, nx, ny)) continue;

            best = d;
            bx = cx;
            by = cy;
        }

        if(bx || by) {
            f->mv.dx = bx;
            f->mv.dy = by;
            f->mv.step_ms = 0;
        }
        return;
    }

    {
        const int32_t hx = (int32_t)f->home_tx - (int32_t)f->mv.tx;
        const int32_t hy = (int32_t)f->home_ty - (int32_t)f->mv.ty;

        if(abs32(hx) + abs32(hy) > FT_FOE_LEASH) {
            /* On the leash: head back, so an idle room does not slowly empty
             * itself into a corner. */
            if(abs32(hx) >= abs32(hy)) sx = (hx > 0) ? 1 : -1;
            else sy = (hy > 0) ? 1 : -1;
        } else {
            switch(foe_rand(f) % 8u) {
            case 0: sx = 1; break;
            case 1: sx = -1; break;
            case 2: sy = 1; break;
            case 3: sy = -1; break;
            default: break; /* idle half the time, at its own rhythm */
            }
        }
    }

    if(!sx && !sy) return;
    if(!step_target_free(map, f->mv.tx, f->mv.ty, sx, sy)) return;
    if(walker_occupied(w, f, (int32_t)f->mv.tx + sx, (int32_t)f->mv.ty + sy)) return;

    f->mv.dx = sx;
    f->mv.dy = sy;
    f->mv.step_ms = 0;
}

/* ---- Update ------------------------------------------------------------ */

/* Defined with the other tile queries below; the player's step needs it,
 * because an NPC is something you walk into rather than through. */
static int npc_at_tile(const FtWorld* w, int32_t tx, int32_t ty);
static bool hale_blocks(const FtWorld* w, int32_t tx, int32_t ty);
static void hale_update(FtWorld* w, uint32_t dt_ms);
static void say_aloud(FtWorld* w, FtBarkWho who, uint8_t bark);
static void chatter(FtWorld* w, uint32_t dt_ms);
static void echo_update(FtWorld* w);

void ft_world_update(FtWorld* w, int8_t dx, int8_t dy, uint32_t dt_ms) {
    const FtMap* map = ft_world_map(w);

    w->area_ms += dt_ms;
    w->bark_ms = (w->bark_ms > dt_ms) ? (uint16_t)(w->bark_ms - dt_ms) : 0u;
    w->shake_ms = (w->shake_ms > dt_ms) ? (uint16_t)(w->shake_ms - dt_ms) : 0u;
    w->arrived = false;
    w->ambushed = false;
    w->revealed_now = false;
    w->echo_now = false;

    /* --- player --- */
    if(ft_world_moving(w)) {
        w->walk_ms += dt_ms;
        w->arrived = step_advance(&w->mv, dt_ms, FT_STEP_MS);
    }

    /* --- whoever is walking with you --- */
    if(w->escort && (w->escort_mv.dx || w->escort_mv.dy)) {
        (void)step_advance(&w->escort_mv, dt_ms, FT_STEP_MS);
    }

    if(!ft_world_moving(w) && (dx || dy)) {
        /* One axis at a time keeps the player on the grid; horizontal wins so
         * a diagonal press still moves rather than stalling. */
        if(dx) dy = 0;

        if(dx < 0) w->facing = FT_FACE_LEFT;
        else if(dx > 0) w->facing = FT_FACE_RIGHT;
        else if(dy < 0) w->facing = FT_FACE_UP;
        else if(dy > 0) w->facing = FT_FACE_DOWN;

        /* Facing always updates, even into a wall — that is what lets you
         * turn and strike something you cannot walk into, and what lets you
         * turn and talk to someone you just bumped into. */
        if(player_step_free(w, map, dx, dy) &&
           npc_at_tile(w, (int32_t)w->mv.tx + dx, (int32_t)w->mv.ty + dy) < 0 &&
           !hale_blocks(w, (int32_t)w->mv.tx + dx, (int32_t)w->mv.ty + dy)) {
            /* She steps into the tile you are leaving on the frame you leave
             * it, so the two move in lockstep and she never falls behind
             * however long the direction is held. It also means every tile
             * she walks is a tile you walked: she can never end up inside a
             * wall, and never cuts a corner through one. */
            /* Where the last in the line is standing before anybody moves:
             * that is the tile Hale steps into, if he is in his place. */
            const int32_t last_x = w->escort ? (int32_t)w->escort_mv.tx : (int32_t)w->mv.tx;
            const int32_t last_y = w->escort ? (int32_t)w->escort_mv.ty : (int32_t)w->mv.ty;

            if(w->escort && !(w->escort_mv.dx || w->escort_mv.dy)) {
                const int32_t ex = (int32_t)w->mv.tx - (int32_t)w->escort_mv.tx;
                const int32_t ey = (int32_t)w->mv.ty - (int32_t)w->escort_mv.ty;

                if(abs32(ex) + abs32(ey) == 1) {
                    w->escort_mv.dx = (int8_t)ex;
                    w->escort_mv.dy = (int8_t)ey;
                    w->escort_mv.step_ms = 0;
                } else if(ex || ey) {
                    /* A door, a ladder or a drop moves you further than a
                     * step. She catches up rather than walking it. */
                    w->escort_mv.tx = w->mv.tx;
                    w->escort_mv.ty = w->mv.ty;
                }
            }

            /* Hale, once he is in his place behind her (or you), joins the
             * same conga: he steps into the tile the one in front of him is
             * leaving, on the frame they leave it. Chasing that tile a frame
             * later instead left him a whole tile out of place all the way
             * home, because at the same pace a gap never closes. */
            if(w->hale == (uint8_t)FT_HALE_FOLLOW && ft_world_hale_here(w) &&
               !(w->hale_mv.dx || w->hale_mv.dy)) {
                const int32_t hx = (int32_t)last_x - (int32_t)w->hale_mv.tx;
                const int32_t hy = (int32_t)last_y - (int32_t)w->hale_mv.ty;

                if(abs32(hx) + abs32(hy) == 1) {
                    w->hale_mv.dx = (int8_t)hx;
                    w->hale_mv.dy = (int8_t)hy;
                    w->hale_mv.step_ms = 0;
                    w->hale_hurry = false;
                    w->hale_facing = (hx > 0) ? FT_FACE_RIGHT : (hx < 0) ? FT_FACE_LEFT :
                                     (hy > 0) ? FT_FACE_DOWN : FT_FACE_UP;
                }
            }

            w->mv.dx = dx;
            w->mv.dy = dy;
            w->mv.step_ms = 0;
            w->walk_ms += dt_ms;
        }
    }

    /* --- Hale, after you, so he reacts to where you are going --- */
    hale_update(w, dt_ms);
    chatter(w, dt_ms);
    echo_update(w);

    /* --- foes --- */
    const FtRoom* room = ft_room(w->room);

    /* Alert is shared: one of them noticing you brings the whole room. A group
     * that reacts individually reads as three oblivious animals rather than
     * something that has seen you. */
    bool any_spotted = false;
    bool any_alive = false;
    for(uint8_t i = 0; i < room->ent_count && i < FT_MAX_ROOM_ENTS; i++) {
        if(w->foes[i].alive) any_alive = true;
    }
    if(any_alive) flow_refresh(map, w->mv.tx, w->mv.ty);

    for(uint8_t i = 0; i < room->ent_count && i < FT_MAX_ROOM_ENTS; i++) {
        FtFoeState* f = &w->foes[i];
        if(!f->alive) continue;

        /* Frozen: it sees nothing until it thaws. */
        if(f->stun_ms > 0u) {
            f->stun_ms = (dt_ms >= (uint32_t)f->stun_ms) ? 0u
                                                         : (uint16_t)(f->stun_ms - (uint16_t)dt_ms);
            continue;
        }

        for(uint8_t m = 0; m < f->count; m++) {
            if(foe_spots(w, &f->w[m], map)) any_spotted = true;
        }
    }
    if(any_spotted) {
        for(uint8_t i = 0; i < room->ent_count && i < FT_MAX_ROOM_ENTS; i++) {
            if(w->foes[i].stun_ms > 0u) continue;
            /* Only the transition starts the beat. Re-arming it every frame
             * the player stays in range would freeze the room solid. */
            if(!w->foes[i].alert) w->foes[i].notice_ms = FT_FOE_NOTICE_MS;
            w->foes[i].alert = true;
        }
    }

    /* Run the beat down before anything alerted gets to move. */
    for(uint8_t i = 0; i < room->ent_count && i < FT_MAX_ROOM_ENTS; i++) {
        FtFoeState* f = &w->foes[i];
        if(f->notice_ms == 0u) continue;
        f->notice_ms = (dt_ms >= (uint32_t)f->notice_ms)
                           ? 0u
                           : (uint16_t)(f->notice_ms - (uint16_t)dt_ms);
    }

    /* One search per room per player tile, shared by every chaser in it. */
    bool chasing = false;
    for(uint8_t i = 0; i < room->ent_count && i < FT_MAX_ROOM_ENTS; i++) {
        if(w->foes[i].alive && w->foes[i].alert) chasing = true;
    }
    if(chasing) flow_refresh(map, w->mv.tx, w->mv.ty);

    for(uint8_t i = 0; i < room->ent_count && i < FT_MAX_ROOM_ENTS; i++) {
        FtFoeState* f = &w->foes[i];
        if(!f->alive) continue;

        /* Frozen by Infrared: a step under way finishes, nothing new starts. */
        if(f->stun_ms > 0u) {
            for(uint8_t m = 0; m < f->count; m++) {
                FtFoeWalker* k = &f->w[m];
                if(k->mv.dx || k->mv.dy) (void)step_advance(&k->mv, dt_ms, FT_FOE_STEP_MS);
            }
            continue;
        }

        /* A boss stands where the story put it: it does not wander off, and
         * it does not come for you. You go to it. */
        const FtRoster* ro = ft_roster(room->ents[i].roster);
        if(room->ents[i].kind == FT_ENT_FOE && ro->count > 0u &&
           (FT_ENEMIES[ro->foes[0]].attrs & FT_ATTR_BOSS)) {
            continue;
        }

        /* Every walker steps and thinks on its own clock. */
        for(uint8_t m = 0; m < f->count; m++) {
            FtFoeWalker* k = &f->w[m];

            /* Spotted you, and taking it in. A step already under way still
             * finishes — stopping dead mid-tile would break the grid. */
            if(f->notice_ms > 0u && !(k->mv.dx || k->mv.dy)) continue;

            if(k->mv.dx || k->mv.dy) {
                /* A walker that finishes its step standing on the player
                 * reached *you*: it is the aggressor, and it gets the
                 * opening turn. Walking into one yourself does not count,
                 * which is the whole distinction. */
                if(step_advance(&k->mv, dt_ms, FT_FOE_STEP_MS)) {
                    if(k->mv.tx == w->mv.tx && k->mv.ty == w->mv.ty) {
                        w->ambushed = true;
                    }
                }
                continue;
            }

            k->think_ms += dt_ms;
            if(k->think_ms < FT_FOE_THINK_MS) continue;

            k->think_ms = 0;
            foe_think(w, k, f->alert, map);
        }
    }
}

bool ft_world_foe_noticing(const FtWorld* w, uint8_t index) {
    if(index >= FT_MAX_ROOM_ENTS) return false;
    const FtFoeState* f = &w->foes[index];
    return f->alive && f->notice_ms > 0u;
}

/* ---- Queries ----------------------------------------------------------- */

static void facing_delta(FtFacing f, int32_t* dx, int32_t* dy) {
    *dx = (f == FT_FACE_LEFT) ? -1 : (f == FT_FACE_RIGHT) ? 1 : 0;
    *dy = (f == FT_FACE_UP) ? -1 : (f == FT_FACE_DOWN) ? 1 : 0;
}

/* NPCs never move and are never cleared, so this is a plain table lookup. */
static int npc_at_tile(const FtWorld* w, int32_t tx, int32_t ty) {
    const FtRoom* r = ft_room(w->room);

    for(uint8_t i = 0; i < r->ent_count && i < FT_MAX_ROOM_ENTS; i++) {
        const FtEntKind k = r->ents[i].kind;

        /* People. A tree is not one of them any more: the trunk is a map
         * tile and blocks whether or not anything is growing on it. */
        const bool solid = (k == FT_ENT_NPC) || (k == FT_ENT_WREN && ft_world_wren_present(w, i));
        if(!solid) continue;
        if((int32_t)r->ents[i].tx == tx && (int32_t)r->ents[i].ty == ty) return (int)i;
    }
    return -1;
}

static int foe_at_tile(const FtWorld* w, int32_t tx, int32_t ty) {
    const FtRoom* r = ft_room(w->room);

    for(uint8_t i = 0; i < r->ent_count && i < FT_MAX_ROOM_ENTS; i++) {
        const FtFoeState* f = &w->foes[i];
        if(!f->alive) continue;

        /* Touching any walker starts the marker's fight: the group is one
         * encounter however spread out it is standing. */
        for(uint8_t m = 0; m < f->count; m++) {
            /* A walker mid-step counts as occupying the tile it is heading
             * for, so you cannot walk through one moving toward you. */
            const int32_t fx = (int32_t)f->w[m].mv.tx + f->w[m].mv.dx;
            const int32_t fy = (int32_t)f->w[m].mv.ty + f->w[m].mv.dy;

            if((fx == tx && fy == ty) ||
               ((int32_t)f->w[m].mv.tx == tx && (int32_t)f->w[m].mv.ty == ty)) {
                return (int)i;
            }
        }
    }
    return -1;
}

int ft_world_foe_contact(const FtWorld* w) {
    return foe_at_tile(w, w->mv.tx, w->mv.ty);
}

int ft_world_foe_ahead(const FtWorld* w) {
    int32_t dx, dy;
    facing_delta(w->facing, &dx, &dy);
    return foe_at_tile(w, (int32_t)w->mv.tx + dx, (int32_t)w->mv.ty + dy);
}

int ft_world_npc_ahead(const FtWorld* w) {
    int32_t dx, dy;
    facing_delta(w->facing, &dx, &dy);

    const int32_t tx = (int32_t)w->mv.tx + dx, ty = (int32_t)w->mv.ty + dy;
    return npc_at_tile(w, tx, ty);
}

/* Whether a given tree is bearing on a given visit.
 *
 * A hash rather than a stored bit: it is the same answer every time it is
 * asked within a visit, it needs no save bytes, and it changes when you come
 * back. FNV-1a over the three things that identify this tree, this time. */
bool ft_world_bearing(const FtWorld* w, uint8_t index) {
    const FtRoom* r = ft_room(w->room);
    if(index >= r->ent_count || index >= FT_MAX_ROOM_ENTS) return false;

    /* Already taken this visit. */
    if(ft_world_entity_gone(w, index)) return false;

    /* Somebody left a cache; it is there or it is not. */
    if(r->ents[index].kind == FT_ENT_CACHE) return true;
    if(r->ents[index].kind != FT_ENT_TREE) return false;

    uint32_t h = 2166136261u;
    h = (h ^ w->room) * 16777619u;
    h = (h ^ index) * 16777619u;
    h = (h ^ (w->visits & 0xFFu)) * 16777619u;
    h = (h ^ ((w->visits >> 8) & 0xFFu)) * 16777619u;

    return (h % 100u) < FT_TREE_BEARING_PCT;
}

int ft_world_pick_ahead(const FtWorld* w) {
    int32_t dx, dy;
    facing_delta(w->facing, &dx, &dy);

    const int32_t tx = (int32_t)w->mv.tx + dx, ty = (int32_t)w->mv.ty + dy;
    const FtRoom* r = ft_room(w->room);

    for(uint8_t i = 0; i < r->ent_count && i < FT_MAX_ROOM_ENTS; i++) {
        const FtEntKind k = r->ents[i].kind;
        if(k != FT_ENT_CACHE) continue;
        if((int32_t)r->ents[i].tx != tx || (int32_t)r->ents[i].ty != ty) continue;
        if(!ft_world_bearing(w, i)) continue;

        return (int)i;
    }
    return -1;
}

/* The crown is the 3x2 of leaves above the trunk. */
static bool in_crown(const FtEntity* e, int32_t tx, int32_t ty) {
    return tx >= (int32_t)e->tx - 1 && tx <= (int32_t)e->tx + 1 &&
           ty >= (int32_t)e->ty - 2 && ty <= (int32_t)e->ty - 1;
}

int ft_world_tree_near(const FtWorld* w) {
    int32_t dx, dy;
    facing_delta(w->facing, &dx, &dy);

    const int32_t px = w->mv.tx, py = w->mv.ty;
    const FtRoom* r = ft_room(w->room);

    for(uint8_t i = 0; i < r->ent_count && i < FT_MAX_ROOM_ENTS; i++) {
        const FtEntity* e = &r->ents[i];
        if(e->kind != FT_ENT_TREE) continue;

        if(in_crown(e, px, py)) return (int)i;
        if((int32_t)e->tx == px + dx && (int32_t)e->ty == py + dy) return (int)i;
    }
    return -1;
}

FtItemId ft_world_shake(FtWorld* w, uint8_t index) {
    w->shake_tree = index;
    w->shake_ms = (uint16_t)FT_SHAKE_MS;
    return ft_world_pick(w, index);
}

int8_t ft_world_shake_offset(const FtWorld* w, int32_t tx, int32_t ty) {
    if(w->shake_ms == 0u) return 0;

    const FtRoom* r = ft_room(w->room);
    if(w->shake_tree >= r->ent_count) return 0;
    if(!in_crown(&r->ents[w->shake_tree], tx, ty)) return 0;

    /* A few quick sways, not a vibration: 60ms each way. */
    return ((w->shake_ms / 60u) % 2u) ? 1 : -1;
}

FtItemId ft_world_pick(FtWorld* w, uint8_t index) {
    const FtRoom* r = ft_room(w->room);
    if(index >= r->ent_count || index >= FT_MAX_ROOM_ENTS) return FT_ITEM_COUNT;

    const FtEntKind k = r->ents[index].kind;
    if(k != FT_ENT_TREE && k != FT_ENT_CACHE) return FT_ITEM_COUNT;
    if(!ft_world_bearing(w, index)) return FT_ITEM_COUNT;

    const FtItemId id = (FtItemId)r->ents[index].roster;

    /* Full pockets leave it on the tree. Picking something you cannot carry
     * and watching it vanish is the worst possible outcome. */
    if(!ft_pockets_add(&w->pockets, id)) return FT_ITEM_COUNT;

    ft_world_clear_entity(w, index);
    return id;
}

bool ft_world_wren_present(const FtWorld* w, uint8_t index) {
    const FtRoom* r = ft_room(w->room);
    if(index >= r->ent_count || r->ents[index].kind != FT_ENT_WREN) return false;

    const bool home = ft_quest_state(&w->quests, FT_QUEST_WREN) == FT_QUEST_DONE;

    if(r->ents[index].roster == FT_WREN_HOME) return home;

    /* In the cave until she is home, except while she is walking with you:
     * then she is behind you, not down there. Lose her on the way — a
     * reload, a wander off without her — and she is back where you found
     * her, and willing to go again. */
    return !home && !w->escort;
}

int ft_world_wren_ahead(const FtWorld* w) {
    int32_t dx, dy;
    facing_delta(w->facing, &dx, &dy);

    const int32_t tx = (int32_t)w->mv.tx + dx, ty = (int32_t)w->mv.ty + dy;
    const FtRoom* r = ft_room(w->room);

    for(uint8_t i = 0; i < r->ent_count && i < FT_MAX_ROOM_ENTS; i++) {
        if(!ft_world_wren_present(w, i)) continue;
        if((int32_t)r->ents[i].tx == tx && (int32_t)r->ents[i].ty == ty) return (int)i;
    }
    return -1;
}

bool ft_world_exit_open(const FtWorld* w, const FtExit* x) {
    if(x == NULL) return false;
    if(x->need_quest == 0u) return true;

    return ft_quest_at_least(&w->quests, (FtQuestId)(x->need_quest - 1u),
                             (FtQuestState)x->need_state);
}

const char* ft_world_exit_refusal(const FtExit* x) {
    if(x == NULL || x->need_quest == 0u) return NULL;

    const FtQuestId id = (FtQuestId)(x->need_quest - 1u);

    /* A gate somebody is holding says who is holding it; a way you simply
     * have no reason to take is the Courier talking to themselves. */
    if(x->need_state == (uint8_t)FT_QUEST_DONE) return "Coll won't open it.";

    return ft_quest_refusal(id, (FtQuestState)x->need_state);
}

bool ft_world_naming_due(const FtWorld* w) {
    if(!w->arrived || !w->escort || w->name != FT_NAME_NONE) return false;
    if(w->room != FT_ROOM_APPROACH && w->room != FT_ROOM_WELDHOME) return false;

    /* Climbing out, she is on your tile until you step off it. Waiting for
     * that means she is standing beside you when she speaks, not inside you. */
    if(w->escort_mv.tx == w->mv.tx && w->escort_mv.ty == w->mv.ty) return false;

    /* And out of the long grass, both of you. In it, the grass is drawn over
     * your legs and hers and the scene was a picture of grass with two heads
     * in it — not the moment you get a name. */
    const FtMap* m = ft_world_map(w);
    return ft_map_tile(m, w->mv.tx, w->mv.ty) != FT_TILE_TALL_GRASS &&
           ft_map_tile(m, w->escort_mv.tx, w->escort_mv.ty) != FT_TILE_TALL_GRASS;
}

void ft_world_escort_start(FtWorld* w) {
    w->escort = true;
    w->chatter_ms = 0;
    say_aloud(w, FT_BARK_BY_WREN, FT_BARK_WREN_START);
    w->escort_mv.tx = w->mv.tx;
    w->escort_mv.ty = w->mv.ty;
    w->escort_mv.dx = 0;
    w->escort_mv.dy = 0;
    w->escort_mv.step_ms = 0;
}

void ft_world_escort_stop(FtWorld* w) {
    w->escort = false;
}

/* Has this exit been shown to you? Always, for an ordinary one. */
static bool exit_known(const FtWorld* w, const FtExit* x) {
    return (w->revealed & x->reveal) == x->reveal;
}

const FtExit* ft_world_exit_under(const FtWorld* w) {
    const FtRoom* r = ft_room(w->room);

    for(uint8_t i = 0; i < r->exit_count; i++) {
        const FtExit* x = &r->exits[i];
        if(x->tx != w->mv.tx || x->ty != w->mv.ty) continue;

        /* Standing on a way nobody has shown you is standing in grass. */
        if(!exit_known(w, x)) return NULL;
        return x;
    }
    return NULL;
}

bool ft_world_pit_at(const FtWorld* w, int32_t tx, int32_t ty) {
    const FtRoom* r = ft_room(w->room);

    for(uint8_t i = 0; i < r->exit_count; i++) {
        const FtExit* x = &r->exits[i];
        if(x->reveal == 0u || !exit_known(w, x)) continue;
        if((int32_t)x->tx == tx && (int32_t)x->ty == ty) return true;
    }
    return false;
}

/* ---- Saying things out loud -------------------------------------------- */

static void say_aloud(FtWorld* w, FtBarkWho who, uint8_t bark) {
    w->bark = bark;
    w->bark_who = (uint8_t)who;
    w->bark_ms = (uint16_t)FT_BARK_MS;
}

/* Wren does not stop talking on the way home. One line every few seconds,
 * in order, and never over the top of somebody else's. */
static void chatter(FtWorld* w, uint32_t dt_ms) {
    if(!w->escort) {
        w->chatter_ms = 0;
        return;
    }
    if(w->bark_ms > 0u) return;

    w->chatter_ms = (uint16_t)(w->chatter_ms + dt_ms);
    if(w->chatter_ms < FT_CHATTER_MS) return;

    w->chatter_ms = 0;
    say_aloud(w, FT_BARK_BY_WREN,
              (uint8_t)(FT_BARK_WREN_CHATTER + (w->chatter_at % FT_BARK_WREN_CHATTER_N)));
    w->chatter_at++;
}

/* ---- Echo -------------------------------------------------------------
 *
 * The Courier before you, seen once: across a gap you cannot cross, it
 * looks at you, says Hush's line, and is gone. That is all of it for now —
 * the fight is at the Scrapline's relay (STORY.md §6). */

static bool echo_due(const FtWorld* w) {
    return w->room == FT_ECHO_ROOM &&
           ft_quest_state(&w->quests, FT_QUEST_WREN) == FT_QUEST_DONE;
}

bool ft_world_echo_here(const FtWorld* w) {
    if(!echo_due(w)) return false;
    if(!(w->revealed & FT_REVEAL_ECHO)) return true;

    /* Seen: it stays exactly as long as it is still talking. */
    return w->bark_who == (uint8_t)FT_BARK_BY_ECHO && w->bark_ms > 0u;
}

/* Whether Echo, and a tile of air over it for its line, is inside the view
 * the camera would show with you standing where you are. */
static bool echo_in_view(const FtWorld* w) {
    const FtPos focus = {(int32_t)w->mv.tx * FT_TILE_PX, (int32_t)w->mv.ty * FT_TILE_PX};
    const FtPos cam = ft_map_camera(ft_world_map(w), focus);
    const int32_t ex = FT_ECHO_TX * FT_TILE_PX, ey = FT_ECHO_TY * FT_TILE_PX;

    return ex >= cam.x && ex + FT_TILE_PX <= cam.x + FT_VIEW_W * FT_TILE_PX &&
           ey - FT_TILE_PX >= cam.y && ey + FT_TILE_PX <= cam.y + FT_VIEW_H * FT_TILE_PX;
}

static void echo_update(FtWorld* w) {
    if(!echo_due(w) || (w->revealed & FT_REVEAL_ECHO)) return;
    if(ft_world_moving(w) || !echo_in_view(w)) return;

    w->revealed |= FT_REVEAL_ECHO;
    w->echo_now = true;
    w->bark_tx = FT_ECHO_TX;
    w->bark_ty = FT_ECHO_TY;
    say_aloud(w, FT_BARK_BY_ECHO, (uint8_t)FT_BARK_ECHO);

    /* A beat longer than anybody else's line. It is looking at you. */
    w->bark_ms = (uint16_t)(FT_BARK_MS + FT_BARK_MS / 2u);
}

/* ---- The Scrapline ------------------------------------------------------ */

bool ft_world_bridge_down(const FtWorld* w) {
    return bridge_down_in(w);
}

/* Infrared is line of sight: straight ahead, across whatever gap is there,
 * to the first thing that is not a gap. Far enough for any gap a map draws,
 * and never so far that you point at something off the screen
 * (FT_IR_RANGE, in ft_world.h). */

bool ft_world_ir_target(const FtWorld* w) {
    if(ft_room(w->room)->bridge == 0u || bridge_down_in(w)) return false;

    int32_t dx, dy;
    facing_delta(w->facing, &dx, &dy);

    const FtMap* m = ft_world_map(w);
    int32_t x = w->mv.tx, y = w->mv.ty;
    uint8_t gap = 0;

    for(uint8_t i = 0; i <= FT_IR_RANGE; i++) {
        x += dx;
        y += dy;
        const FtTile t = ft_map_tile(m, x, y);

        if(t == FT_TILE_VOID || t == FT_TILE_BRIDGE) {
            gap++;
            continue;
        }
        return t == FT_TILE_RECEIVER && gap > 0u;
    }
    return false;
}

bool ft_world_ir_fire(FtWorld* w) {
    if(!ft_world_ir_target(w)) return false;
    w->revealed |= ft_room(w->room)->bridge;
    return true;
}

int ft_world_ir_foe(const FtWorld* w) {
    int32_t dx, dy;
    facing_delta(w->facing, &dx, &dy);

    const FtMap* m = ft_world_map(w);
    int32_t x = w->mv.tx, y = w->mv.ty;

    for(uint8_t i = 1; i <= FT_IR_RANGE; i++) {
        x += dx;
        y += dy;
        const FtTile t = ft_map_tile(m, x, y);
        const int who = foe_at_tile(w, x, y);

        if(who >= 0) return (i >= 2u) ? who : -1;
        if(ft_tile_solid(t) && t != FT_TILE_VOID && t != FT_TILE_BRIDGE) return -1;
    }
    return -1;
}

bool ft_world_ir_stun(FtWorld* w) {
    const int who = ft_world_ir_foe(w);
    if(who < 0) return false;

    FtFoeState* f = &w->foes[who];
    f->stun_ms = (uint16_t)FT_IR_STUN_MS;
    f->alert = false;
    f->notice_ms = 0;

    /* A walker mid-step finishes it: stopping between tiles breaks the grid. */
    return true;
}

bool ft_world_foe_stunned(const FtWorld* w, uint8_t index) {
    return index < FT_MAX_ROOM_ENTS && w->foes[index].alive && w->foes[index].stun_ms > 0u;
}

bool ft_world_relay_ahead(const FtWorld* w) {
    int32_t dx, dy;
    facing_delta(w->facing, &dx, &dy);
    return ft_map_tile(ft_world_map(w), (int32_t)w->mv.tx + dx, (int32_t)w->mv.ty + dy) ==
           FT_TILE_RELAY;
}

bool ft_world_call_due(const FtWorld* w) {
    return w->room == FT_ROOM_RELAY &&
           ft_quest_at_least(&w->quests, FT_QUEST_RIVET, FT_QUEST_READY) &&
           !(w->revealed & FT_REVEAL_KEEPER_CALL);
}

/* ---- Hale ---------------------------------------------------------------
 *
 * The other guard on Weldhome's gate, and the one who knows where Wren went.
 * See FtHalePhase in ft_world.h for what he can be doing. Every change of
 * phase is caused by where the player goes, never by something they press,
 * so there is nothing here the player has to learn. */

/* Where he stands on the gate: the other side of it from Coll, so the tile
 * in front of the gate stays clear for the pair of them. */
#define HALE_POST_TX 22
#define HALE_POST_TY 6

/* Leading, he leaves Weldhome by its west door and comes into the Approach
 * one tile in from its east one — just ahead of where you will arrive. */
#define HALE_OUT_TX  0
#define HALE_OUT_TY  5
#define HALE_IN_TX   21
#define HALE_IN_TY   5

/* Just north of the pit, which is the hidden exit at (11,8). He comes down
 * off the path to it, so he stops with the hole in front of him.
 *
 * It was (10,8), west of it, which is the far side coming from the gate —
 * so he walked straight across the hidden tile to get there, one step past
 * where the hole then opened up. That is the "one tile too far". */
#define HALE_PIT_TX  11
#define HALE_PIT_TY  7

/* Where he waits once it is open: one step aside, so the way from the path
 * straight down into the hole is not through him. You are right behind him
 * when he stops, and a guide who then stands in the doorway he just showed
 * you is a guide you have to walk round. */
#define HALE_WAIT_TX 10
#define HALE_WAIT_TY 7

/* How far behind you can fall before he stops and waits for you. */
#define HALE_LEASH 3

/* How far from the pit you walk before he decides you are leaving and comes
 * with you. One wider than the leash, so arriving at the pit right behind
 * him never reads as walking away from it. */
#define HALE_FOLLOW_AT 4

/* By the time he decides to follow, you are already that far off, and at
 * walking pace he would stay that far off all the way home — measured at
 * seven tiles back, which reads as somebody going the same way as you, not
 * with you. So he jogs, at twice your pace, while he is more than a step out
 * of his place. Three-fifths pace was tried first and still had not caught
 * up by the time you reached the gate. */
#define HALE_HURRY_MS (FT_STEP_MS / 2u)

void ft_world_hale_post(uint8_t* tx, uint8_t* ty) {
    *tx = HALE_POST_TX;
    *ty = HALE_POST_TY;
}

void ft_world_hale_pitside(uint8_t* tx, uint8_t* ty) {
    *tx = HALE_WAIT_TX;
    *ty = HALE_WAIT_TY;
}

static void hale_place(FtWorld* w, uint8_t room, uint8_t tx, uint8_t ty) {
    w->hale_hurry = false;
    w->hale_room = room;
    w->hale_mv.tx = tx;
    w->hale_mv.ty = ty;
    w->hale_mv.dx = 0;
    w->hale_mv.dy = 0;
    w->hale_mv.step_ms = 0;
}

bool ft_world_hale_here(const FtWorld* w) {
    return w->hale_room == w->room;
}

uint32_t ft_world_hale_step_ms(const FtWorld* w) {
    return w->hale_hurry ? HALE_HURRY_MS : FT_STEP_MS;
}

/* He is solid while he stands and walks through nobody while he walks, which
 * is the escort's rule too: a person on the move who blocked you would turn
 * following him into a shoving match. */
static bool hale_standing(const FtWorld* w) {
    return w->hale == (uint8_t)FT_HALE_POST || w->hale == (uint8_t)FT_HALE_WAIT;
}

static bool hale_blocks(const FtWorld* w, int32_t tx, int32_t ty) {
    if(!ft_world_hale_here(w) || !hale_standing(w)) return false;
    return (int32_t)w->hale_mv.tx == tx && (int32_t)w->hale_mv.ty == ty;
}

bool ft_world_hale_ahead(const FtWorld* w) {
    int32_t dx, dy;
    facing_delta(w->facing, &dx, &dy);
    return hale_blocks(w, (int32_t)w->mv.tx + dx, (int32_t)w->mv.ty + dy);
}

void ft_world_hale_lead(FtWorld* w) {
    if(w->revealed & FT_REVEAL_PIT) return;
    if(w->hale != (uint8_t)FT_HALE_POST) return;

    w->hale = (uint8_t)FT_HALE_LEAD;
    w->hale_waits = 0;
    w->hale_waiting = false;
    say_aloud(w, FT_BARK_BY_HALE, FT_BARK_HALE_SET_OFF);
}

static int32_t tiles_apart(int32_t ax, int32_t ay, int32_t bx, int32_t by) {
    return abs32(ax - bx) + abs32(ay - by);
}

/* A breadth-first distance field out from the goal, for one room. Rooms are
 * small and he recomputes it only when he is about to take a step, so there
 * is nothing to cache and nothing to go stale. Static rather than on the
 * stack: core does not allocate, and the stack on the device is small. */
#define HALE_GRID 512u
static uint8_t  hale_dist[HALE_GRID];
static uint16_t hale_queue[HALE_GRID];

static bool hale_passable(const FtWorld* w, const FtMap* m, int32_t tx, int32_t ty) {
    if(ft_tile_solid(ft_map_tile(m, tx, ty))) return false;
    if(npc_at_tile(w, tx, ty) >= 0) return false;

    /* He shows you the hole; he does not fall down it — and he knows where
     * it is before you do, so he does not walk over it while it is hidden
     * either. */
    const FtRoom* r = ft_room(w->room);
    for(uint8_t i = 0; i < r->exit_count; i++) {
        const FtExit* x = &r->exits[i];
        if(x->reveal != 0u && (int32_t)x->tx == tx && (int32_t)x->ty == ty) return false;
    }
    return true;
}

/* Is anybody else standing on, or stepping into, this tile? He walks round
 * the player and whoever is with them rather than through them. */
static bool hale_crowded(const FtWorld* w, int32_t tx, int32_t ty) {
    if((int32_t)w->mv.tx == tx && (int32_t)w->mv.ty == ty) return true;
    if((int32_t)w->mv.tx + w->mv.dx == tx && (int32_t)w->mv.ty + w->mv.dy == ty) return true;

    if(w->escort) {
        const FtStepper* e = &w->escort_mv;
        if((int32_t)e->tx == tx && (int32_t)e->ty == ty) return true;
        if((int32_t)e->tx + e->dx == tx && (int32_t)e->ty + e->dy == ty) return true;
    }
    return false;
}

/* Start one step toward (gx, gy). False when he is there, boxed in, or the
 * only way on is through somebody. */
static bool hale_step_toward(FtWorld* w, int32_t gx, int32_t gy) {
    const FtMap* m = ft_world_map(w);
    const uint32_t cells = (uint32_t)m->w * (uint32_t)m->h;
    if(cells == 0u || cells > HALE_GRID) return false;
    if(gx < 0 || gy < 0 || gx >= (int32_t)m->w || gy >= (int32_t)m->h) return false;

    for(uint32_t i = 0; i < cells; i++) hale_dist[i] = 255u;

    uint32_t head = 0, tail = 0;
    const uint16_t goal = (uint16_t)((uint32_t)gy * m->w + (uint32_t)gx);
    hale_dist[goal] = 0u;
    hale_queue[tail++] = goal;

    static const int8_t STEP[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

    while(head < tail) {
        const uint16_t at = hale_queue[head++];
        const int32_t ax = (int32_t)(at % m->w), ay = (int32_t)(at / m->w);
        const uint8_t d = hale_dist[at];
        if(d >= 254u) continue;

        for(uint8_t k = 0; k < 4u; k++) {
            const int32_t nx = ax + STEP[k][0], ny = ay + STEP[k][1];
            if(nx < 0 || ny < 0 || nx >= (int32_t)m->w || ny >= (int32_t)m->h) continue;

            const uint16_t n = (uint16_t)((uint32_t)ny * m->w + (uint32_t)nx);
            if(hale_dist[n] != 255u) continue;
            if(!hale_passable(w, m, nx, ny)) continue;

            hale_dist[n] = (uint8_t)(d + 1u);
            hale_queue[tail++] = n;
        }
    }

    const int32_t hx = w->hale_mv.tx, hy = w->hale_mv.ty;
    const uint8_t here = hale_dist[(uint32_t)hy * m->w + (uint32_t)hx];
    if(here == 0u) return false;

    int8_t bx = 0, by = 0;
    uint8_t best = here;
    for(uint8_t k = 0; k < 4u; k++) {
        const int32_t nx = hx + STEP[k][0], ny = hy + STEP[k][1];
        if(nx < 0 || ny < 0 || nx >= (int32_t)m->w || ny >= (int32_t)m->h) continue;

        const uint8_t d = hale_dist[(uint32_t)ny * m->w + (uint32_t)nx];
        if(d >= best) continue;
        if(hale_crowded(w, nx, ny)) continue;

        best = d;
        bx = STEP[k][0];
        by = STEP[k][1];
    }
    if(!bx && !by) return false;

    w->hale_mv.dx = bx;
    w->hale_mv.dy = by;
    w->hale_mv.step_ms = 0;
    w->hale_hurry = false;
    w->hale_facing = (bx > 0) ? FT_FACE_RIGHT : (bx < 0) ? FT_FACE_LEFT :
                     (by > 0) ? FT_FACE_DOWN : FT_FACE_UP;
    return true;
}

static void hale_face(FtWorld* w, int32_t tx, int32_t ty) {
    const int32_t dx = tx - (int32_t)w->hale_mv.tx, dy = ty - (int32_t)w->hale_mv.ty;
    if(abs32(dx) >= abs32(dy)) {
        if(dx) w->hale_facing = (dx > 0) ? FT_FACE_RIGHT : FT_FACE_LEFT;
    } else {
        w->hale_facing = (dy > 0) ? FT_FACE_DOWN : FT_FACE_UP;
    }
}

/* Where leading goes in the room he is in. */
static void hale_lead_goal(const FtWorld* w, int32_t* gx, int32_t* gy) {
    if(w->room == FT_ROOM_WELDHOME) {
        *gx = HALE_OUT_TX;
        *gy = HALE_OUT_TY;
    } else {
        *gx = HALE_PIT_TX;
        *gy = HALE_PIT_TY;
    }
}

/* The end of this room's part of the walk. */
static void hale_lead_done(FtWorld* w) {
    if(w->room == FT_ROOM_WELDHOME) {
        /* Out of the door and on ahead. He waits just inside the next room
         * for you to come through after him. */
        hale_place(w, FT_ROOM_APPROACH, HALE_IN_TX, HALE_IN_TY);
        w->hale_facing = FT_FACE_LEFT;
    } else if(w->room == FT_ROOM_APPROACH) {
        /* Here. The pit is right by him, and it has been there the whole
         * time. */
        w->revealed |= FT_REVEAL_PIT;
        w->revealed_now = true;
        w->hale = (uint8_t)FT_HALE_WAIT;

        say_aloud(w, FT_BARK_BY_HALE, FT_BARK_HALE_FOUND);

        /* And steps aside: "after you". */
        if(!hale_step_toward(w, HALE_WAIT_TX, HALE_WAIT_TY)) w->hale_facing = FT_FACE_DOWN;
    }
}

/* He has just finished a step. Some tiles are the end of something. */
static void hale_landed(FtWorld* w) {
    const uint8_t tx = w->hale_mv.tx, ty = w->hale_mv.ty;

    switch((FtHalePhase)w->hale) {
    case FT_HALE_LEAD: {
        int32_t gx, gy;
        hale_lead_goal(w, &gx, &gy);
        if((int32_t)tx == gx && (int32_t)ty == gy) hale_lead_done(w);
        break;
    }

    case FT_HALE_HOME:
        if(tx == HALE_POST_TX && ty == HALE_POST_TY) {
            w->hale = (uint8_t)FT_HALE_POST;
            w->hale_facing = FT_FACE_DOWN;
            say_aloud(w, FT_BARK_BY_HALE, FT_BARK_HALE_POSTED);
        }
        break;

    default:
        break;
    }
}

static void hale_update(FtWorld* w, uint32_t dt_ms) {
    if(!ft_world_hale_here(w)) return;

    if(w->hale_mv.dx || w->hale_mv.dy) {
        if(step_advance(&w->hale_mv, dt_ms, ft_world_hale_step_ms(w))) hale_landed(w);
        return;
    }

    const int32_t px = w->mv.tx, py = w->mv.ty;
    const int32_t hx = w->hale_mv.tx, hy = w->hale_mv.ty;

    switch((FtHalePhase)w->hale) {
    case FT_HALE_LEAD: {
        /* Too far behind: he stops and looks back for you — and says so,
         * once per stop, so a player who has wandered off hears about it. */
        if(tiles_apart(hx, hy, px, py) > HALE_LEASH) {
            hale_face(w, px, py);
            if(!w->hale_waiting) {
                w->hale_waiting = true;
                say_aloud(w, FT_BARK_BY_HALE,
                          (w->hale_waits++ % 2u) ? FT_BARK_HALE_COMING : FT_BARK_HALE_KEEP_UP);
            }
            return;
        }
        w->hale_waiting = false;

        if(w->room != FT_ROOM_WELDHOME && w->room != FT_ROOM_APPROACH) return;

        int32_t gx, gy;
        hale_lead_goal(w, &gx, &gy);

        /* The last step of his way can be you. He will not walk through you
         * and you are waiting for him, so a doorway turns into a standoff —
         * the first player to reach the gate before him found exactly that.
         * Nearly there counts as there. */
        if(!hale_step_toward(w, gx, gy) && tiles_apart(hx, hy, gx, gy) <= 2) {
            hale_lead_done(w);
        }
        return;
    }

    case FT_HALE_WAIT:
        /* Walk far enough off and he takes it that you are going home, and
         * comes too — whether Wren is with you or not. */
        if(tiles_apart(hx, hy, px, py) >= HALE_FOLLOW_AT) {
            w->hale = (uint8_t)FT_HALE_FOLLOW;
            say_aloud(w, FT_BARK_BY_HALE, FT_BARK_HALE_WAIT);
        }
        return;

    case FT_HALE_FOLLOW: {
        /* Behind whoever is last in the line. */
        const int32_t lx = w->escort ? (int32_t)w->escort_mv.tx : px;
        const int32_t ly = w->escort ? (int32_t)w->escort_mv.ty : py;

        const bool leader_moving = w->escort ? (w->escort_mv.dx || w->escort_mv.dy) :
                                               ft_world_moving(w);
        const int32_t gap = tiles_apart(hx, hy, lx, ly);

        /* In his place, and the one in front is moving off: he steps into
         * the tile they are leaving, at walking pace. The conga in
         * ft_world_update does the same on the exact frame; this catches
         * him when he landed a moment too late for it, which otherwise left
         * him jogging to catch up and stopping, in bursts, all the way home. */
        if(gap == 1 && leader_moving) {
            const int8_t sx = (int8_t)(lx - hx), sy = (int8_t)(ly - hy);
            w->hale_mv.dx = sx;
            w->hale_mv.dy = sy;
            w->hale_mv.step_ms = 0;
            w->hale_hurry = false;
            w->hale_facing = (sx > 0) ? FT_FACE_RIGHT : (sx < 0) ? FT_FACE_LEFT :
                             (sy > 0) ? FT_FACE_DOWN : FT_FACE_UP;
            return;
        }

        /* Out of his place, so he jogs until he is back in it. Jogging only
         * when more than two back left him parked exactly two back, walking
         * at your pace one row over, all the way to the gate. */
        if(gap > 1 && hale_step_toward(w, lx, ly)) w->hale_hurry = true;
        return;
    }

    case FT_HALE_HOME:
        if(!hale_step_toward(w, HALE_POST_TX, HALE_POST_TY) &&
           hx == HALE_POST_TX && hy == HALE_POST_TY) {
            w->hale = (uint8_t)FT_HALE_POST;
            w->hale_facing = FT_FACE_DOWN;
        }
        return;

    case FT_HALE_POST:
    default:
        return;
    }
}

/* The player has changed room. Where does that leave him? */
static void hale_on_enter(FtWorld* w, uint8_t from, uint8_t to, uint8_t tx, uint8_t ty) {
    switch((FtHalePhase)w->hale) {
    case FT_HALE_LEAD:
        if(from == FT_ROOM_WELDHOME && to == FT_ROOM_APPROACH) {
            /* Through the door after him: he is just ahead, whether he got
             * there first or you did. */
            hale_place(w, FT_ROOM_APPROACH, HALE_IN_TX, HALE_IN_TY);
            w->hale_facing = FT_FACE_LEFT;
            say_aloud(w, FT_BARK_BY_HALE, FT_BARK_HALE_THIS_WAY);
        } else if(from == FT_ROOM_APPROACH && to == FT_ROOM_WELDHOME) {
            /* You turned back. He comes with you, and it will take talking
             * to him again to set off. */
            w->hale = (uint8_t)FT_HALE_HOME;
            hale_place(w, FT_ROOM_WELDHOME, tx, ty);
            say_aloud(w, FT_BARK_BY_HALE, FT_BARK_HALE_TURNED);
        } else if(from == FT_ROOM_APPROACH || from == FT_ROOM_WELDHOME) {
            /* Off somewhere he was not taking you. He goes back to the gate. */
            w->hale = (uint8_t)FT_HALE_POST;
            hale_place(w, FT_ROOM_WELDHOME, HALE_POST_TX, HALE_POST_TY);
            w->hale_facing = FT_FACE_DOWN;
        }
        break;

    case FT_HALE_FOLLOW:
        if(to == FT_ROOM_WELDHOME) {
            /* Home, with you. He walks back to his post from the door. */
            w->hale = (uint8_t)FT_HALE_HOME;
            hale_place(w, FT_ROOM_WELDHOME, tx, ty);
        } else {
            /* Down the pit, or off west: not his way. He goes back to it
             * and waits. */
            w->hale = (uint8_t)FT_HALE_WAIT;
            hale_place(w, FT_ROOM_APPROACH, HALE_WAIT_TX, HALE_WAIT_TY);
            w->hale_facing = FT_FACE_RIGHT;
        }
        break;

    case FT_HALE_HOME:
        if(to != FT_ROOM_WELDHOME) {
            w->hale = (uint8_t)FT_HALE_POST;
            hale_place(w, FT_ROOM_WELDHOME, HALE_POST_TX, HALE_POST_TY);
            w->hale_facing = FT_FACE_DOWN;
        }
        break;

    case FT_HALE_POST:
    case FT_HALE_WAIT:
    default:
        break;
    }
}

bool ft_world_terminal_near(const FtWorld* w) {
    int32_t dx, dy;
    const FtMap* m = ft_world_map(w);

    if(ft_map_tile(m, w->mv.tx, w->mv.ty) == FT_TILE_TERM) return true;

    facing_delta(w->facing, &dx, &dy);
    return ft_map_tile(m, (int32_t)w->mv.tx + dx, (int32_t)w->mv.ty + dy) == FT_TILE_TERM;
}

void ft_world_terminal_speaks(FtWorld* w) {
    int32_t dx, dy;
    const FtMap* m = ft_world_map(w);

    int32_t tx = w->mv.tx, ty = w->mv.ty;
    if(ft_map_tile(m, tx, ty) != FT_TILE_TERM) {
        facing_delta(w->facing, &dx, &dy);
        tx += dx;
        ty += dy;
    }
    if(ft_map_tile(m, tx, ty) != FT_TILE_TERM) return;

    w->bark_tx = (uint8_t)tx;
    w->bark_ty = (uint8_t)ty;
    say_aloud(w, FT_BARK_BY_TERMINAL, (uint8_t)(FT_BARK_HUSH + (w->hush_at % FT_BARK_HUSH_N)));
    w->hush_at++;
}

const char* ft_world_banner(const FtWorld* w) {
    return w->banner ? ft_room(w->room)->area : NULL;
}
