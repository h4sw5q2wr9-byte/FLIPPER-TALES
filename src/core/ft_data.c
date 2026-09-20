#include "ft_data.h"

/* Attack ids are stable and nonzero: the Signal Library keys on them. */
#define FT_ATK_SUBGHZ  1
#define FT_ATK_NFC     2
#define FT_ATK_PAYLOAD 3

#define FT_ATK_PACKET_BURST 10
#define FT_ATK_BEACON_PING  11
#define FT_ATK_BEACON_SWEEP 12
#define FT_ATK_LOCK_CLAMP   13
#define FT_ATK_LOCK_SEAL    14

const FtModule FT_MODULES[FT_MODULE_COUNT] = {
    [FT_MOD_SUBGHZ] =
        {.name = "Sub-GHz",
         .slot = FT_SLOT_BROADCAST,
         .flash_cost = 0,
         .ram_cost = 0,
         .max_stacks = 1,
         .hits_all = true,
         /* Power 3, below NFC's 4: the broadcast trades per-hit damage for
          * hitting everything. It must stay high enough that being forced
          * onto it by AIRBORNE is a redirection, not a punishment. */
         .attack = {FT_ATK_SUBGHZ, 3, 0, false, FT_DELIVERY_BROADCAST, FT_CLASS_NORMAL,
                    FT_PAYLOAD_NONE}},

    [FT_MOD_AMPLIFY] =
        {.name = "Amplify",
         .slot = FT_SLOT_PASSIVE,
         .flash_cost = 2,
         .ram_cost = 1,
         .max_stacks = 3,
         .atk_up = 2},

    [FT_MOD_NFC] =
        {.name = "NFC",
         .slot = FT_SLOT_CONTACT,
         .flash_cost = 0,
         .ram_cost = 0,
         .max_stacks = 1,
         /* Contact burst: higher power, and halves the target's shield. */
         .attack = {FT_ATK_NFC, 4, 0, true, FT_DELIVERY_CONTACT, FT_CLASS_NORMAL,
                    FT_PAYLOAD_NONE}},

    [FT_MOD_PAYLOAD] =
        {.name = "Payload",
         .slot = FT_SLOT_CONTACT,
         .flash_cost = 2,
         .ram_cost = 2,
         .max_stacks = 2,
         .attack = {FT_ATK_PAYLOAD, 3, 0, false, FT_DELIVERY_CONTACT, FT_CLASS_NORMAL,
                    FT_PAYLOAD_CORRUPT}},

    [FT_MOD_CHARGE_PLUS] =
        {.name = "Charge+",
         .slot = FT_SLOT_PASSIVE,
         .flash_cost = 3,
         .ram_cost = 0,
         .max_stacks = 5,
         .charge_max_bonus = 5},

    [FT_MOD_FARADAY] =
        {.name = "Faraday",
         .slot = FT_SLOT_PASSIVE,
         .flash_cost = 1,
         .ram_cost = 0,
         .max_stacks = 2,
         .jam_bonus_pct = 10},

    [FT_MOD_DEEP_FOCUS] =
        {.name = "Deep Focus",
         .slot = FT_SLOT_PASSIVE,
         .flash_cost = 1,
         .ram_cost = 0,
         .max_stacks = 4,
         .deep_focus = 1},

    [FT_MOD_HARD_MODE] =
        {.name = "Hard Mode",
         .slot = FT_SLOT_PASSIVE,
         .flash_cost = 0,
         .ram_cost = 0,
         .max_stacks = 1,
         .hard_mode = true},
};

uint8_t ft_module_ram_cost(FtModuleId id, uint8_t stacks) {
    if(id >= FT_MODULE_COUNT || stacks == 0u) return 0u;
    return (uint8_t)(FT_MODULES[id].ram_cost * stacks);
}

void ft_loadout_init(FtLoadout* lo) {
    for(uint8_t i = 0; i < FT_MODULE_COUNT; i++) lo->stacks[i] = 0u;

    /* The two base modules are always installed and cost no Flash. */
    lo->stacks[FT_MOD_SUBGHZ] = 1u;
    lo->stacks[FT_MOD_NFC] = 1u;
}

bool ft_loadout_add(FtLoadout* lo, FtModuleId id) {
    if(id >= FT_MODULE_COUNT) return false;
    if(lo->stacks[id] >= FT_MODULES[id].max_stacks) return false;

    lo->stacks[id]++;
    return true;
}

FtLoadoutEffects ft_loadout_effects(const FtLoadout* lo) {
    FtLoadoutEffects fx = {0, 0, 0, FT_JAM_REDUCTION_PCT, 0, false};

    for(uint8_t i = 0; i < FT_MODULE_COUNT; i++) {
        const uint8_t n = lo->stacks[i];
        if(n == 0u) continue;

        const FtModule* m = &FT_MODULES[i];

        fx.flash_used += (int16_t)(m->flash_cost * n);
        fx.charge_max_bonus += (int16_t)(m->charge_max_bonus * n);
        fx.atk_up += (int16_t)(m->atk_up * n);
        fx.jam_reduction_pct = (uint8_t)(fx.jam_reduction_pct + m->jam_bonus_pct * n);
        fx.deep_focus_stacks = (uint8_t)(fx.deep_focus_stacks + m->deep_focus * n);
        if(m->hard_mode) fx.hard_mode = true;
    }

    if(fx.jam_reduction_pct > 100u) fx.jam_reduction_pct = 100u;

    return fx;
}

const FtEnemy FT_ENEMIES[FT_ENEMY_COUNT] = {
    [FT_ENEMY_STRAY_PACKET] =
        {.name = "Stray Packet",
         .charge = 8,
         .shielded = 0,
         .attrs = 0,
         .level = 1,
         .xp = 12,
         .attack_count = 1,
         .attacks = {{FT_ATK_PACKET_BURST, 3, 0, false, FT_DELIVERY_BROADCAST,
                      FT_CLASS_NORMAL, FT_PAYLOAD_NONE}}},

    /* Airborne: contact modules cannot reach it, so Sub-GHz is the answer. */
    [FT_ENEMY_DRIFT_BEACON] =
        {.name = "Drift Beacon",
         .charge = 10,
         /* Deliberately unshielded: AIRBORNE already forces the player onto
          * the broadcast module, and shielding it too would tax the same
          * forced choice twice. */
         .shielded = 0,
         .attrs = FT_ATTR_AIRBORNE,
         .level = 2,
         .xp = 18,
         .attack_count = 2,
         .attacks = {{FT_ATK_BEACON_PING, 4, 0, false, FT_DELIVERY_BROADCAST,
                      FT_CLASS_NORMAL, FT_PAYLOAD_NONE},
                     /* Guarded: jammable, but its secrets stay locked. */
                     {FT_ATK_BEACON_SWEEP, 5, 0, false, FT_DELIVERY_BROADCAST,
                      FT_CLASS_GUARDED, FT_PAYLOAD_STALL}}},

    /* Encrypted and shielded: broadcast is refunded, so contact and piercing
     * are the answer. */
    [FT_ENEMY_SEALED_LOCK] =
        {.name = "Sealed Lock",
         .charge = 14,
         .shielded = 2,
         .attrs = FT_ATTR_ENCRYPTED | FT_ATTR_FAST,
         .level = 3,
         .xp = 26,
         .attack_count = 2,
         .attacks = {{FT_ATK_LOCK_CLAMP, 5, 0, false, FT_DELIVERY_CONTACT,
                      FT_CLASS_NORMAL, FT_PAYLOAD_NONE},
                     /* Undodgeable: neither jammable nor capturable. */
                     {FT_ATK_LOCK_SEAL, 4, 1, false, FT_DELIVERY_CONTACT,
                      FT_CLASS_UNDODGEABLE, FT_PAYLOAD_DRAIN}}},
};
