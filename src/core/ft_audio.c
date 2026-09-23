#include "ft_audio.h"

/* Equal temperament, rounded to whole hertz. Named so the tunes below read
 * as music rather than as a column of numbers. */
#define C4  262
#define D4  294
#define E4  330
#define F4  349
#define G4  392
#define A4  440
#define B4  494
#define C5  523
#define D5  587
#define E5  659
#define F5  698
#define G5  784
#define A5  880
#define B5  988
#define C6 1047
#define E6 1319
#define G6 1568

/* ---- The cues ----------------------------------------------------------
 *
 * Kept short. Anything over about 300ms on a step or a hit starts arriving
 * after the thing it is describing, and a buzzer has no decay to hide in. */

static const FtNote SFX_MOVE[]  = {{C5, 10}};

/* Seen. Two clipped beeps on the same note: a warning reads as a warning
 * because it repeats, not because it is loud. */
static const FtNote SFX_SPOT[]  = {{B5, 40}, {FT_NOTE_REST, 35}, {B5, 70}};
static const FtNote SFX_PICK[]  = {{G5, 40}, {C6, 70}};
static const FtNote SFX_DENY[]  = {{A4, 50}, {F4, 80}};
static const FtNote SFX_TALK[]  = {{E5, 30}, {FT_NOTE_REST, 20}, {G5, 40}};

static const FtNote SFX_SWING[] = {{G4, 20}, {C5, 20}};

/* A hit is a fast fall, not a note.
 *
 * Two tones read as a beep; four, dropping an octave in under a tenth of a
 * second, read as something connecting. A buzzer has no decay to land the
 * impact for you, so the shape has to do it. */
static const FtNote SFX_HIT[]   = {{E6, 14}, {C6, 18}, {G5, 22}, {E5, 38}};

/* Taking one is the same shape, lower and slower: it happened to you. */
static const FtNote SFX_HURT[]  = {{A4, 25}, {F4, 35}, {D4, 45}, {C4, 80}};

/* A jam is a small click up; a perfect block is the same idea with the top
 * note the fanfare uses, so "that was the good one" is audible without
 * reading anything. */
static const FtNote SFX_JAM[]     = {{A5, 30}, {E5, 40}};
static const FtNote SFX_PERFECT[] = {{C6, 30}, {E6, 30}, {G6, 70}};
static const FtNote SFX_DEFLECT[] = {{G6, 30}, {E6, 25}, {C6, 25}, {G5, 60}};

static const FtNote SFX_LEVEL[] = {
    {C5, 70}, {E5, 70}, {G5, 70}, {C6, 160},
};

static const FtNote SFX_WIN[] = {
    {C5, 90}, {E5, 90}, {G5, 90}, {C6, 110},
    {FT_NOTE_REST, 40}, {G5, 90}, {C6, 260},
};

/* Down, and slow, and it does not resolve. */
static const FtNote SFX_LOSE[] = {
    {G4, 140}, {F4, 140}, {D4, 160}, {FT_NOTE_REST, 60}, {C4, 340},
};

/* The wipe into a fight: two rising stabs, over before the iris is. */
static const FtNote SFX_ENCOUNTER[] = {
    {C5, 45}, {FT_NOTE_REST, 30}, {G5, 45}, {FT_NOTE_REST, 30}, {C6, 90},
};

/* Found. Falling, low and slow — the ground giving way under the grass —
 * and then one note back up, so it lands as a discovery rather than as a
 * trap. It ends high on purpose: this is good news. */
static const FtNote SFX_REVEAL[] = {
    {G4, 60}, {E4, 60}, {C4, 90}, {FT_NOTE_REST, 60}, {G5, 110},
};

/* Voices. Each is a single short note — a blip, not a word — and they are
 * spaced far enough apart in pitch that you can tell who is talking with
 * your eyes shut. The Keeper is low and slow, Coll clipped, Hale in the
 * middle, Wren high. You are two notes a fifth apart, which is the only one
 * that is not a single pitch: you are the machine in the room. */
static const FtNote VOICE_KEEPER[] = {{G4, 22}};
static const FtNote VOICE_COLL[]   = {{C5, 14}};
static const FtNote VOICE_HALE[]   = {{E5, 18}};
static const FtNote VOICE_WREN[]   = {{A5, 14}};
static const FtNote VOICE_YOU[]    = {{C5, 9}, {G5, 9}};

#define CUE(a, v) {a, (uint8_t)(sizeof(a) / sizeof((a)[0])), v}

static const struct {
    const FtNote* notes;
    uint8_t       count;
    uint8_t       volume; /* 0-100 */
} FT_CUES[FT_SFX_COUNT] = {
    /* A footstep fires on every tile, so it is barely there: loud enough to
     * give walking a texture, quiet enough to stop being noticed. */
    [FT_SFX_MOVE]      = CUE(SFX_MOVE, 12),
    [FT_SFX_SPOT]      = CUE(SFX_SPOT, 70),
    [FT_SFX_PICK]      = CUE(SFX_PICK, 45),
    [FT_SFX_DENY]      = CUE(SFX_DENY, 40),
    [FT_SFX_TALK]      = CUE(SFX_TALK, 35),

    /* The fight is the loud part. */
    [FT_SFX_SWING]     = CUE(SFX_SWING, 45),
    [FT_SFX_HIT]       = CUE(SFX_HIT, 90),
    [FT_SFX_HURT]      = CUE(SFX_HURT, 80),
    [FT_SFX_JAM]       = CUE(SFX_JAM, 70),
    [FT_SFX_PERFECT]   = CUE(SFX_PERFECT, 90),
    [FT_SFX_DEFLECT]   = CUE(SFX_DEFLECT, 95),

    [FT_SFX_LEVEL]     = CUE(SFX_LEVEL, 70),
    [FT_SFX_WIN]       = CUE(SFX_WIN, 75),
    [FT_SFX_LOSE]      = CUE(SFX_LOSE, 60),
    [FT_SFX_REVEAL]    = CUE(SFX_REVEAL, 70),

    /* Under the text, not over it: a voice that is louder than a footstep
     * but well under anything that happens to you. */
    [FT_SFX_VOICE_KEEPER] = CUE(VOICE_KEEPER, 22),
    [FT_SFX_VOICE_COLL]   = CUE(VOICE_COLL, 22),
    [FT_SFX_VOICE_HALE]   = CUE(VOICE_HALE, 22),
    [FT_SFX_VOICE_WREN]   = CUE(VOICE_WREN, 22),
    [FT_SFX_VOICE_YOU]    = CUE(VOICE_YOU, 18),
    [FT_SFX_ENCOUNTER] = CUE(SFX_ENCOUNTER, 65),
};

const FtNote* ft_sfx(FtSfxId id, uint8_t* count) {
    if(id >= FT_SFX_COUNT) {
        if(count) *count = 0u;
        return NULL;
    }
    if(count) *count = FT_CUES[id].count;
    return FT_CUES[id].notes;
}

uint8_t ft_sfx_volume(FtSfxId id) {
    if(id >= FT_SFX_COUNT) return 0u;
    return FT_CUES[id].volume;
}

uint16_t ft_sfx_length_ms(FtSfxId id) {
    uint8_t       n = 0;
    const FtNote* notes = ft_sfx(id, &n);
    if(!notes) return 0u;

    uint32_t total = 0;
    for(uint8_t i = 0; i < n; i++) total += notes[i].ms;

    return (uint16_t)((total > 0xFFFFu) ? 0xFFFFu : total);
}
