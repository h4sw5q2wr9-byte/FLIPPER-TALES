#include "ft_sound.h"

#include <furi_hal_speaker.h>

/* The buzzer is loud and the game is played in a room with other people in
 * it, so even a cue at 100 is played at half the hardware's range. Per-cue
 * volume scales inside that. */
#define FT_SOUND_CEILING 0.55f

/* How long to wait for the speaker. Zero: another app holding it is not a
 * reason for this one to stall a frame. */
#define FT_SOUND_ACQUIRE_MS 0

static void sound_silence(FtSound* s) {
    if(!s->held) return;

    if(furi_hal_speaker_is_mine()) {
        furi_hal_speaker_stop();
        furi_hal_speaker_release();
    }
    s->held = false;
}

/* Put the current note on the speaker, or stop for a rest. */
static void sound_voice(FtSound* s) {
    if(s->at >= s->count) return;

    const uint16_t hz = s->notes[s->at].hz;

    if(hz == FT_NOTE_REST) {
        if(s->held && furi_hal_speaker_is_mine()) furi_hal_speaker_stop();
        return;
    }

    if(!s->held) {
        if(!furi_hal_speaker_acquire(FT_SOUND_ACQUIRE_MS)) return;
        s->held = true;
    }
    if(furi_hal_speaker_is_mine()) {
        furi_hal_speaker_start((float)hz, s->volume);
    }
}

void ft_sound_init(FtSound* s, bool on) {
    s->notes = NULL;
    s->count = 0;
    s->at = 0;
    s->note_ms = 0;
    s->volume = FT_SOUND_CEILING;
    s->on = on;
    s->held = false;
}

void ft_sound_stop(FtSound* s) {
    s->notes = NULL;
    s->count = 0;
    s->at = 0;
    s->note_ms = 0;
    sound_silence(s);
}

void ft_sound_play(FtSound* s, FtSfxId id) {
    if(!s->on) return;

    uint8_t n = 0;
    const FtNote* notes = ft_sfx(id, &n);
    if(!notes || n == 0u) return;

    s->notes = notes;
    s->count = n;
    s->at = 0;
    s->note_ms = 0;
    s->volume = FT_SOUND_CEILING * (float)ft_sfx_volume(id) / 100.0f;

    sound_voice(s);
}

void ft_sound_tick(FtSound* s, uint32_t dt_ms) {
    if(s->notes == NULL) {
        /* Nothing to play and still holding the speaker: let go, so other
         * apps are not locked out by a game sitting in its pause menu. */
        sound_silence(s);
        return;
    }

    s->note_ms += dt_ms;

    while(s->notes != NULL && s->note_ms >= s->notes[s->at].ms) {
        s->note_ms -= s->notes[s->at].ms;
        s->at++;

        if(s->at >= s->count) {
            ft_sound_stop(s);
            return;
        }
        sound_voice(s);
    }
}

void ft_sound_set(FtSound* s, bool on) {
    s->on = on;
    if(!on) ft_sound_stop(s);
}
