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

void ft_guide_traits(FtEnemyId id, char* out, uint8_t cap) {
    if(cap == 0u) return;
    out[0] = '\0';
    if(id >= FT_ENEMY_COUNT) return;

    const FtEnemy* en = &FT_ENEMIES[id];

    if(en->shielded > 0) {
        append(out, cap, "SH");
        append_num(out, cap, en->shielded);
    }
    if(en->attrs & FT_ATTR_AIRBORNE) {
        if(out[0] != '\0') append(out, cap, " ");
        append(out, cap, "AIR");
    }
    if(en->attrs & FT_ATTR_ENCRYPTED) {
        if(out[0] != '\0') append(out, cap, " ");
        append(out, cap, "ENC");
    }
    if(en->attrs & FT_ATTR_FAST) {
        if(out[0] != '\0') append(out, cap, " ");
        append(out, cap, "FST");
    }
    if(en->attrs & FT_ATTR_JAMMER) {
        if(out[0] != '\0') append(out, cap, " ");
        append(out, cap, "JAM");
    }
    if(en->attrs & FT_ATTR_BULWARK) {
        if(out[0] != '\0') append(out, cap, " ");
        append(out, cap, "WALL");
    }
    if(en->attrs & FT_ATTR_SLEEPER) {
        if(out[0] != '\0') append(out, cap, " ");
        append(out, cap, "SLP");
    }

    if(out[0] == '\0') append(out, cap, "no traits");
}

const char* ft_guide_note(FtEnemyId id, uint8_t n) {
    if(id >= FT_ENEMY_COUNT) return NULL;

    const FtEnemy* en = &FT_ENEMIES[id];
    uint8_t at = 0;

    /* A tag is only useful if you know what it costs you, so each line says
     * what the trait does rather than restating its name. */
    if(en->attrs & FT_ATTR_AIRBORNE) {
        if(at == n) return "Airborne: NFC misses";
        at++;
    }
    if(en->attrs & FT_ATTR_ENCRYPTED) {
        if(at == n) return "Sealed: Sub-GHz dud";
        at++;
    }
    if(en->attrs & FT_ATTR_FAST) {
        if(at == n) return "Fast: moves first";
        at++;
    }
    if(en->attrs & FT_ATTR_JAMMER) {
        if(at == n) return "Jams your SP";
        at++;
    }
    if(en->attrs & FT_ATTR_BULWARK) {
        if(at == n) return "Blocks all behind it";
        at++;
    }
    if(en->attrs & FT_ATTR_SLEEPER) {
        if(at == n) return "Wakes up last, hard";
        at++;
    }
    if(en->shielded > 0) {
        if(at == n) return "Shielded: pierce it";
        at++;
    }
    return NULL;
}

bool ft_guide_attack_line(FtEnemyId id, uint8_t n, char* out, uint8_t cap) {
    if(cap == 0u) return false;
    out[0] = '\0';

    if(id >= FT_ENEMY_COUNT) return false;

    const FtEnemy* en = &FT_ENEMIES[id];
    if(n >= en->attack_count) return false;

    const FtAttack* atk = &en->attacks[n];

    append(out, cap, (atk->delivery == FT_DELIVERY_BROADCAST) ? "Wide " : "Close ");
    append_num(out, cap, atk->base_power);

    switch(atk->klass) {
    case FT_CLASS_UNDODGEABLE: append(out, cap, " none"); break;
    case FT_CLASS_GUARDED:     append(out, cap, " jam"); break;
    case FT_CLASS_NORMAL:
    default:                   append(out, cap, " keep"); break;
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
