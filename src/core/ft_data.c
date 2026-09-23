#include "ft_data.h"

/* Attack ids are stable and nonzero: the Signal Library keys on them. */
#define FT_ATK_SUBGHZ  1
#define FT_ATK_NFC     2
#define FT_ATK_PAYLOAD 3

#define FT_ATK_STAMP         10
#define FT_ATK_FLICKER       11
#define FT_ATK_GLARE         12
#define FT_ATK_CLAMP         13
#define FT_ATK_LOCK_UP       14
#define FT_ATK_SCOOP         20
#define FT_ATK_SLAM          21
#define FT_ATK_FROSTBITE     22
#define FT_ATK_TICKET        23
#define FT_ATK_REFUSED       24
#define FT_ATK_ANNOUNCE      25
#define FT_ATK_FEEDBACK      26
#define FT_ATK_SHH           27
#define FT_ATK_MUTE          28
#define FT_ATK_PLEASE_QUEUE  29
#define FT_ATK_SNORE         30
#define FT_ATK_CLOCK_IN      31
#define FT_ATK_HOLD_MUSIC    40
#define FT_ATK_PLEASE_HOLD   41

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

/* The Carrier's maintenance machines. Each one still does the job it was built
 * for, for Hush — whose one new rule is that everybody stays home — and the
 * job is the reason for the fight: a lamp glares, a lock locks you up, a
 * loudspeaker is too loud to think over. When a mechanic needs a reason, the
 * job is where it comes from. */
const FtEnemy FT_ENEMIES[FT_ENEMY_COUNT] = {
    /* Carried the mail between villages. Now it "returns" anything it finds
     * wandering, stamped and to the wrong address. The plain one. */
    [FT_ENEMY_PARCEL_RUNNER] =
        {.name = "Parcel Runner",
         .charge = 8,
         .shielded = 0,
         .attrs = 0,
         .level = 1,
         .xp = 12,
         .attack_count = 1,
         .attacks = {{FT_ATK_STAMP, 3, 0, false, FT_DELIVERY_BROADCAST,
                      FT_CLASS_NORMAL, FT_PAYLOAD_NONE}}},

    /* A floating lamp that lit the night roads for travellers. It floats,
     * so contact cannot reach it — Sub-GHz is the answer — and its Glare is
     * the lamp turned full on you: you may lose the turn blinking. */
    [FT_ENEMY_LAMPLIGHTER] =
        {.name = "Lamplighter",
         .charge = 10,
         /* Deliberately unshielded: AIRBORNE already forces the player onto
          * the broadcast module, and shielding it too would tax the same
          * forced choice twice. */
         .shielded = 0,
         .attrs = FT_ATTR_AIRBORNE,
         .level = 2,
         .xp = 18,
         .attack_count = 2,
         .attacks = {{FT_ATK_FLICKER, 4, 0, false, FT_DELIVERY_BROADCAST,
                      FT_CLASS_NORMAL, FT_PAYLOAD_NONE},
                     /* Guarded: jammable, but its secrets stay locked. */
                     {FT_ATK_GLARE, 5, 0, false, FT_DELIVERY_BROADCAST,
                      FT_CLASS_GUARDED, FT_PAYLOAD_STALL}}},

    /* Locked the doors at curfew. A lock cannot be picked from across the
     * room (ENCRYPTED: broadcast is refunded), so it is a hands-on fight; and
     * Lock Up locks your memory, draining MP. */
    [FT_ENEMY_CURFEW_LOCK] =
        {.name = "Curfew Lock",
         .charge = 14,
         .shielded = 2,
         .attrs = FT_ATTR_ENCRYPTED | FT_ATTR_FAST,
         .level = 3,
         .xp = 26,
         .attack_count = 2,
         .attacks = {{FT_ATK_CLAMP, 5, 0, false, FT_DELIVERY_CONTACT,
                      FT_CLASS_NORMAL, FT_PAYLOAD_NONE},
                     /* Undodgeable: neither jammable nor capturable. */
                     {FT_ATK_LOCK_UP, 4, 1, false, FT_DELIVERY_CONTACT,
                      FT_CLASS_UNDODGEABLE, FT_PAYLOAD_DRAIN}}},

    /* ---- The Scrapline ------------------------------------------------
     *
     * Swept debris off the spans. To a Sweeper, you are debris. FAST, because
     * it scurries: it gets the first word every round, so initiative is a
     * stat — kill it before it is a problem, or brace for a hit you cannot
     * out-race. */
    [FT_ENEMY_SWEEPER] =
        {.name = "Sweeper",
         /* Tough enough to survive one good hit, or FAST never gets to be
          * true of it: at 7 Charge it died before it ever acted and the
          * Scrapline was a 100% walkover at every skill level. */
         .charge = 12,
         .shielded = 0,
         .attrs = FT_ATTR_FAST,
         .level = 2,
         .xp = 16,
         .attack_count = 1,
         .attacks = {{FT_ATK_SCOOP, 6, 0, false, FT_DELIVERY_CONTACT,
                      FT_CLASS_NORMAL, FT_PAYLOAD_NONE}}},

    /* ---- Cold Storage --------------------------------------------------
     *
     * Kept the archive vaults cold. Heavy insulated shell — the Curfew Lock's
     * lesson with the volume up: shield 3 on top of ENCRYPTED. Broadcast is refunded and unpierced contact barely scratches
     * it, so this is the fight that makes Payload's half-pierce worth its
     * Flash. Slow and low-damage to compensate — it is a wall, not a threat. */
    [FT_ENEMY_CHILLER] =
        {.name = "Chiller",
         .charge = 16,
         .shielded = 3,
         .attrs = FT_ATTR_ENCRYPTED,
         .level = 3,
         .xp = 28,
         .attack_count = 2,
         .attacks = {{FT_ATK_SLAM, 4, 0, false, FT_DELIVERY_CONTACT,
                      FT_CLASS_NORMAL, FT_PAYLOAD_NONE},
                     {FT_ATK_FROSTBITE, 3, 0, false, FT_DELIVERY_CONTACT,
                      FT_CLASS_NORMAL, FT_PAYLOAD_STALL}}},

    /* ---- The Turnstile --------------------------------------------------
     *
     * Checked tickets at the Turnstile. It asks "Ticket?" and then Refuses
     * you, which is a joke and also the whole district. AIRBORNE and FAST: contact cannot touch it and it moves before
     * you do. Broadcast is the only answer, which is the point — the area is
     * about routes that are closed until you hold the right thing. */
    [FT_ENEMY_TICKET_DRONE] =
        {.name = "Ticket Drone",
         .charge = 9,
         .shielded = 1,
         .attrs = FT_ATTR_AIRBORNE | FT_ATTR_FAST,
         .level = 3,
         .xp = 24,
         .attack_count = 2,
         .attacks = {{FT_ATK_TICKET, 3, 0, false, FT_DELIVERY_BROADCAST,
                      FT_CLASS_NORMAL, FT_PAYLOAD_NONE},
                     {FT_ATK_REFUSED, 5, 0, false, FT_DELIVERY_CONTACT,
                      FT_CLASS_GUARDED, FT_PAYLOAD_NONE}}},

    /* ---- Signal Hill ----------------------------------------------------
     *
     * The mast loudspeaker that made the announcements, now playing Hush's
     * "please stay home" on a loop. So loud you cannot think — that is what
     * JAMMER means here. Its Feedback is the sound of the Loud Day. A JAMMER: the Signal meter is locked for the whole fight, so Focus and
     * every captured replay are off the table and the two base modules have
     * to carry it. Tanky and slow, because a fight you have fewer tools for
     * should be long rather than sharp. */
    [FT_ENEMY_LOUDHAILER] =
        {.name = "Loudhailer",
         .charge = 18,
         .shielded = 1,
         .attrs = FT_ATTR_JAMMER,
         .level = 4,
         .xp = 32,
         .attack_count = 2,
         .attacks = {{FT_ATK_ANNOUNCE, 3, 0, false, FT_DELIVERY_BROADCAST,
                      FT_CLASS_NORMAL, FT_PAYLOAD_NONE},
                     {FT_ATK_FEEDBACK, 5, 0, false, FT_DELIVERY_BROADCAST,
                      FT_CLASS_GUARDED, FT_PAYLOAD_CORRUPT}}},

    /* ---- The Deadzone ---------------------------------------------------
     *
     * Hush's own quiet-maker, from where the Silence started. It shushes:
     * ENCRYPTED and a JAMMER, and both its attacks are GUARDED — jammable,
     * never capturable. Nothing about this fight gives you anything back:
     * no broadcast damage, no meter, no new signals. Contact and timing, or
     * nothing. The area is named for what it takes away. */
    [FT_ENEMY_SHUSHER] =
        {.name = "Shusher",
         .charge = 15,
         .shielded = 2,
         .attrs = FT_ATTR_ENCRYPTED | FT_ATTR_JAMMER,
         .level = 5,
         .xp = 40,
         .attack_count = 2,
         .attacks = {{FT_ATK_SHH, 4, 0, false, FT_DELIVERY_BROADCAST,
                      FT_CLASS_GUARDED, FT_PAYLOAD_NONE},
                     {FT_ATK_MUTE, 6, 0, false, FT_DELIVERY_CONTACT,
                      FT_CLASS_GUARDED, FT_PAYLOAD_DRAIN}}},

    /* ---- Shape-changers -------------------------------------------------
     *
     * Kept the Turnstile's queue orderly. "Please queue" is all it ever
     * does, so you fight the others in line, in order. Queue Barrier never
     * attacks and nothing behind it can be touched while it
     * stands — a broadcast included, because a way round would make it
     * scenery. All it does is decide the order you fight in, which turns a
     * row of foes into a queue. Tough, but worth little: it is a delay, not
     * a threat, and paying full XP for a punching bag would make it farm
     * bait. Its attack entry exists only because every enemy has one; it is
     * never chosen, because a BULWARK never takes a turn. */
    [FT_ENEMY_QUEUE_BARRIER] =
        {.name = "Queue Barrier",
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
         .attacks = {{FT_ATK_PLEASE_QUEUE, 1, 0, false, FT_DELIVERY_CONTACT,
                      FT_CLASS_NORMAL, FT_PAYLOAD_NONE}}},

    /* Works the night shift: asleep while the others are busy, and clocks
     * in when the room goes quiet. Night Shift sits the fight out while
     * anything else lives, then wakes
     * up as the hardest thing on the board. Clearing the room is what starts
     * the fight, which inverts the usual read: the safe-looking one in the
     * corner is the reason you should have kept something alive. */
    [FT_ENEMY_NIGHT_SHIFT] =
        {.name = "Night Shift",
         .charge = 14,
         .shielded = 2,
         .attrs = FT_ATTR_SLEEPER,
         .level = 5,
         .xp = 44,
         .attack_count = 2,
         /* The first is never used — a sleeper leads with its last — but a
          * captured replay needs somewhere sane to land if one is ever
          * taken from it. */
         .attacks = {{FT_ATK_SNORE, 3, 0, false, FT_DELIVERY_CONTACT,
                      FT_CLASS_NORMAL, FT_PAYLOAD_NONE},
                     {FT_ATK_CLOCK_IN, 8, 1, false, FT_DELIVERY_CONTACT,
                      FT_CLASS_NORMAL, FT_PAYLOAD_CORRUPT}}},

    /* Echo. Your two attacks, in Hush's words: Hold Music is its Sub-GHz,
     * reaching you wherever you stand and making you wait; Please Hold is
     * its NFC, up close and hard. Neither AIRBORNE nor ENCRYPTED, because
     * it is built like you and both your modules reach it.
     *
     * Its Charge is a boss's, but it only has to lose half of it — then it
     * is gone (FT_ATTR_RETREATS). */
    [FT_ENEMY_ECHO] =
        {.name = "Echo",
         .charge = 52,
         .shielded = 1,
         .attrs = FT_ATTR_BOSS | FT_ATTR_RETREATS,
         .level = 4,
         .xp = 50,
         .attack_count = 2,
         .attacks = {{FT_ATK_HOLD_MUSIC, 3, 0, false, FT_DELIVERY_BROADCAST,
                      FT_CLASS_NORMAL, FT_PAYLOAD_STALL},
                     {FT_ATK_PLEASE_HOLD, 5, 0, true, FT_DELIVERY_CONTACT,
                      FT_CLASS_NORMAL, FT_PAYLOAD_NONE}}},
};

/* Every attack is named for what the machine used to do — a Lamplighter's
 * Glare is its lamp turned on you, a Ticket Drone asks "Ticket?" and then
 * Refuses you. Named here rather than in FtAttack so the enemy table keeps
 * its shape. */
static const struct {
    uint16_t    id;
    const char* name;
} ATTACK_NAMES[] = {
    {FT_ATK_STAMP, "Stamp"},
    {FT_ATK_FLICKER, "Flicker"},
    {FT_ATK_GLARE, "Glare"},
    {FT_ATK_CLAMP, "Clamp"},
    {FT_ATK_LOCK_UP, "Lock Up"},
    {FT_ATK_SCOOP, "Scoop"},
    {FT_ATK_SLAM, "Slam"},
    {FT_ATK_FROSTBITE, "Frostbite"},
    {FT_ATK_TICKET, "Ticket?"},
    {FT_ATK_REFUSED, "Refused"},
    {FT_ATK_ANNOUNCE, "Announcement"},
    {FT_ATK_FEEDBACK, "Feedback"},
    {FT_ATK_SHH, "Shh"},
    {FT_ATK_MUTE, "Mute"},
    {FT_ATK_PLEASE_QUEUE, "Please Queue"},
    {FT_ATK_SNORE, "Snore"},
    {FT_ATK_CLOCK_IN, "Clock In"},
    {FT_ATK_HOLD_MUSIC, "Hold Music"},
    {FT_ATK_PLEASE_HOLD, "Please Hold"},
};

const char* ft_attack_name(uint16_t id) {
    for(size_t i = 0; i < sizeof(ATTACK_NAMES) / sizeof(ATTACK_NAMES[0]); i++) {
        if(ATTACK_NAMES[i].id == id) return ATTACK_NAMES[i].name;
    }
    return "Attack";
}

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
