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

#define FT_ATK_CRAWLER_RIP  20
#define FT_ATK_RIME_CRUSH   21
#define FT_ATK_RIME_FREEZE  22
#define FT_ATK_DRONE_STRAFE 23
#define FT_ATK_DRONE_DIVE   24
#define FT_ATK_RELAY_HUM    25
#define FT_ATK_RELAY_SURGE  26
#define FT_ATK_NULL_WASH    27
#define FT_ATK_NULL_COLLAPSE 28
#define FT_ATK_WALL_NONE     29
#define FT_ATK_BOOT_STIR     30
#define FT_ATK_BOOT_WAKE     31

const FtModule FT_MODULES[FT_MODULE_COUNT] = {
    [FT_MOD_SUBGHZ] =
        {.name = "Sub-GHz",
         .slot = FT_SLOT_BROADCAST,
         .ram_cost = 0,
         .hits_all = true,
         /* Power 3, below NFC's 4: the broadcast trades per-hit damage for
          * hitting everything. It must stay high enough that being forced
          * onto it by AIRBORNE is a redirection, not a punishment. */
         .attack = {FT_ATK_SUBGHZ, 3, 0, false, FT_DELIVERY_BROADCAST, FT_CLASS_NORMAL,
                    FT_PAYLOAD_NONE}},

    [FT_MOD_NFC] =
        {.name = "NFC",
         .slot = FT_SLOT_CONTACT,
         /* MP was earned and never spent, so a third of the status row meant
          * nothing. The strong module costs one, which gives MP a job and
          * gives a long fight a rhythm: hit hard until you are out, then
          * brace. */
         .ram_cost = 1,
         .hits_all = false,
         /* Contact burst: higher power, and halves the target's shield. */
         .attack = {FT_ATK_NFC, 4, 0, true, FT_DELIVERY_CONTACT, FT_CLASS_NORMAL,
                    FT_PAYLOAD_NONE}},
};

uint8_t ft_module_ram_cost(FtModuleId id) {
    if(id >= FT_MODULE_COUNT) return 0u;
    return FT_MODULES[id].ram_cost;
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

    /* ---- The Scrapline ------------------------------------------------
     *
     * FAST and fragile. It gets the first word every round, so the lesson is
     * that initiative is a stat: kill it before it is a problem, or spend a
     * turn bracing for a hit you cannot out-race. */
    [FT_ENEMY_SCRAP_CRAWLER] =
        {.name = "Scrap Crawler",
         /* Tough enough to survive one good hit, or FAST never gets to be
          * true of it: at 7 Charge it died before it ever acted and the
          * Scrapline was a 100% walkover at every skill level. */
         .charge = 12,
         .shielded = 0,
         .attrs = FT_ATTR_FAST,
         .level = 2,
         .xp = 16,
         .attack_count = 1,
         .attacks = {{FT_ATK_CRAWLER_RIP, 6, 0, false, FT_DELIVERY_CONTACT,
                      FT_CLASS_NORMAL, FT_PAYLOAD_NONE}}},

    /* ---- Cold Storage --------------------------------------------------
     *
     * The Sealed Lock's lesson with the volume up: shield 3 on top of
     * ENCRYPTED. Broadcast is refunded and unpierced contact barely scratches
     * it, so this is the fight that makes Payload's half-pierce worth its
     * Flash. Slow and low-damage to compensate — it is a wall, not a threat. */
    [FT_ENEMY_RIME_SHELL] =
        {.name = "Rime Shell",
         .charge = 16,
         .shielded = 3,
         .attrs = FT_ATTR_ENCRYPTED,
         .level = 3,
         .xp = 28,
         .attack_count = 2,
         .attacks = {{FT_ATK_RIME_CRUSH, 4, 0, false, FT_DELIVERY_CONTACT,
                      FT_CLASS_NORMAL, FT_PAYLOAD_NONE},
                     {FT_ATK_RIME_FREEZE, 3, 0, false, FT_DELIVERY_CONTACT,
                      FT_CLASS_NORMAL, FT_PAYLOAD_STALL}}},

    /* ---- The Turnstile --------------------------------------------------
     *
     * AIRBORNE and FAST together: contact cannot touch it and it moves before
     * you do. Broadcast is the only answer, which is the point — the area is
     * about routes that are closed until you hold the right thing. */
    [FT_ENEMY_GATE_DRONE] =
        {.name = "Gate Drone",
         .charge = 9,
         .shielded = 1,
         .attrs = FT_ATTR_AIRBORNE | FT_ATTR_FAST,
         .level = 3,
         .xp = 24,
         .attack_count = 2,
         .attacks = {{FT_ATK_DRONE_STRAFE, 3, 0, false, FT_DELIVERY_BROADCAST,
                      FT_CLASS_NORMAL, FT_PAYLOAD_NONE},
                     {FT_ATK_DRONE_DIVE, 5, 0, false, FT_DELIVERY_CONTACT,
                      FT_CLASS_GUARDED, FT_PAYLOAD_NONE}}},

    /* ---- Signal Hill ----------------------------------------------------
     *
     * A JAMMER: the Signal meter is locked for the whole fight, so Focus and
     * every captured replay are off the table and the two base modules have
     * to carry it. Tanky and slow, because a fight you have fewer tools for
     * should be long rather than sharp. */
    [FT_ENEMY_MAST_RELAY] =
        {.name = "Mast Relay",
         .charge = 18,
         .shielded = 1,
         .attrs = FT_ATTR_JAMMER,
         .level = 4,
         .xp = 32,
         .attack_count = 2,
         .attacks = {{FT_ATK_RELAY_HUM, 3, 0, false, FT_DELIVERY_BROADCAST,
                      FT_CLASS_NORMAL, FT_PAYLOAD_NONE},
                     {FT_ATK_RELAY_SURGE, 5, 0, false, FT_DELIVERY_BROADCAST,
                      FT_CLASS_GUARDED, FT_PAYLOAD_CORRUPT}}},

    /* ---- The Deadzone ---------------------------------------------------
     *
     * ENCRYPTED and a JAMMER, and both its attacks are GUARDED — jammable,
     * never capturable. Nothing about this fight gives you anything back:
     * no broadcast damage, no meter, no new signals. Contact and timing, or
     * nothing. The area is named for what it takes away. */
    [FT_ENEMY_NULL_FIELD] =
        {.name = "Null Field",
         .charge = 15,
         .shielded = 2,
         .attrs = FT_ATTR_ENCRYPTED | FT_ATTR_JAMMER,
         .level = 5,
         .xp = 40,
         .attack_count = 2,
         .attacks = {{FT_ATK_NULL_WASH, 4, 0, false, FT_DELIVERY_BROADCAST,
                      FT_CLASS_GUARDED, FT_PAYLOAD_NONE},
                     {FT_ATK_NULL_COLLAPSE, 6, 0, false, FT_DELIVERY_CONTACT,
                      FT_CLASS_GUARDED, FT_PAYLOAD_DRAIN}}},

    /* ---- Shape-changers -------------------------------------------------
     *
     * Blank Wall never attacks and nothing behind it can be touched while it
     * stands — a broadcast included, because a way round would make it
     * scenery. All it does is decide the order you fight in, which turns a
     * row of foes into a queue. Tough, but worth little: it is a delay, not
     * a threat, and paying full XP for a punching bag would make it farm
     * bait. Its attack entry exists only because every enemy has one; it is
     * never chosen, because a BULWARK never takes a turn. */
    [FT_ENEMY_BLANK_WALL] =
        {.name = "Blank Wall",
         /* A gate, not a boss. At 20 Charge behind a shield, with two foes
          * hitting you freely the whole time it stands, the fight was 15% at
          * low skill: the mechanic was doing all the work and the numbers
          * were doing it again. */
         .charge = 14,
         .shielded = 0,
         .attrs = FT_ATTR_BULWARK,
         .level = 3,
         .xp = 14,
         .attack_count = 1,
         .attacks = {{FT_ATK_WALL_NONE, 1, 0, false, FT_DELIVERY_CONTACT,
                      FT_CLASS_NORMAL, FT_PAYLOAD_NONE}}},

    /* Cold Booter sits the fight out while anything else lives, then wakes
     * up as the hardest thing on the board. Clearing the room is what starts
     * the fight, which inverts the usual read: the safe-looking one in the
     * corner is the reason you should have kept something alive. */
    [FT_ENEMY_COLD_BOOTER] =
        {.name = "Cold Booter",
         .charge = 14,
         .shielded = 2,
         .attrs = FT_ATTR_SLEEPER,
         .level = 5,
         .xp = 44,
         .attack_count = 2,
         /* The first is never used — a sleeper leads with its last — but a
          * captured replay needs somewhere sane to land if one is ever
          * taken from it. */
         .attacks = {{FT_ATK_BOOT_STIR, 3, 0, false, FT_DELIVERY_CONTACT,
                      FT_CLASS_NORMAL, FT_PAYLOAD_NONE},
                     {FT_ATK_BOOT_WAKE, 8, 1, false, FT_DELIVERY_CONTACT,
                      FT_CLASS_NORMAL, FT_PAYLOAD_CORRUPT}}},
};

const FtAttack* ft_attack_by_id(uint16_t id) {
    if(id == 0u) return NULL;

    for(uint8_t e = 0; e < FT_ENEMY_COUNT; e++) {
        const FtEnemy* en = &FT_ENEMIES[e];
        for(uint8_t a = 0; a < en->attack_count; a++) {
            if(en->attacks[a].id == id) return &en->attacks[a];
        }
    }
    return NULL;
}
