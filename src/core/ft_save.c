#include "ft_save.h"

/* ---- Primitives -------------------------------------------------------- */

static void put_u8(uint8_t* buf, uint8_t* at, uint8_t v) {
    buf[(*at)++] = v;
}

static void put_i16(uint8_t* buf, uint8_t* at, int16_t v) {
    const uint16_t u = (uint16_t)v;
    buf[(*at)++] = (uint8_t)(u & 0xFFu);
    buf[(*at)++] = (uint8_t)((u >> 8) & 0xFFu);
}

static uint8_t get_u8(const uint8_t* buf, uint8_t* at) {
    return buf[(*at)++];
}

static int16_t get_i16(const uint8_t* buf, uint8_t* at) {
    const uint16_t lo = buf[(*at)++];
    const uint16_t hi = buf[(*at)++];
    return (int16_t)(uint16_t)(lo | (uint16_t)(hi << 8));
}

/* FNV-1a. Not cryptography — it is here to catch a truncated write or a
 * half-erased sector, which is exactly what a game save on removable media
 * runs into. */
static uint32_t checksum(const uint8_t* b, uint8_t len) {
    uint32_t h = 2166136261u;
    for(uint8_t i = 0; i < len; i++) {
        h ^= b[i];
        h *= 16777619u;
    }
    return h;
}

/* ---- Layout ------------------------------------------------------------ */

#define HEADER_BYTES 6u
#define CHECK_BYTES  4u

/* Writes the payload and returns its length. Reading does the same walk in
 * the same order; keeping them adjacent is what stops the two drifting. */
static uint8_t write_payload(const FtSaveData* d, uint8_t* b) {
    uint8_t at = 0;

    put_i16(b, &at, d->stats.charge);
    put_i16(b, &at, d->stats.charge_max);
    put_i16(b, &at, d->stats.ram);
    put_i16(b, &at, d->stats.ram_max);
    put_i16(b, &at, d->stats.flash_used);
    put_i16(b, &at, d->stats.flash_max);
    put_i16(b, &at, d->stats.level);
    put_i16(b, &at, d->stats.xp);
    put_i16(b, &at, d->stats.orbs);
    for(uint8_t i = 0; i < FT_UP_COUNT; i++) put_u8(b, &at, d->stats.spent[i]);

    for(uint8_t i = 0; i < FT_MODULE_COUNT; i++) put_u8(b, &at, d->loadout.stacks[i]);

    put_i16(b, &at, (int16_t)d->guide.seen);

    for(uint8_t i = 0; i < FT_QUEST_BYTES; i++) put_u8(b, &at, d->quests.state[i]);
    for(uint8_t i = 0; i < FT_ITEM_COUNT; i++) put_u8(b, &at, d->pockets.count[i]);

    put_u8(b, &at, d->room);
    put_u8(b, &at, d->tx);
    put_u8(b, &at, d->ty);
    put_u8(b, &at, d->save_room);
    put_u8(b, &at, d->save_tx);
    put_u8(b, &at, d->save_ty);

    for(uint8_t i = 0; i < FT_CLEARED_BYTES; i++) put_u8(b, &at, d->cleared[i]);

    put_u8(b, &at, d->coach ? 1u : 0u);

    put_u8(b, &at, d->escort ? 1u : 0u);
    put_u8(b, &at, d->escort_tx);
    put_u8(b, &at, d->escort_ty);

    return at;
}

static void read_payload(const uint8_t* b, FtSaveData* d) {
    uint8_t at = 0;

    d->stats.charge = get_i16(b, &at);
    d->stats.charge_max = get_i16(b, &at);
    d->stats.ram = get_i16(b, &at);
    d->stats.ram_max = get_i16(b, &at);
    d->stats.flash_used = get_i16(b, &at);
    d->stats.flash_max = get_i16(b, &at);
    d->stats.level = get_i16(b, &at);
    d->stats.xp = get_i16(b, &at);
    d->stats.orbs = get_i16(b, &at);
    for(uint8_t i = 0; i < FT_UP_COUNT; i++) d->stats.spent[i] = get_u8(b, &at);

    for(uint8_t i = 0; i < FT_MODULE_COUNT; i++) d->loadout.stacks[i] = get_u8(b, &at);

    d->guide.seen = (uint16_t)get_i16(b, &at);

    for(uint8_t i = 0; i < FT_QUEST_BYTES; i++) d->quests.state[i] = get_u8(b, &at);
    for(uint8_t i = 0; i < FT_ITEM_COUNT; i++) d->pockets.count[i] = get_u8(b, &at);

    d->room = get_u8(b, &at);
    d->tx = get_u8(b, &at);
    d->ty = get_u8(b, &at);
    d->save_room = get_u8(b, &at);
    d->save_tx = get_u8(b, &at);
    d->save_ty = get_u8(b, &at);

    for(uint8_t i = 0; i < FT_CLEARED_BYTES; i++) d->cleared[i] = get_u8(b, &at);

    d->coach = get_u8(b, &at) != 0u;

    d->escort = get_u8(b, &at) != 0u;
    d->escort_tx = get_u8(b, &at);
    d->escort_ty = get_u8(b, &at);
}

/* The payload length, which the header carries so a decode can check the file
 * is the size it claims before trusting a byte of it. */
static uint8_t payload_bytes(void) {
    return (uint8_t)(9u * 2u + FT_UP_COUNT        /* stats, orbs, where they went */
                     + FT_MODULE_COUNT            /* loadout */
                     + 2u                         /* field guide */
                     + FT_QUEST_BYTES             /* quests */
                     + FT_ITEM_COUNT              /* pockets */
                     + 6u                         /* position and save point */
                     + FT_CLEARED_BYTES + 1u      /* flags */
                     + 3u);                       /* whoever is with you */
}

/* ---- Encode and decode ------------------------------------------------- */

uint8_t ft_save_encode(const FtSaveData* d, uint8_t* out, uint8_t cap) {
    const uint8_t plen = payload_bytes();
    const uint8_t total = (uint8_t)(HEADER_BYTES + plen + CHECK_BYTES);

    if(out == NULL || cap < total) return 0u;

    uint8_t at = 0;
    put_u8(out, &at, FT_SAVE_MAGIC_0);
    put_u8(out, &at, FT_SAVE_MAGIC_1);
    put_u8(out, &at, FT_SAVE_MAGIC_2);
    put_u8(out, &at, FT_SAVE_MAGIC_3);
    put_u8(out, &at, FT_SAVE_VERSION);
    put_u8(out, &at, plen);

    const uint8_t written = write_payload(d, out + at);
    at = (uint8_t)(at + written);

    /* Over the header as well as the payload: a file whose version byte was
     * flipped should fail the checksum, not silently decode as v0. */
    const uint32_t h = checksum(out, at);
    put_u8(out, &at, (uint8_t)(h & 0xFFu));
    put_u8(out, &at, (uint8_t)((h >> 8) & 0xFFu));
    put_u8(out, &at, (uint8_t)((h >> 16) & 0xFFu));
    put_u8(out, &at, (uint8_t)((h >> 24) & 0xFFu));

    return at;
}

bool ft_save_decode(const uint8_t* in, uint8_t len, FtSaveData* out) {
    if(in == NULL || out == NULL) return false;
    if(len < HEADER_BYTES + CHECK_BYTES) return false;

    if(in[0] != FT_SAVE_MAGIC_0 || in[1] != FT_SAVE_MAGIC_1 ||
       in[2] != FT_SAVE_MAGIC_2 || in[3] != FT_SAVE_MAGIC_3) {
        return false;
    }
    if(in[4] != FT_SAVE_VERSION) return false;

    const uint8_t plen = in[5];
    if(plen != payload_bytes()) return false;

    const uint8_t total = (uint8_t)(HEADER_BYTES + plen + CHECK_BYTES);
    if(len < total) return false;

    const uint32_t want = checksum(in, (uint8_t)(HEADER_BYTES + plen));
    const uint8_t* c = in + HEADER_BYTES + plen;
    const uint32_t got = (uint32_t)c[0] | ((uint32_t)c[1] << 8) |
                         ((uint32_t)c[2] << 16) | ((uint32_t)c[3] << 24);
    if(want != got) return false;

    read_payload(in + HEADER_BYTES, out);
    return true;
}

/* ---- The world, both ways ---------------------------------------------- */

void ft_save_from_world(const FtWorld* w, bool coach, FtSaveData* d) {
    d->stats = w->stats;
    d->loadout = w->loadout;
    d->guide = w->guide;
    d->quests = w->quests;
    d->pockets = w->pockets;

    d->room = w->room;
    d->tx = w->mv.tx;
    d->ty = w->mv.ty;

    d->save_room = w->save_room;
    d->save_tx = w->save_tx;
    d->save_ty = w->save_ty;

    for(uint8_t i = 0; i < FT_CLEARED_BYTES; i++) d->cleared[i] = w->cleared[i];
    d->coach = coach;

    d->escort = w->escort;
    d->escort_tx = w->escort_mv.tx;
    d->escort_ty = w->escort_mv.ty;
}

void ft_save_to_world(const FtSaveData* d, FtWorld* w, bool* coach) {
    ft_world_init(w);

    w->stats = d->stats;
    w->loadout = d->loadout;
    w->guide = d->guide;
    w->quests = d->quests;
    w->pockets = d->pockets;

    /* Cleared entities are restored *before* entering, because entering is
     * what decides which foes spawn. Loading and then walking into a foe you
     * already beat is the classic version of this bug. */
    for(uint8_t i = 0; i < FT_CLEARED_BYTES; i++) w->cleared[i] = d->cleared[i];

    w->save_room = d->save_room;
    w->save_tx = d->save_tx;
    w->save_ty = d->save_ty;

    ft_world_enter(w, d->room, d->tx, d->ty);

    /* After entering, because entering is what places her beside you. */
    w->escort = d->escort;
    if(w->escort) {
        w->escort_mv.tx = d->escort_tx;
        w->escort_mv.ty = d->escort_ty;
        w->escort_mv.dx = 0;
        w->escort_mv.dy = 0;
        w->escort_mv.step_ms = 0;
    }

    if(coach) *coach = d->coach;
}
