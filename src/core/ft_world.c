#include "ft_world.h"

#include "../app/ft_maps.h"

/* ---- Content ---------------------------------------------------------- */

/* One visible foe means a whole group in battle, which is what makes the
 * broadcast-versus-contact choice matter out here too. */
static const FtRoster FT_ROSTERS[] = {
    {1, {FT_ENEMY_STRAY_PACKET, 0, 0}},
    {2, {FT_ENEMY_STRAY_PACKET, FT_ENEMY_STRAY_PACKET, 0}},
    {2, {FT_ENEMY_DRIFT_BEACON, FT_ENEMY_STRAY_PACKET, 0}},
    {3, {FT_ENEMY_STRAY_PACKET, FT_ENEMY_DRIFT_BEACON, FT_ENEMY_SEALED_LOCK}},
};
#define ROSTER_COUNT (sizeof(FT_ROSTERS) / sizeof(FT_ROSTERS[0]))

/* [1] Wake: a terminal and the way out. No foe — the first room teaches
 * walking and saving, nothing else. */
static const FtExit CB1_EXITS[] = {
    {15, 3, 1, 1, 2},
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
    {17, 8, 0, 2, 3},
};
static const FtEntity CB4_ENTS[] = {
    {FT_ENT_FOE, 13, 8, 3},
};

static const FtRoom FT_ROOMS[] = {
    {&FT_MAP_CB1, CB1_EXITS, 1, NULL, 0},
    {&FT_MAP_CB2, CB2_EXITS, 2, CB2_ENTS, 1},
    {&FT_MAP_CB3, CB3_EXITS, 2, CB3_ENTS, 2},
    {&FT_MAP_CB4, CB4_EXITS, 2, CB4_ENTS, 1},
};
#define ROOM_COUNT (sizeof(FT_ROOMS) / sizeof(FT_ROOMS[0]))

const FtRoom* ft_room(uint8_t index) {
    return &FT_ROOMS[index < ROOM_COUNT ? index : 0];
}

uint8_t ft_room_count(void) {
    return (uint8_t)ROOM_COUNT;
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

    /* The avatar is taller than a tile, so it sits back to stand on one. */
    p.y -= (FT_AVATAR_H - FT_TILE_PX);

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

    w->mv.tx = tx;
    w->mv.ty = ty;
    w->mv.dx = 0;
    w->mv.dy = 0;
    w->mv.step_ms = 0;

    w->facing = FT_FACE_DOWN;
    w->walk_ms = 0;
    w->area_ms = 0;
    w->arrived = false;

    /* Foes start where the room says, and are alive unless already beaten. */
    const FtRoom* r = ft_room(w->room);
    for(uint8_t i = 0; i < FT_MAX_ROOM_ENTS; i++) {
        FtFoeState* f = &w->foes[i];

        f->alive = false;
        f->alert = false;
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
    ft_siglib_init(&w->lib);

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
    const int32_t dx = (int32_t)w->mv.tx - (int32_t)f->mv.tx;
    const int32_t dy = (int32_t)w->mv.ty - (int32_t)f->mv.ty;

    int8_t sx = 0, sy = 0;

    if(alert) {
        /* Close the larger gap first, so a chase reads as deliberate rather
         * than as a diagonal stagger. A little jitter keeps several chasers
         * from stacking into one column. */
        const bool prefer_x = (abs32(dx) >= abs32(dy));
        const bool jitter = (foe_rand(f) % 5u) == 0u;

        if(prefer_x != jitter) {
            if(dx) sx = (dx > 0) ? 1 : -1;
            else if(dy) sy = (dy > 0) ? 1 : -1;
        } else {
            if(dy) sy = (dy > 0) ? 1 : -1;
            else if(dx) sx = (dx > 0) ? 1 : -1;
        }
    } else {
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

void ft_world_update(FtWorld* w, int8_t dx, int8_t dy, uint32_t dt_ms) {
    const FtMap* map = ft_world_map(w);

    w->area_ms += dt_ms;
    w->arrived = false;

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
         * turn and strike something you cannot walk into. */
        if(step_target_free(map, w->mv.tx, w->mv.ty, dx, dy)) {
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
            w->foes[i].alert = true;
        }
    }

    for(uint8_t i = 0; i < room->ent_count && i < FT_MAX_ROOM_ENTS; i++) {
        FtFoeState* f = &w->foes[i];
        if(!f->alive) continue;

        /* Every walker steps and thinks on its own clock. */
        for(uint8_t m = 0; m < f->count; m++) {
            FtFoeWalker* k = &f->w[m];

            if(k->mv.dx || k->mv.dy) {
                step_advance(&k->mv, dt_ms, FT_FOE_STEP_MS);
                continue;
            }

            k->think_ms += dt_ms;
            if(k->think_ms < FT_FOE_THINK_MS) continue;

            k->think_ms = 0;
            foe_think(w, k, f->alert, map);
        }
    }
}

/* ---- Queries ----------------------------------------------------------- */

static void facing_delta(FtFacing f, int32_t* dx, int32_t* dy) {
    *dx = (f == FT_FACE_LEFT) ? -1 : (f == FT_FACE_RIGHT) ? 1 : 0;
    *dy = (f == FT_FACE_UP) ? -1 : (f == FT_FACE_DOWN) ? 1 : 0;
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
