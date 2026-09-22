/* What the game sounds like, as data.
 *
 * The Flipper's speaker is a piezo buzzer and the official HAL offers one
 * thing: furi_hal_speaker_start(frequency, volume). So this is a chiptune
 * voice — one note at a time — and everything here is a list of notes.
 *
 * Core, like everything beside it, so the tunes are data a test can walk and
 * not a pile of furi calls scattered through the app. src/app owns the
 * speaker; this owns what comes out of it.
 *
 * Frequencies are whole hertz. The HAL takes a float and the app casts on the
 * way out, because core stays integer (DESIGN.md 2.2). */
#ifndef FT_AUDIO_H
#define FT_AUDIO_H

#include "ft_types.h"

/* Silence, for a rest inside a phrase. */
#define FT_NOTE_REST 0

typedef struct {
    uint16_t hz;
    uint16_t ms;
} FtNote;

typedef enum {
    FT_SFX_MOVE = 0,   /* a step onto a new tile */
    FT_SFX_PICK,       /* took something */
    FT_SFX_DENY,       /* an action that will not happen */
    FT_SFX_TALK,       /* somebody said something */
    FT_SFX_SWING,      /* your attack goes out */
    FT_SFX_HIT,        /* it lands */
    FT_SFX_HURT,       /* one lands on you */
    FT_SFX_JAM,        /* a guard that worked */
    FT_SFX_PERFECT,    /* a guard that was frame-perfect */
    FT_SFX_DEFLECT,    /* and sent it back */
    FT_SFX_LEVEL,      /* a level, an orb, a reward */
    FT_SFX_WIN,
    FT_SFX_LOSE,
    FT_SFX_ENCOUNTER,  /* the wipe into a fight */
    FT_SFX_COUNT
} FtSfxId;

/* The notes for a cue, and how many. Never NULL for a valid id. */
const FtNote* ft_sfx(FtSfxId id, uint8_t* count);

/* The longest any cue runs, so the app can size its own expectations. */
uint16_t ft_sfx_length_ms(FtSfxId id);

#endif /* FT_AUDIO_H */
