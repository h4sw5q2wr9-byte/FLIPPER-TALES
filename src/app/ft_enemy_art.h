/* Which sprite belongs to which enemy.
 *
 * One table, used by the battle screen and the overworld alike, so a foe is
 * the same thing in the corridor as it is in the fight. It is keyed on the
 * enemy id rather than on its attributes: attributes stopped being unique
 * the moment two enemies shared one, and picking art by them silently gave
 * the Gate Drone the Drift Beacon's body. */
#ifndef FT_ENEMY_ART_H
#define FT_ENEMY_ART_H

#include "../core/ft_data.h"
#include "ft_sprites.h"

static const uint16_t* ft_enemy_art(FtEnemyId id) {
    switch(id) {
    case FT_ENEMY_DRIFT_BEACON:  return FT_SPRITE_BEACON;
    case FT_ENEMY_SEALED_LOCK:   return FT_SPRITE_LOCK;
    case FT_ENEMY_SCRAP_CRAWLER: return FT_SPRITE_CRAWLER;
    case FT_ENEMY_RIME_SHELL:    return FT_SPRITE_RIME;
    case FT_ENEMY_GATE_DRONE:    return FT_SPRITE_DRONE;
    case FT_ENEMY_MAST_RELAY:    return FT_SPRITE_RELAY;
    case FT_ENEMY_NULL_FIELD:    return FT_SPRITE_NULLF;
    case FT_ENEMY_BLANK_WALL:    return FT_SPRITE_WALL;
    case FT_ENEMY_COLD_BOOTER:   return FT_SPRITE_BOOTER;
    case FT_ENEMY_STRAY_PACKET:
    default:                     return FT_SPRITE_PACKET;
    }
}

#endif /* FT_ENEMY_ART_H */
