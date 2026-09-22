#include "ft_item.h"

static const FtItemDef FT_ITEMS[FT_ITEM_COUNT] = {
    /* Grows on the scrub that comes up through the Carrier's floor. Common,
     * small, and the reason walking past a tree is worth the two seconds. */
    [FT_ITEM_APPLE]  = {"Apple",  "+5 HP",  5, 0},

    /* Somebody packed this. You find them where people were. */
    [FT_ITEM_RATION] = {"Ration", "+12 HP", 12, 0},

    /* Not food. The thing you want when NFC has priced itself out. */
    [FT_ITEM_CELL]   = {"Cell",   "+4 MP",  0, 4},
};

const FtItemDef* ft_item_def(FtItemId id) {
    return &FT_ITEMS[(id < FT_ITEM_COUNT) ? id : 0];
}

void ft_pockets_init(FtPockets* p) {
    for(uint8_t i = 0; i < FT_ITEM_COUNT; i++) p->count[i] = 0u;
}

uint8_t ft_pockets_used(const FtPockets* p) {
    uint8_t n = 0;
    for(uint8_t i = 0; i < FT_ITEM_COUNT; i++) n = (uint8_t)(n + p->count[i]);
    return n;
}

bool ft_pockets_full(const FtPockets* p) {
    return ft_pockets_used(p) >= FT_POCKET_MAX;
}

bool ft_pockets_add(FtPockets* p, FtItemId id) {
    if(id >= FT_ITEM_COUNT) return false;
    if(ft_pockets_full(p)) return false;

    p->count[id]++;
    return true;
}

bool ft_pockets_take(FtPockets* p, FtItemId id) {
    if(id >= FT_ITEM_COUNT) return false;
    if(p->count[id] == 0u) return false;

    p->count[id]--;
    return true;
}

uint8_t ft_pockets_count(const FtPockets* p, FtItemId id) {
    return (id < FT_ITEM_COUNT) ? p->count[id] : 0u;
}

uint8_t ft_pockets_kinds(const FtPockets* p) {
    uint8_t n = 0;
    for(uint8_t i = 0; i < FT_ITEM_COUNT; i++) {
        if(p->count[i] > 0u) n++;
    }
    return n;
}

FtItemId ft_pockets_nth(const FtPockets* p, uint8_t n) {
    uint8_t at = 0;
    for(uint8_t i = 0; i < FT_ITEM_COUNT; i++) {
        if(p->count[i] == 0u) continue;
        if(at == n) return (FtItemId)i;
        at++;
    }
    return FT_ITEM_COUNT;
}

bool ft_item_useful(FtItemId id, int16_t hp, int16_t hp_max, int16_t mp, int16_t mp_max) {
    const FtItemDef* d = ft_item_def(id);

    /* Eating at full health is throwing it away, and the one thing a limited
     * pocket must never do is let you waste a slot by pressing OK twice. */
    if(d->heal > 0 && hp < hp_max) return true;
    if(d->ram > 0 && mp < mp_max) return true;

    return false;
}
