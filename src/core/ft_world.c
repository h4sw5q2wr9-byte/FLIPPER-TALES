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

/* ---- State ------------------------------------------------------------ */

const FtMap* ft_world_map(const FtWorld* w) {
    return ft_room(w->room)->map;
}

void ft_world_foot_tile(const FtWorld* w, int32_t* tx, int32_t* ty) {
    /* The feet, not the head: the avatar's lower rows are what collide, so
     * they are also what stands on a door. */
    *tx = (w->pos.x + FT_AVATAR_W / 2) / FT_TILE_PX;
    *ty = (w->pos.y + FT_AVATAR_H - 2) / FT_TILE_PX;
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

    ft_world_enter(w, 0, 3, 4);
}

void ft_world_enter(FtWorld* w, uint8_t room, uint8_t tx, uint8_t ty) {
    w->room = (room < ROOM_COUNT) ? room : 0u;

    /* Centre the avatar on the tile, and sit it so its feet land there. */
    w->pos.x = (int32_t)tx * FT_TILE_PX;
    w->pos.y = (int32_t)ty * FT_TILE_PX - (FT_AVATAR_H - FT_TILE_PX);

    w->facing = FT_FACE_DOWN;
    w->moving = false;
    w->step_ms = 0;
    w->walk_accum = 0;
    w->area_ms = 0;
}

void ft_world_walk(FtWorld* w, int8_t dx, int8_t dy, uint32_t dt_ms) {
    w->area_ms += dt_ms;

    if(dx == 0 && dy == 0) {
        w->moving = false;
        w->walk_accum = 0;
        return;
    }

    /* Facing follows intent, not movement: walking into a wall still turns
     * you, which is what makes striking a foe through a doorway work. */
    if(dx < 0) w->facing = FT_FACE_LEFT;
    else if(dx > 0) w->facing = FT_FACE_RIGHT;
    else if(dy < 0) w->facing = FT_FACE_UP;
    else if(dy > 0) w->facing = FT_FACE_DOWN;

    w->moving = true;
    w->step_ms += dt_ms;

    /* Sub-pixel accumulation, because at a 10ms tick the integer division
     * floors to zero. Rounding that up to one pixel per tick would run the
     * player at 100 px/s rather than FT_WALK_PX_PER_S. */
    w->walk_accum += dt_ms * FT_WALK_PX_PER_S;

    const int32_t move = (int32_t)(w->walk_accum / 1000u);
    if(move == 0) return;

    w->walk_accum -= (uint32_t)move * 1000u;
    w->pos = ft_map_move(ft_world_map(w), w->pos, dx * move, dy * move);
}

/* ---- Queries ----------------------------------------------------------- */

static void facing_delta(FtFacing f, int32_t* dx, int32_t* dy) {
    *dx = (f == FT_FACE_LEFT) ? -1 : (f == FT_FACE_RIGHT) ? 1 : 0;
    *dy = (f == FT_FACE_UP) ? -1 : (f == FT_FACE_DOWN) ? 1 : 0;
}

static int foe_at_tile(const FtWorld* w, int32_t tx, int32_t ty) {
    const FtRoom* r = ft_room(w->room);

    for(uint8_t i = 0; i < r->ent_count; i++) {
        if(r->ents[i].kind != FT_ENT_FOE) continue;
        if(ft_world_entity_gone(w, i)) continue;
        if((int32_t)r->ents[i].tx == tx && (int32_t)r->ents[i].ty == ty) return (int)i;
    }
    return -1;
}

int ft_world_foe_contact(const FtWorld* w) {
    int32_t tx, ty;
    ft_world_foot_tile(w, &tx, &ty);
    return foe_at_tile(w, tx, ty);
}

int ft_world_foe_ahead(const FtWorld* w) {
    int32_t tx, ty, dx, dy;
    ft_world_foot_tile(w, &tx, &ty);
    facing_delta(w->facing, &dx, &dy);

    return foe_at_tile(w, tx + dx, ty + dy);
}

const FtExit* ft_world_exit_under(const FtWorld* w) {
    int32_t tx, ty;
    ft_world_foot_tile(w, &tx, &ty);

    const FtRoom* r = ft_room(w->room);
    for(uint8_t i = 0; i < r->exit_count; i++) {
        if((int32_t)r->exits[i].tx == tx && (int32_t)r->exits[i].ty == ty) {
            return &r->exits[i];
        }
    }
    return NULL;
}

bool ft_world_terminal_near(const FtWorld* w) {
    int32_t tx, ty, dx, dy;
    ft_world_foot_tile(w, &tx, &ty);

    const FtMap* m = ft_world_map(w);
    if(ft_map_tile(m, tx, ty) == FT_TILE_TERM) return true;

    facing_delta(w->facing, &dx, &dy);
    return ft_map_tile(m, tx + dx, ty + dy) == FT_TILE_TERM;
}
