/* Which sprite belongs to which enemy.
 *
 * One table, used by the battle screen and the overworld alike, so a foe is
 * the same thing in the corridor as it is in the fight. It is keyed on the
 * enemy id rather than on its attributes: attributes stopped being unique
 * the moment two enemies shared one, and picking art by them silently gave
 * the Ticket Drone the Lamplighter's body. */
#ifndef FT_ENEMY_ART_H
#define FT_ENEMY_ART_H

#include "../core/ft_data.h"
#include "ft_sprites.h"

static const uint16_t* ft_enemy_art(FtEnemyId id) {
    switch(id) {
    case FT_ENEMY_LAMPLIGHTER:   return FT_SPRITE_LAMP;
    case FT_ENEMY_CURFEW_LOCK:   return FT_SPRITE_CURFEW;
    case FT_ENEMY_SWEEPER:       return FT_SPRITE_SWEEPER;
    case FT_ENEMY_CHILLER:       return FT_SPRITE_CHILLER;
    case FT_ENEMY_TICKET_DRONE:  return FT_SPRITE_TICKET;
    case FT_ENEMY_LOUDHAILER:    return FT_SPRITE_HAILER;
    case FT_ENEMY_SHUSHER:       return FT_SPRITE_SHUSHER;
    case FT_ENEMY_QUEUE_BARRIER: return FT_SPRITE_BARRIER;
    case FT_ENEMY_NIGHT_SHIFT:   return FT_SPRITE_NIGHTSHIFT;
    case FT_ENEMY_ECHO:          return FT_SPRITE_ECHO_FOE;
    case FT_ENEMY_PARCEL_RUNNER:
    default:                     return FT_SPRITE_RUNNER;
    }
}

#endif /* FT_ENEMY_ART_H */
