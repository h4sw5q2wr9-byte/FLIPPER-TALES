/* The save file, as bytes.
 *
 * Pure core: this knows how a save is *laid out*, not where it lives. The app
 * layer hands the bytes to Storage and back. That split is what lets the whole
 * format — versioning, the checksum, every field's round trip — be tested on a
 * host machine instead of on an SD card.
 *
 * Everything is written little-endian byte by byte rather than by memcpy-ing
 * structs, so the layout does not silently change when a field is reordered or
 * the compiler pads differently.
 */
#ifndef FT_SAVE_H
#define FT_SAVE_H

#include "ft_data.h"
#include "ft_guide.h"
#include "ft_item.h"
#include "ft_progress.h"
#include "ft_quest.h"
#include "ft_signal.h"
#include "ft_types.h"
#include "ft_world.h"

#define FT_SAVE_MAGIC_0 'F'
#define FT_SAVE_MAGIC_1 'T'
#define FT_SAVE_MAGIC_2 'S'
#define FT_SAVE_MAGIC_3 'V'

/* Bumped whenever the layout changes. An older or newer file is refused
 * rather than misread: a garbled save is worse than a missing one. The one
 * exception is FT_SAVE_VERSION_PREV, which is this layout minus the byte
 * added last, and is read with that byte at its default. */
#define FT_SAVE_VERSION 11
#define FT_SAVE_VERSION_PREV 10

/* Header (4 magic + 1 version + 1 length) + payload + 4 checksum. Generous,
 * and asserted against the real encoded length by the tests. */
#define FT_SAVE_MAX_BYTES 128

typedef struct {
    FtStats         stats;
    FtGuide         guide;
    FtQuests        quests;
    FtPockets       pockets;

    /* Where the player stands, and where they come back to when downed. */
    uint8_t room, tx, ty;
    uint8_t save_room, save_tx, save_ty;

    uint8_t cleared[FT_CLEARED_BYTES];
    bool    coach;
    bool    sound;

    /* The talking voices, separately from sound. Stored in the same byte as
     * sound, as a "voices off" bit, so a save from before the setting
     * existed reads back with voices on — which is what it was. */
    bool    voices;

    /* Whether somebody is walking with you, and where they are standing.
     * Saving at a terminal half way home must not lose her. */
    bool    escort;
    uint8_t escort_tx, escort_ty;

    /* What has been shown to you, and where Hale is and what he is doing.
     * A pit found and then forgotten on reload is the same bug as a boss
     * that respawns. */
    uint8_t revealed;
    uint8_t hale, hale_room, hale_tx, hale_ty;

    /* What Wren called you. Version 10 had no name and still loads, as
     * FT_NAME_NONE: she names you next time she is with you. */
    uint8_t name;
} FtSaveData;

/* Returns the number of bytes written, or 0 if the buffer is too small. */
uint8_t ft_save_encode(const FtSaveData* d, uint8_t* out, uint8_t cap);

/* Returns false — leaving `out` untouched — for a buffer that is too short,
 * not ours, the wrong version, truncated, or corrupt. */
bool ft_save_decode(const uint8_t* in, uint8_t len, FtSaveData* out);

/* The world and the save, in both directions. Here rather than in the app so
 * the round trip is testable. */
void ft_save_from_world(const FtWorld* w, bool coach, bool sound, FtSaveData* d);
void ft_save_to_world(const FtSaveData* d, FtWorld* w, bool* coach, bool* sound);

#endif /* FT_SAVE_H */
