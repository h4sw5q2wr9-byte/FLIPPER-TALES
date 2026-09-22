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
#define C6 1047
#define E6 1319
#define G6 1568

/* ---- The cues ----------------------------------------------------------
 *
 * Kept short. Anything over about 300ms on a step or a hit starts arriving
 * after the thing it is describing, and a buzzer has no decay to hide in. */

static const FtNote SFX_MOVE[]  = {{C5, 12}};
static const FtNote SFX_PICK[]  = {{G5, 40}, {C6, 70}};
static const FtNote SFX_DENY[]  = {{A4, 50}, {F4, 80}};
static const FtNote SFX_TALK[]  = {{E5, 30}, {FT_NOTE_REST, 20}, {G5, 40}};

static const FtNote SFX_SWING[] = {{G4, 25}, {C5, 25}};
static const FtNote SFX_HIT[]   = {{C6, 30}, {G5, 50}};
static const FtNote SFX_HURT[]  = {{E4, 45}, {C4, 90}};

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

#define CUE(a) {a, (uint8_t)(sizeof(a) / sizeof((a)[0]))}

static const struct {
    const FtNote* notes;
    uint8_t       count;
} FT_CUES[FT_SFX_COUNT] = {
    [FT_SFX_MOVE]      = CUE(SFX_MOVE),
    [FT_SFX_PICK]      = CUE(SFX_PICK),
    [FT_SFX_DENY]      = CUE(SFX_DENY),
    [FT_SFX_TALK]      = CUE(SFX_TALK),
    [FT_SFX_SWING]     = CUE(SFX_SWING),
    [FT_SFX_HIT]       = CUE(SFX_HIT),
    [FT_SFX_HURT]      = CUE(SFX_HURT),
    [FT_SFX_JAM]       = CUE(SFX_JAM),
    [FT_SFX_PERFECT]   = CUE(SFX_PERFECT),
    [FT_SFX_DEFLECT]   = CUE(SFX_DEFLECT),
    [FT_SFX_LEVEL]     = CUE(SFX_LEVEL),
    [FT_SFX_WIN]       = CUE(SFX_WIN),
    [FT_SFX_LOSE]      = CUE(SFX_LOSE),
    [FT_SFX_ENCOUNTER] = CUE(SFX_ENCOUNTER),
};

const FtNote* ft_sfx(FtSfxId id, uint8_t* count) {
    if(id >= FT_SFX_COUNT) {
        if(count) *count = 0u;
        return NULL;
    }
    if(count) *count = FT_CUES[id].count;
    return FT_CUES[id].notes;
}

uint16_t ft_sfx_length_ms(FtSfxId id) {
    uint8_t       n = 0;
    const FtNote* notes = ft_sfx(id, &n);
    if(!notes) return 0u;

    uint32_t total = 0;
    for(uint8_t i = 0; i < n; i++) total += notes[i].ms;

    return (uint16_t)((total > 0xFFFFu) ? 0xFFFFu : total);
}
