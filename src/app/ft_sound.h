/* The speaker, and the only file that touches it.
 *
 * Same split as storage: core says what a cue *is* (ft_audio.h), this plays
 * it. One voice, because the hardware is one piezo buzzer.
 *
 * Nothing here blocks. A cue is started and then advanced from the game's
 * own tick, so a fanfare never holds up a frame — the render thread and the
 * input queue both keep running through it. */
#ifndef FT_SOUND_H
#define FT_SOUND_H

#include "../core/ft_audio.h"

typedef struct {
    const FtNote* notes;
    uint8_t       count;
    uint8_t       at;
    uint32_t      note_ms;

    bool on;    /* the player's setting */
    bool held;  /* we currently own the speaker */
} FtSound;

void ft_sound_init(FtSound* s, bool on);

/* Stop anything playing and hand the speaker back. Safe to call twice. */
void ft_sound_stop(FtSound* s);

/* Start a cue, replacing whatever was playing. A cue that is already the one
 * running is restarted, which is what a second hit in a row should sound
 * like. Does nothing while muted. */
void ft_sound_play(FtSound* s, FtSfxId id);

/* Advance by dt. Call once per frame from the game's tick. */
void ft_sound_tick(FtSound* s, uint32_t dt_ms);

/* Flip the setting. Muting stops whatever is playing immediately. */
void ft_sound_set(FtSound* s, bool on);

#endif /* FT_SOUND_H */
