/* Pockets: what you are carrying, and what it does.
 *
 * The only healing in this game used to be a terminal you had to walk back
 * to and Protect, which restores two. That makes every fight a one-way trip
 * and every wrong turn a reload. Food fixes it without making the game
 * easier in the wrong place: you have to have *found* it, and you can only
 * carry so much, so topping up is a thing you plan rather than a button.
 *
 * Pure core like the rest: no allocation, no stdio, and the whole economy is
 * host-testable. */
#ifndef FT_ITEM_H
#define FT_ITEM_H

#include "ft_types.h"

typedef enum {
    FT_ITEM_APPLE = 0, /* the common one: grows, heals a little */
    FT_ITEM_RATION,    /* the rare one: heals properly */
    FT_ITEM_CELL,      /* MP, for a long fight that has run dry */
    FT_ITEM_COUNT
} FtItemId;

/* How much you can carry at once, across every kind.
 *
 * The cap is the whole design. Without one, food stops being a decision and
 * becomes a chore you do before every fight. */
#define FT_POCKET_MAX 6

typedef struct {
    uint8_t count[FT_ITEM_COUNT];
} FtPockets;

typedef struct {
    const char* name;  /* at most 10 characters: it shares a row */
    const char* what;  /* what it does, at most 20 */
    int16_t     heal;  /* HP restored */
    int16_t     ram;   /* MP restored */
} FtItemDef;

const FtItemDef* ft_item_def(FtItemId id);

void    ft_pockets_init(FtPockets* p);
uint8_t ft_pockets_used(const FtPockets* p);
bool    ft_pockets_full(const FtPockets* p);

/* Returns false when there is no room, and changes nothing. */
bool ft_pockets_add(FtPockets* p, FtItemId id);

/* Returns false when you are not carrying one. */
bool ft_pockets_take(FtPockets* p, FtItemId id);

uint8_t ft_pockets_count(const FtPockets* p, FtItemId id);

/* How many *kinds* you are carrying, and the nth of them. Together these walk
 * a picker without it having to know the item table. */
uint8_t  ft_pockets_kinds(const FtPockets* p);
FtItemId ft_pockets_nth(const FtPockets* p, uint8_t n);

/* Would using this do anything right now? A full-HP player eating an apple
 * has thrown it away, so the picker greys it out rather than letting them. */
bool ft_item_useful(FtItemId id, int16_t hp, int16_t hp_max, int16_t mp, int16_t mp_max);

#endif /* FT_ITEM_H */
