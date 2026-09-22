#include "ft_world.h"

#include "../app/ft_maps.h"

/* ---- Content ---------------------------------------------------------- */

/* One visible foe means a whole group in battle, which is what makes the
 * broadcast-versus-contact choice matter out here too. */
static const FtRoster FT_ROSTERS[] = {
    /* [0-3] The prologue, one lesson at a time. */
    {1, {FT_ENEMY_STRAY_PACKET, 0, 0}},
    {2, {FT_ENEMY_STRAY_PACKET, FT_ENEMY_STRAY_PACKET, 0}},
    {2, {FT_ENEMY_DRIFT_BEACON, FT_ENEMY_STRAY_PACKET, 0}},
    {3, {FT_ENEMY_STRAY_PACKET, FT_ENEMY_DRIFT_BEACON, FT_ENEMY_SEALED_LOCK}},

    /* [4-8] One per area. Each pairs its own enemy with something from the
     * prologue, so a fight is the new idea plus a thing you already know how
     * to handle rather than two puzzles at once. */
    {2, {FT_ENEMY_SCRAP_CRAWLER, FT_ENEMY_STRAY_PACKET, 0}},           /* Scrapline */
    {3, {FT_ENEMY_SCRAP_CRAWLER, FT_ENEMY_SCRAP_CRAWLER,
         FT_ENEMY_STRAY_PACKET}},                                      /* Scrapline, heavier */
    {2, {FT_ENEMY_RIME_SHELL, FT_ENEMY_STRAY_PACKET, 0}},              /* Cold Storage */
    {2, {FT_ENEMY_GATE_DRONE, FT_ENEMY_SEALED_LOCK, 0}},               /* Turnstile */
    {2, {FT_ENEMY_MAST_RELAY, FT_ENEMY_DRIFT_BEACON, 0}},              /* Signal Hill */
    /* The Null Field is the whole fight: ENCRYPTED, a jammer, and both its
     * attacks unguardable-for-capture. Pairing it with a FAST Crawler as
     * well put the Deadzone at 28% for an average player — two stars, no
     * room to learn either. A plain Packet gives it a partner without
     * giving it a second mechanic. */
    {2, {FT_ENEMY_NULL_FIELD, FT_ENEMY_STRAY_PACKET, 0}},              /* Deadzone */

    /* [10-11] The shape-changers. Order matters for both: the wall stands in
     * slot 0 so it is literally in front, and the sleeper sits in the last
     * slot so clearing the row is what wakes it. */
    {3, {FT_ENEMY_BLANK_WALL, FT_ENEMY_DRIFT_BEACON, FT_ENEMY_STRAY_PACKET}},
    {3, {FT_ENEMY_STRAY_PACKET, FT_ENEMY_SCRAP_CRAWLER, FT_ENEMY_COLD_BOOTER}},
};
#define ROSTER_COUNT (sizeof(FT_ROSTERS) / sizeof(FT_ROSTERS[0]))

/* Kept in step with the table by the compiler rather than by memory. */
typedef char ft_roster_count_matches[(ROSTER_COUNT == FT_ROSTER_COUNT) ? 1 : -1];

/* [1] Wake: a terminal, the way out, and the one person who asks you for
 * anything. No foe — the first room teaches walking, saving and talking,
 * nothing else. */
static const FtExit CB1_EXITS[] = {
    {15, 3, 1, 1, 2},
};
static const FtEntity CB1_ENTS[] = {
    /* Two tiles from where you wake up, off the line to the door, so you
     * meet them by choice rather than by walking into them. */
    {FT_ENT_NPC, 7, 2, FT_QUEST_CLEAN_RUN},
};

/* [2] Boot Corridor: the first encounter, placed far enough right that it is
 * seen well before it is reached. */
static const FtExit CB2_EXITS[] = {
    {0, 2, 0, 14, 3},
    {19, 4, 2, 1, 2},
};
static const FtEntity CB2_ENTS[] = {
    {FT_ENT_FOE, 13, 3, 0},
};

/* [3] The Drop: upper shelf, ladder down, terminal on the lower floor. */
static const FtExit CB3_EXITS[] = {
    {0, 2, 1, 18, 4},
    {17, 10, 3, 1, 2},
};
static const FtEntity CB3_ENTS[] = {
    {FT_ENT_FOE, 9, 2, 1},
    {FT_ENT_FOE, 6, 11, 2},
};

/* [4] Cold Gate: a group standing in the exit, the way an area ends. */
static const FtExit CB4_EXITS[] = {
    {0, 2, 2, 16, 10},
    {17, 8, 4, 1, 2}, /* out of the prologue and into the concept slices */
};
static const FtEntity CB4_ENTS[] = {
    {FT_ENT_FOE, 13, 8, 3},
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
    {0, 2, 3, 16, 8},
    {21, 8, 5, 1, 2},
};
static const FtEntity SL1_ENTS[] = {
    {FT_ENT_FOE, 8, 6, 4},
    {FT_ENT_FOE, 11, 3, 5},
};

/* [6] Cold Storage. A sealed cell in the middle of a frosted floor, with no
 * visible way in: RFID reads through walls, so the module finds the door
 * that was never drawn. */
static const FtExit CS1_EXITS[] = {
    {0, 2, 4, 20, 8},
    {21, 8, 6, 1, 2},
};
static const FtEntity CS1_ENTS[] = {
    {FT_ENT_FOE, 17, 2, 6},
    {FT_ENT_FOE, 4, 7, 6},
};

/* [7] The Turnstile. Ranks of locked ports across the only route through.
 * This is the wall the chapter is named after, and you can walk up to it
 * long before the iButton exists. */
static const FtExit TS1_EXITS[] = {
    {0, 2, 5, 20, 8},
    {21, 8, 7, 1, 2},
};
static const FtEntity TS1_ENTS[] = {
    {FT_ENT_FOE, 15, 4, 7},
    {FT_ENT_FOE, 4, 2, 10}, /* the wall, in a room about things in the way */
};

/* [8] Signal Hill. Pylons and dead cable runs on a terrace, with the ladder
 * up sitting behind a run that carries nothing: GPIO powers what is already
 * there. */
static const FtExit SH1_EXITS[] = {
    {0, 2, 6, 20, 8},
    {21, 8, 8, 1, 2},
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
    {0, 2, 7, 20, 8},
    {21, 8, 0, 3, 4},
};
static const FtEntity DZ1_ENTS[] = {
    {FT_ENT_FOE, 9, 4, 9},
    {FT_ENT_FOE, 17, 7, 11}, /* the sleeper, at the far end */
};

static const FtRoom FT_ROOMS[] = {
    {&FT_MAP_CB1, CB1_EXITS, 1, CB1_ENTS, 1},
    {&FT_MAP_CB2, CB2_EXITS, 2, CB2_ENTS, 1},
    {&FT_MAP_CB3, CB3_EXITS, 2, CB3_ENTS, 2},
    {&FT_MAP_CB4, CB4_EXITS, 2, CB4_ENTS, 1},
    {&FT_MAP_SL1, SL1_EXITS, 2, SL1_ENTS, 2},
    {&FT_MAP_CS1, CS1_EXITS, 2, CS1_ENTS, 2},
    {&FT_MAP_TS1, TS1_EXITS, 2, TS1_ENTS, 2},
    {&FT_MAP_SH1, SH1_EXITS, 2, SH1_ENTS, 2},
    {&FT_MAP_DZ1, DZ1_EXITS, 2, DZ1_ENTS, 2},
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

void ft_world_enter(FtWorld* w, uint8_t room, uint8_t tx, uint8_t ty) {
    w->room = (room < ft_room_count()) ? room : 0u;

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
        if(fresh->ents[i].kind != FT_ENT_FOE) continue;

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
    w->arrived = false;
    w->ambushed = false;

    /* Arriving somewhere is the only thing a quest watches for on its own;
     * everything else it hears about from the app. */
    ft_quest_enter_room(&w->quests, w->room);

    /* Foes start where the room says, and are alive unless already beaten. */
    const FtRoom* r = ft_room(w->room);
    for(uint8_t i = 0; i < FT_MAX_ROOM_ENTS; i++) {
        FtFoeState* f = &w->foes[i];

        f->alive = false;
        f->alert = false;
        f->notice_ms = 0;
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

    ft_loadout_init(&w->loadout);
    ft_stats_init(&w->stats);
    ft_guide_init(&w->guide);
    ft_quests_init(&w->quests);

    /* World stats are authoritative and carry the loadout's bonuses, because a
     * battle copies them in rather than building its own. */
    const FtLoadoutEffects fx = ft_loadout_effects(&w->loadout);
    w->stats.charge_max = (int16_t)(w->stats.charge_max + fx.charge_max_bonus);
    w->stats.charge = w->stats.charge_max;
    w->stats.flash_used = fx.flash_used;

    /* The first room's terminal is where a new run starts and, until you save
     * somewhere else, where being downed puts you back. */
    w->save_room = 0;
    w->save_tx = 5;
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
static bool foe_spots(const FtWorld* w, const FtFoeWalker* k) {
    const int32_t dx = (int32_t)w->mv.tx - (int32_t)k->mv.tx;
    const int32_t dy = (int32_t)w->mv.ty - (int32_t)k->mv.ty;
    return (abs32(dx) + abs32(dy)) <= FT_FOE_ALERT;
}

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

void ft_world_update(FtWorld* w, int8_t dx, int8_t dy, uint32_t dt_ms) {
    const FtMap* map = ft_world_map(w);

    w->area_ms += dt_ms;
    w->arrived = false;
    w->ambushed = false;

    /* --- player --- */
    if(ft_world_moving(w)) {
        w->walk_ms += dt_ms;
        w->arrived = step_advance(&w->mv, dt_ms, FT_STEP_MS);
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
        if(step_target_free(map, w->mv.tx, w->mv.ty, dx, dy) &&
           npc_at_tile(w, (int32_t)w->mv.tx + dx, (int32_t)w->mv.ty + dy) < 0) {
            w->mv.dx = dx;
            w->mv.dy = dy;
            w->mv.step_ms = 0;
            w->walk_ms += dt_ms;
        }
    }

    /* --- foes --- */
    const FtRoom* room = ft_room(w->room);

    /* Alert is shared: one of them noticing you brings the whole room. A group
     * that reacts individually reads as three oblivious animals rather than
     * something that has seen you. */
    bool any_spotted = false;
    for(uint8_t i = 0; i < room->ent_count && i < FT_MAX_ROOM_ENTS; i++) {
        const FtFoeState* f = &w->foes[i];
        if(!f->alive) continue;

        for(uint8_t m = 0; m < f->count; m++) {
            if(foe_spots(w, &f->w[m])) any_spotted = true;
        }
    }
    if(any_spotted) {
        for(uint8_t i = 0; i < room->ent_count && i < FT_MAX_ROOM_ENTS; i++) {
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
        if(r->ents[i].kind != FT_ENT_NPC) continue;
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

const FtExit* ft_world_exit_under(const FtWorld* w) {
    const FtRoom* r = ft_room(w->room);

    for(uint8_t i = 0; i < r->exit_count; i++) {
        if(r->exits[i].tx == w->mv.tx && r->exits[i].ty == w->mv.ty) return &r->exits[i];
    }
    return NULL;
}

bool ft_world_terminal_near(const FtWorld* w) {
    int32_t dx, dy;
    const FtMap* m = ft_world_map(w);

    if(ft_map_tile(m, w->mv.tx, w->mv.ty) == FT_TILE_TERM) return true;

    facing_delta(w->facing, &dx, &dy);
    return ft_map_tile(m, (int32_t)w->mv.tx + dx, (int32_t)w->mv.ty + dy) == FT_TILE_TERM;
}
