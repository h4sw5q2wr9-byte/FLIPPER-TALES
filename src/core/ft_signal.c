#include "ft_signal.h"

/* ---- Meter ----------------------------------------------------------- */

void ft_signal_init(FtSignal* sig, uint8_t max_bars) {
    if(max_bars == 0u) max_bars = 1u;
    if(max_bars > FT_SIGNAL_MAX_BARS) max_bars = FT_SIGNAL_MAX_BARS;

    sig->max_bars = max_bars;
    sig->locked = false;
    sig->value = 0;
}

int16_t ft_signal_capacity(const FtSignal* sig) {
    return (int16_t)((int32_t)sig->max_bars * FT_SIGNAL_PER_BAR);
}

void ft_signal_battle_start(FtSignal* sig) {
    const int16_t cap = ft_signal_capacity(sig);
    sig->value = (FT_SIGNAL_BATTLE_START < cap) ? FT_SIGNAL_BATTLE_START : cap;
}

void ft_signal_add(FtSignal* sig, int16_t amount) {
    if(sig->locked || amount <= 0) return;

    const int32_t cap = ft_signal_capacity(sig);
    int32_t next = (int32_t)sig->value + amount;
    if(next > cap) next = cap;

    sig->value = (int16_t)next;
}

bool ft_signal_spend_bars(FtSignal* sig, uint8_t bars) {
    if(sig->locked || bars == 0u) return false;

    const int32_t cost = (int32_t)bars * FT_SIGNAL_PER_BAR;
    if((int32_t)sig->value < cost) return false;

    sig->value -= (int16_t)cost;
    return true;
}

uint8_t ft_signal_bars(const FtSignal* sig) {
    return (uint8_t)(sig->value / FT_SIGNAL_PER_BAR);
}

int16_t ft_signal_attack_gain(int16_t charge, int16_t charge_max) {
    if(charge <= 1) return FT_SIGNAL_GAIN_LAST_CHARGE;

    /* At or below a quarter of maximum Charge. */
    if((int32_t)charge * 4 <= (int32_t)charge_max) return FT_SIGNAL_GAIN_LOW_CHARGE;

    return FT_SIGNAL_GAIN_ATTACK;
}

int16_t ft_signal_focus_gain(uint8_t deep_focus_stacks) {
    return (int16_t)(FT_SIGNAL_GAIN_FOCUS +
                     (int32_t)deep_focus_stacks * FT_SIGNAL_GAIN_DEEP_FOCUS);
}

/* ---- Library --------------------------------------------------------- */

void ft_siglib_init(FtSignalLibrary* lib) {
    for(uint8_t i = 0; i < FT_SIGLIB_SLOTS; i++) lib->ids[i] = 0u;
    lib->count = 0u;
    lib->next = 0u;
}

bool ft_siglib_holds(const FtSignalLibrary* lib, uint16_t attack_id) {
    for(uint8_t i = 0; i < lib->count; i++) {
        if(lib->ids[i] == attack_id) return true;
    }
    return false;
}

bool ft_siglib_capture(FtSignalLibrary* lib, uint16_t attack_id) {
    if(attack_id == 0u) return false;
    if(ft_siglib_holds(lib, attack_id)) return false;

    lib->ids[lib->next] = attack_id;
    lib->next = (uint8_t)((lib->next + 1u) % FT_SIGLIB_SLOTS);
    if(lib->count < FT_SIGLIB_SLOTS) lib->count++;

    return true;
}

int16_t ft_siglib_replay_power(int16_t base_power) {
    if(base_power <= 0) return 0;

    int32_t power = ((int32_t)base_power * FT_SIGLIB_REPLAY_PCT) / 100;
    if(power < 1) power = 1; /* a copy is weaker, never harmless */

    return (int16_t)power;
}
