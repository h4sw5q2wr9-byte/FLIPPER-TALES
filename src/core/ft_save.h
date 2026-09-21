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
#include "ft_progress.h"
#include "ft_signal.h"
#include "ft_types.h"
#include "ft_world.h"

#define FT_SAVE_MAGIC_0 'F'
#define FT_SAVE_MAGIC_1 'T'
#define FT_SAVE_MAGIC_2 'S'
#define FT_SAVE_MAGIC_3 'V'

/* Bumped whenever the layout changes. An older or newer file is refused
 * rather than misread: a garbled save is worse than a missing one. */
#define FT_SAVE_VERSION 2

/* Header (4 magic + 1 version + 1 length) + payload + 4 checksum. Generous,
 * and asserted against the real encoded length by the tests. */
#define FT_SAVE_MAX_BYTES 128

typedef struct {
    FtStats         stats;
    FtLoadout       loadout;
    FtSignalLibrary lib;

    /* Where the player stands, and where they come back to when downed. */
    uint8_t room, tx, ty;
    uint8_t save_room, save_tx, save_ty;

    uint8_t cleared[FT_CLEARED_BYTES];
    bool    coach;
} FtSaveData;

/* Returns the number of bytes written, or 0 if the buffer is too small. */
uint8_t ft_save_encode(const FtSaveData* d, uint8_t* out, uint8_t cap);

/* Returns false — leaving `out` untouched — for a buffer that is too short,
 * not ours, the wrong version, truncated, or corrupt. */
bool ft_save_decode(const uint8_t* in, uint8_t len, FtSaveData* out);

/* The world and the save, in both directions. Here rather than in the app so
 * the round trip is testable. */
void ft_save_from_world(const FtWorld* w, bool coach, FtSaveData* d);
void ft_save_to_world(const FtSaveData* d, FtWorld* w, bool* coach);

#endif /* FT_SAVE_H */
