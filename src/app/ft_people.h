/* Who is who, in pixels.
 *
 * Everybody in the overworld is drawn from here, so "important people look
 * important" is one table rather than a rule each caller remembers.
 *
 * The rule the art follows (tools/gensprites.py has the drawings):
 *
 *   ORDINARY people are short and bare-headed, and start three rows down.
 *   IMPORTANT people are a head taller and carry something on the
 *   silhouette an ordinary one never does — a hood, a helmet.
 *
 * So you can tell across the room who is worth walking to. */
#ifndef FT_PEOPLE_H
#define FT_PEOPLE_H

#include "../core/ft_quest.h"
#include "ft_sprites.h"

/* The giver of this quest. Named characters get their own body; anybody the
 * table does not know falls back to an ordinary villager, which is the right
 * answer for a crowd. */
static const uint16_t* ft_person_art(FtQuestId id) {
    switch(id) {
    case FT_QUEST_CLEAN_RUN: return FT_SPRITE_KEEPER;
    case FT_QUEST_WREN:      return FT_SPRITE_WARDEN;
    case FT_QUEST_RIVET:     return FT_SPRITE_RIVET;
    case FT_QUEST_LEDGER:    return FT_SPRITE_LEDGER;
    default:                 return FT_SPRITE_FOLK;
    }
}

#endif /* FT_PEOPLE_H */
