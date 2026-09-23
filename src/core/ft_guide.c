#include "ft_guide.h"

/* One bit per enemy. If the table ever outgrows the bitfield the guide would
 * silently stop recording the newest enemies, which is the kind of bug you
 * find months later and blame on the save format. */
typedef char ft_guide_bits_fit[(FT_ENEMY_COUNT <= 16) ? 1 : -1];

void ft_guide_init(FtGuide* g) {
    g->seen = 0u;
}

void ft_guide_note_encounter(FtGuide* g, const FtEncounter* e) {
    for(uint8_t i = 0; i < e->foe_count; i++) {
        const FtEnemyId id = e->foes[i].id;
        if(id < FT_ENEMY_COUNT) g->seen = (uint16_t)(g->seen | (1u << id));
    }
}

bool ft_guide_knows(const FtGuide* g, FtEnemyId id) {
    if(id >= FT_ENEMY_COUNT) return false;
    return (g->seen & (1u << id)) != 0u;
}

uint8_t ft_guide_count(const FtGuide* g) {
    uint8_t n = 0;
    for(uint8_t i = 0; i < FT_ENEMY_COUNT; i++) {
        if(ft_guide_knows(g, (FtEnemyId)i)) n++;
    }
    return n;
}

FtEnemyId ft_guide_nth(const FtGuide* g, uint8_t n) {
    uint8_t seen = 0;
    for(uint8_t i = 0; i < FT_ENEMY_COUNT; i++) {
        if(!ft_guide_knows(g, (FtEnemyId)i)) continue;
        if(seen == n) return (FtEnemyId)i;
        seen++;
    }
    return FT_ENEMY_COUNT;
}

/* ---- Entry text -------------------------------------------------------- */

/* Append src to out, which holds at most cap-1 characters. No snprintf: core
 * stays free of stdio, and the widths here are all known and small. */
static void append(char* out, uint8_t cap, const char* src) {
    uint8_t n = 0;
    while(out[n] != '\0' && n < cap) n++;

    while(*src != '\0' && n + 1u < cap) out[n++] = *src++;
    out[n] = '\0';
}

static void append_num(char* out, uint8_t cap, int16_t v) {
    char buf[6];
    uint8_t n = 0;

    if(v < 0) v = 0;
    if(v == 0) {
        buf[n++] = '0';
    } else {
        char rev[6];
        uint8_t r = 0;
        while(v > 0 && r < sizeof(rev)) {
            rev[r++] = (char)('0' + (v % 10));
            v = (int16_t)(v / 10);
        }
        while(r > 0) buf[n++] = rev[--r];
    }
    buf[n] = '\0';

    append(out, cap, buf);
}

void ft_guide_vitals(FtEnemyId id, char* out, uint8_t cap) {
    if(cap == 0u) return;
    out[0] = '\0';
    if(id >= FT_ENEMY_COUNT) return;

    const FtEnemy* en = &FT_ENEMIES[id];

    append(out, cap, "HP ");
    append_num(out, cap, en->charge);

    /* "SH2" said the same thing in three characters nobody could read. */
    if(en->shielded > 0) {
        append(out, cap, "  Shield ");
        append_num(out, cap, en->shielded);
    }
}

const char* ft_guide_advice(FtEnemyId id) {
    if(id >= FT_ENEMY_COUNT) return "";

    const uint32_t attrs = FT_ENEMIES[id].attrs;

    /* Order is what to do *first*, not what is most unusual. A wall has to
     * die before anything else matters; a sleeper has to be left alone. */
    if(attrs & FT_ATTR_BULWARK)   return "Kill this first";
    if(attrs & FT_ATTR_SLEEPER)   return "Save it for last";

    /* Reach beats everything else: an attack that cannot land is not a
     * choice, it is a wasted turn. */
    if(attrs & FT_ATTR_AIRBORNE)  return "Use Sub-GHz";
    if(attrs & FT_ATTR_ENCRYPTED) return "Use NFC";

    if(FT_ENEMIES[id].shielded >= 2) return "Pierce it: NFC";
    if(attrs & FT_ATTR_JAMMER)    return "Kill it, free SP";
    if(attrs & FT_ATTR_FAST)      return "Drop it early";

    return "Any module works";
}

const char* ft_guide_tag(FtEnemyId id) {
    if(id >= FT_ENEMY_COUNT) return "";

    const uint32_t attrs = FT_ENEMIES[id].attrs;

    if(attrs & FT_ATTR_BULWARK)   return "first";
    if(attrs & FT_ATTR_SLEEPER)   return "last";
    if(attrs & FT_ATTR_AIRBORNE)  return "Sub-GHz";
    if(attrs & FT_ATTR_ENCRYPTED) return "NFC";
    if(FT_ENEMIES[id].shielded >= 2) return "NFC";
    if(attrs & FT_ATTR_JAMMER)    return "no SP";
    if(attrs & FT_ATTR_FAST)      return "fast";

    return "any";
}

const char* ft_guide_note(FtEnemyId id, uint8_t n) {
    if(id >= FT_ENEMY_COUNT) return NULL;

    const FtEnemy* en = &FT_ENEMIES[id];
    uint8_t at = 0;

    /* A tag is only useful if you know what it costs you, so each line says
     * what the trait does rather than restating its name. */
    if(en->attrs & FT_ATTR_BULWARK) {
        if(at == n) return "Nothing gets past it";
        at++;
    }
    if(en->attrs & FT_ATTR_SLEEPER) {
        if(at == n) return "Idle till it's alone";
        at++;
    }
    if(en->attrs & FT_ATTR_AIRBORNE) {
        if(at == n) return "Flies: NFC misses";
        at++;
    }
    if(en->attrs & FT_ATTR_ENCRYPTED) {
        if(at == n) return "Sealed: Sub-GHz = 0";
        at++;
    }
    if(en->attrs & FT_ATTR_FAST) {
        if(at == n) return "Moves before you do";
        at++;
    }
    if(en->attrs & FT_ATTR_JAMMER) {
        if(at == n) return "Locks your SP meter";
        at++;
    }
    return NULL;
}

uint8_t ft_guide_attack_count(FtEnemyId id) {
    if(id >= FT_ENEMY_COUNT) return 0u;

    /* A bulwark never takes a turn, so the attack in its table is a
     * placeholder the resolver needs and the player must never be shown.
     * Listing it read as a threat that does not exist. */
    if(FT_ENEMIES[id].attrs & FT_ATTR_BULWARK) return 0u;

    return FT_ENEMIES[id].attack_count;
}

bool ft_guide_attack_line(FtEnemyId id, uint8_t n, char* out, uint8_t cap) {
    if(cap == 0u) return false;
    out[0] = '\0';

    if(n >= ft_guide_attack_count(id)) return false;

    const FtAttack* atk = &FT_ENEMIES[id].attacks[n];

    /* Its name first: "Glare 5 jam SLOW". The name is what the wind-up in
     * the fight shows, so the page and the fight use the same word for the
     * same thing. That cost the "wave"/"touch" column, which never changed a
     * decision — the guard window is what you act on, and it is still here. */
    append(out, cap, ft_attack_name(atk->id));
    append(out, cap, " ");

    /* Power: the number you compare against your own HP. */
    append_num(out, cap, atk->base_power);

    /* What a guard can do about it, only when it is not the usual. */
    switch(atk->klass) {
    case FT_CLASS_UNDODGEABLE: append(out, cap, " no-jam"); break;
    case FT_CLASS_GUARDED:     append(out, cap, " jam"); break;
    case FT_CLASS_NORMAL:
    default:                   break;
    }

    /* What it leaves behind, if anything. Worth a column of its own: an
     * attack that corrupts you is a different problem from one that hits
     * for the same number and does not. */
    switch(atk->payload) {
    case FT_PAYLOAD_CORRUPT: append(out, cap, " DOT"); break;
    case FT_PAYLOAD_DRAIN:   append(out, cap, " MP-"); break;
    case FT_PAYLOAD_STALL:   append(out, cap, " SLOW"); break;
    case FT_PAYLOAD_NONE:
    default:                 break;
    }

    return true;
}
