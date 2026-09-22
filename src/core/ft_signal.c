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
