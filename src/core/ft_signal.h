/* The Signal meter and the Signal Library. See DESIGN.md 4.5 and 4.7.
 *
 * The meter is stored in centi-units: FT_SIGNAL_PER_BAR (100) is one bar.
 * Integer only — the MCU has no FPU budget. */
#ifndef FT_SIGNAL_H
#define FT_SIGNAL_H

#include "ft_types.h"

/* ---- Meter ----------------------------------------------------------- */

typedef struct {
    int16_t value;
    uint8_t max_bars;
    bool    locked; /* a JAMMER enemy is present */
} FtSignal;

void ft_signal_init(FtSignal* sig, uint8_t max_bars);

/* Reset to the per-battle starting charge, clamped to capacity. */
void ft_signal_battle_start(FtSignal* sig);

int16_t ft_signal_capacity(const FtSignal* sig);

/* Add centi-units, clamped to capacity. No-op while locked. */
void ft_signal_add(FtSignal* sig, int16_t amount);

/* Spend whole bars. Returns false and changes nothing if short or locked. */
bool ft_signal_spend_bars(FtSignal* sig, uint8_t bars);

uint8_t ft_signal_bars(const FtSignal* sig);

/* Gain for landing an attack. Rises as Charge falls: this is the solo
 * risk/reward loop, and it pairs with being mid-roll. */
int16_t ft_signal_attack_gain(int16_t charge, int16_t charge_max);

/* Gain from Focus, including any Deep Focus stacks installed. */
int16_t ft_signal_focus_gain(uint8_t deep_focus_stacks);

/* ---- Library --------------------------------------------------------- */

/* Captured enemy attacks, replayable at FT_SIGLIB_REPLAY_PCT power.
 * A ring buffer: capturing when full overwrites the oldest entry. */
typedef struct {
    uint16_t ids[FT_SIGLIB_SLOTS];
    uint8_t  count;
    uint8_t  next;
} FtSignalLibrary;

void ft_siglib_init(FtSignalLibrary* lib);

/* Store an attack id. Returns false if it was already held (no duplicates)
 * so the caller can distinguish a new capture from a repeat. */
bool ft_siglib_capture(FtSignalLibrary* lib, uint16_t attack_id);

bool ft_siglib_holds(const FtSignalLibrary* lib, uint16_t attack_id);

/* Power of a replayed signal: a copy is weaker than the original. */
int16_t ft_siglib_replay_power(int16_t base_power);

/* The most recently captured id, or 0 when the library is empty. This is what
 * a SIGNAL action replays. */
uint16_t ft_siglib_latest(const FtSignalLibrary* lib);

#endif /* FT_SIGNAL_H */
