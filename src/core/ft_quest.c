#include "ft_quest.h"

/* Every quest needs a byte to live in, and the save reserves exactly
 * FT_QUEST_BYTES of them. Overflowing would be silent — the quest would
 * simply never remember anything — so it fails the build instead. */
typedef char ft_quest_bytes_fit[(FT_QUEST_BYTES >= FT_QUEST_COUNT) ? 1 : -1];

static const FtQuestDef FT_QUESTS[FT_QUEST_COUNT] = {
    /* The giver stands in Wake, room 0, which is also where you start and
     * where the terminal is. The goal is the Cold Gate at the far end of the
     * prologue: four rooms out, four back, and foes respawn behind you, so
     * "without a fight" is a real route rather than a formality. */
    [FT_QUEST_CLEAN_RUN] = {"Clean Run", 0u, 3u, true, 2},

    /* Warden Coll holds the gate in room 10; Wren is down the junction at
     * room 11. The goal is not a room, because getting there is not what
     * finishes it — you have to talk her into coming out. */
    [FT_QUEST_WREN] = {"The Kid", 10u, FT_QUEST_NO_ROOM, false, 3},
};

const FtQuestDef* ft_quest_def(FtQuestId id) {
    return &FT_QUESTS[(id < FT_QUEST_COUNT) ? id : 0];
}

void ft_quests_init(FtQuests* q) {
    for(uint8_t i = 0; i < FT_QUEST_BYTES; i++) q->state[i] = (uint8_t)FT_QUEST_UNKNOWN;
}

FtQuestState ft_quest_state(const FtQuests* q, FtQuestId id) {
    if(id >= FT_QUEST_COUNT) return FT_QUEST_UNKNOWN;
    return (FtQuestState)q->state[id];
}

static void set_state(FtQuests* q, FtQuestId id, FtQuestState s) {
    if(id >= FT_QUEST_COUNT) return;
    q->state[id] = (uint8_t)s;
}

void ft_quest_enter_room(FtQuests* q, uint8_t room) {
    for(uint8_t i = 0; i < FT_QUEST_COUNT; i++) {
        const FtQuestDef* d = &FT_QUESTS[i];
        if(d->goal_room == FT_QUEST_NO_ROOM) continue;
        if(ft_quest_state(q, (FtQuestId)i) != FT_QUEST_ACTIVE) continue;
        if(d->goal_room == room) set_state(q, (FtQuestId)i, FT_QUEST_READY);
    }
}

bool ft_quest_at_least(const FtQuests* q, FtQuestId id, FtQuestState need) {
    const FtQuestState at = ft_quest_state(q, id);

    /* FAILED is a step back to before it was taken, not past it: a clean run
     * you broke does not count as having been on one. */
    if(at == FT_QUEST_FAILED) return need == FT_QUEST_UNKNOWN;

    return (uint8_t)at >= (uint8_t)need;
}

void ft_quest_advance(FtQuests* q, FtQuestId id, FtQuestState to) {
    set_state(q, id, to);
}

const char* ft_quest_refusal(FtQuestId id, FtQuestState need) {
    (void)need;

    /* The soft lock. No key, no gate art, no new tile — the way is open and
     * the Courier simply has no reason, which is the cheapest possible way
     * to make a world open up later. */
    if(id == FT_QUEST_WREN) return "Nothing down there.";

    return "No reason to go.";
}

void ft_quest_battle(FtQuests* q) {
    for(uint8_t i = 0; i < FT_QUEST_COUNT; i++) {
        if(!FT_QUESTS[i].no_battle) continue;

        /* READY fails too. Turning round at the gate and punching your way
         * home is not a clean run, and the reward is for the whole route. */
        const FtQuestState s = ft_quest_state(q, (FtQuestId)i);
        if(s == FT_QUEST_ACTIVE || s == FT_QUEST_READY) {
            set_state(q, (FtQuestId)i, FT_QUEST_FAILED);
        }
    }
}

int ft_quest_for_room(uint8_t room) {
    for(uint8_t i = 0; i < FT_QUEST_COUNT; i++) {
        if(FT_QUESTS[i].giver_room == room) return (int)i;
    }
    return -1;
}


/* ---- What people say ---------------------------------------------------
 *
 * Every line is at most 20 characters — the panel's width budget — and a
 * test walks every branch of every conversation and measures them.
 *
 * Beats alternate on purpose. The old version was three lines in a box with
 * the quest's name over them, so only one person ever spoke and the player
 * never answered; these read as two people talking, and the one that
 * matters ends on a question. */

#define TALK(spk, arr) {spk, arr, (uint8_t)(sizeof(arr) / sizeof((arr)[0])), \
                        false, NULL, NULL}
#define ASK(spk, arr, y, n) {spk, arr, (uint8_t)(sizeof(arr) / sizeof((arr)[0])), \
                             true, y, n}

/* ---- The Keeper, in Cold Boot ---- */

static const char* KEEPER = "The Keeper";

static const FtBeat KEEPER_OFFER[] = {
    {FT_SAY_THEM, "You're awake.", NULL},
    {FT_SAY_THEM, "Took its time.", NULL},
    {FT_SAY_YOU,  "Where is this?", NULL},
    {FT_SAY_THEM, "Cold Boot. The end", "of a dead line."},
    {FT_SAY_THEM, "Do one thing for me.", NULL},
    {FT_SAY_THEM, "Touch the Cold Gate,", "come back. No fights"},
};

static const FtBeat KEEPER_ON[] = {
    {FT_SAY_THEM, "Still no fights?", NULL},
    {FT_SAY_YOU,  "Still walking.", NULL},
    {FT_SAY_THEM, "Gate's four rooms", "east. Go on."},
};

static const FtBeat KEEPER_FAILED[] = {
    {FT_SAY_THEM, "You fought one.", NULL},
    {FT_SAY_YOU,  "It found me.", NULL},
    {FT_SAY_THEM, "Then start again.", NULL},
};

static const FtBeat KEEPER_PAID[] = {
    {FT_SAY_THEM, "Clean the whole way.", NULL},
    {FT_SAY_YOU,  "Nothing touched me.", NULL},
    {FT_SAY_THEM, "Two orbs. Take them.", NULL},
};

static const FtBeat KEEPER_DONE[] = {
    {FT_SAY_THEM, "Nothing else today.", NULL},
};

/* ---- Warden Coll, at Weldhome's gate ---- */

static const char* COLL = "Warden Coll";

static const FtBeat COLL_OFFER[] = {
    {FT_SAY_THEM, "Gate's shut.", NULL},
    {FT_SAY_YOU,  "I'm passing through.", NULL},
    {FT_SAY_THEM, "You move like them.", "Like what takes us."},
    {FT_SAY_YOU,  "I'm not one of them.", NULL},
    {FT_SAY_THEM, "Prove it. A kid went", "east two days back."},
    {FT_SAY_THEM, "Bring Wren home and", "the gate opens."},
};

static const FtBeat COLL_ON[] = {
    {FT_SAY_THEM, "Down the shaft, then", "east. She knew."},
    {FT_SAY_YOU,  "I'll find her.", NULL},
};

static const FtBeat COLL_FAILED[] = {
    {FT_SAY_THEM, "Still a kid out", "there, unit."},
    {FT_SAY_YOU,  "I know.", NULL},
};

static const FtBeat COLL_PAID[] = {
    {FT_SAY_THEM, "Wren. Get inside.", NULL},
    {FT_SAY_YOU,  "She isn't hurt.", NULL},
    {FT_SAY_THEM, "Gate's open, unit.", NULL},
    {FT_SAY_THEM, "You're not the first", "one through here."},
    {FT_SAY_YOU,  "...Who was?", NULL},
    {FT_SAY_THEM, "Ask me when you've", "seen the spans.", },
};

static const FtBeat COLL_DONE[] = {
    {FT_SAY_THEM, "Gate's open.", NULL},
    {FT_SAY_THEM, "Mind the spans.", NULL},
};

/* ---- Wren, at the end of the junction ---- */

static const char* WREN = "Wren";

static const FtBeat WREN_FOUND[] = {
    {FT_SAY_THEM, "You're one of them.", NULL},
    {FT_SAY_YOU,  "No.", NULL},
    {FT_SAY_THEM, "...No. You're not.", NULL},
    {FT_SAY_THEM, "Take me home?", NULL},
};

static const FtBeat WREN_WAIT[] = {
    {FT_SAY_THEM, "Don't.", NULL},
};

/* ---- Dispatch ---------------------------------------------------------- */

static FtTalk keeper_talk(FtQuestState at) {
    switch(at) {
    case FT_QUEST_UNKNOWN: return (FtTalk)ASK(KEEPER, KEEPER_OFFER, "Fine", "Not now");
    case FT_QUEST_ACTIVE:  return (FtTalk)TALK(KEEPER, KEEPER_ON);
    case FT_QUEST_FAILED:  return (FtTalk)TALK(KEEPER, KEEPER_FAILED);
    case FT_QUEST_READY:   return (FtTalk)TALK(KEEPER, KEEPER_PAID);
    case FT_QUEST_DONE:
    default:               return (FtTalk)TALK(KEEPER, KEEPER_DONE);
    }
}

static FtTalk coll_talk(FtQuestState at) {
    switch(at) {
    case FT_QUEST_UNKNOWN: return (FtTalk)ASK(COLL, COLL_OFFER, "I'll go", "No");
    case FT_QUEST_ACTIVE:  return (FtTalk)TALK(COLL, COLL_ON);
    case FT_QUEST_FAILED:  return (FtTalk)TALK(COLL, COLL_FAILED);
    case FT_QUEST_READY:   return (FtTalk)TALK(COLL, COLL_PAID);
    case FT_QUEST_DONE:
    default:               return (FtTalk)TALK(COLL, COLL_DONE);
    }
}

FtTalk ft_quest_talk(const FtQuests* q, FtQuestId id) {
    const FtQuestState at = ft_quest_state(q, id);

    switch(id) {
    case FT_QUEST_CLEAN_RUN: return keeper_talk(at);
    case FT_QUEST_WREN:      return coll_talk(at);
    default:                 return (FtTalk)TALK("", WREN_WAIT);
    }
}

FtTalk ft_quest_wren_talk(const FtQuests* q) {
    if(ft_quest_state(q, FT_QUEST_WREN) != FT_QUEST_ACTIVE) {
        return (FtTalk)TALK(WREN, WREN_WAIT);
    }
    return (FtTalk)TALK(WREN, WREN_FOUND);
}

/* ---- What a finished conversation did ---------------------------------- */

static FtQuestOutcome nothing(void) {
    FtQuestOutcome o = {0, false, false};
    return o;
}

FtQuestOutcome ft_quest_answer(FtQuests* q, FtQuestId id, bool yes) {
    FtQuestOutcome o = nothing();
    if(id >= FT_QUEST_COUNT) return o;

    switch(ft_quest_state(q, id)) {
    case FT_QUEST_UNKNOWN:
        /* Saying no leaves everything exactly as it was, so a question you
         * did not mean to open costs nothing. */
        if(yes) set_state(q, id, FT_QUEST_ACTIVE);
        break;

    case FT_QUEST_FAILED:
        /* Failing re-offers rather than closing the door: a quest that can
         * only be failed once is a punishment for trying it early, which is
         * exactly when a player would try it. */
        set_state(q, id, FT_QUEST_ACTIVE);
        break;

    case FT_QUEST_READY:
        set_state(q, id, FT_QUEST_DONE);
        o.orbs = FT_QUESTS[id].reward_orbs;
        o.ended = true;
        break;

    case FT_QUEST_ACTIVE:
    case FT_QUEST_DONE:
    default:
        break;
    }
    return o;
}

FtQuestOutcome ft_quest_wren_answer(FtQuests* q) {
    FtQuestOutcome o = nothing();

    if(ft_quest_state(q, FT_QUEST_WREN) != FT_QUEST_ACTIVE) return o;

    set_state(q, FT_QUEST_WREN, FT_QUEST_READY);
    o.follows = true;
    return o;
}

const char* ft_quest_status_line(const FtQuests* q, FtQuestId id) {
    switch(ft_quest_state(q, id)) {
    case FT_QUEST_ACTIVE: return "on";
    case FT_QUEST_FAILED: return "failed";
    case FT_QUEST_READY:  return "collect";
    case FT_QUEST_DONE:   return "done";
    case FT_QUEST_UNKNOWN:
    default:              return "not met";
    }
}
