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

/* Lines are at most 20 characters — the panel's width budget. A test walks
 * every branch of this function and measures them. */
static FtQuestTalk say3(const char* a, const char* b, const char* c, int16_t orbs) {
    FtQuestTalk t;
    t.who = "";
    t.follows = false;
    t.line[0] = a;
    t.line[1] = b;
    t.line[2] = c;
    t.lines = (uint8_t)(c ? 3u : (b ? 2u : 1u));
    t.orbs = orbs;
    return t;
}

/* Wren, once she is out. She is the only thing in this game that is not
 * either a machine or afraid of you, which is the whole point of her. */
static FtQuestTalk wren_line(const char* a, const char* b, const char* c) {
    FtQuestTalk t = say3(a, b, c, 0);
    t.who = "Wren";
    return t;
}

static FtQuestTalk clean_run_talk(FtQuests* q) {
    const FtQuestId id = FT_QUEST_CLEAN_RUN;
    FtQuestTalk t;

    switch(ft_quest_state(q, id)) {
    case FT_QUEST_UNKNOWN:
        set_state(q, id, FT_QUEST_ACTIVE);
        t = say3("Touch the Cold Gate", "and come back here.",
                 "No fights. 2 orbs.", 0);
        break;

    case FT_QUEST_ACTIVE:
        t = say3("Still no fights?", "Good. The gate is", "four rooms east.", 0);
        break;

    case FT_QUEST_FAILED:
        /* Failing re-offers rather than closing the door: the quest is a
         * route to practise, and one that can only be failed once is a
         * punishment for trying it early. */
        set_state(q, id, FT_QUEST_ACTIVE);
        t = say3("You fought one.", "Start over: gate,", "back, no fights.", 0);
        break;

    case FT_QUEST_READY:
        set_state(q, id, FT_QUEST_DONE);
        t = say3("Clean the whole way.", "Take these.", "+2 orbs",
                 FT_QUESTS[id].reward_orbs);
        break;

    case FT_QUEST_DONE:
    default:
        t = say3("Nothing else for", "you today.", NULL, 0);
        break;
    }

    t.who = "The Keeper";
    return t;
}

static FtQuestTalk wren_talk(FtQuests* q) {
    const FtQuestId id = FT_QUEST_WREN;
    FtQuestTalk t;

    switch(ft_quest_state(q, id)) {
    case FT_QUEST_UNKNOWN:
        /* She names the terms herself, because she would rather be wrong
         * about you than right. */
        set_state(q, id, FT_QUEST_ACTIVE);
        t = say3("You move like them.", "A kid went east two",
                 "days back. Find her.", 0);
        break;

    case FT_QUEST_ACTIVE:
        t = say3("South, then east.", "She knew not to go.", "Go on.", 0);
        break;

    case FT_QUEST_FAILED:
        set_state(q, id, FT_QUEST_ACTIVE);
        t = say3("Still east. Still a", "kid. Go.", NULL, 0);
        break;

    case FT_QUEST_READY:
        set_state(q, id, FT_QUEST_DONE);
        /* No apology, no speech. The last line is the first thread of the
         * plot: Chapter 1 ends by making the spine visible. */
        t = say3("Wren. Inside.", "Gate's open, unit.",
                 "You're not the first", FT_QUESTS[id].reward_orbs);
        break;

    case FT_QUEST_DONE:
    default:
        t = say3("Gate's open.", "Mind the spans.", NULL, 0);
        break;
    }

    t.who = "Warden Coll";
    return t;
}

FtQuestTalk ft_quest_talk(FtQuests* q, FtQuestId id) {
    FtQuestTalk t;

    switch(id) {
    case FT_QUEST_CLEAN_RUN: return clean_run_talk(q);
    case FT_QUEST_WREN:      return wren_talk(q);
    default:
        t = say3("...", NULL, NULL, 0);
        t.who = "";
        return t;
    }
}

FtQuestTalk ft_quest_wren_talk(FtQuests* q) {
    /* Down the junction, once the things holding her are gone. */
    if(ft_quest_state(q, FT_QUEST_WREN) != FT_QUEST_ACTIVE) {
        return wren_line("Not now.", NULL, NULL);
    }

    ft_quest_advance(q, FT_QUEST_WREN, FT_QUEST_READY);

    FtQuestTalk t = wren_line("You're one of them.", "But you're not.",
                              "Take me home?");
    t.follows = true;
    return t;
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
