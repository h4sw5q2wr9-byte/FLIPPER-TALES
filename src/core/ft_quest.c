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
        if(ft_quest_state(q, (FtQuestId)i) != FT_QUEST_ACTIVE) continue;
        if(d->goal_room == room) set_state(q, (FtQuestId)i, FT_QUEST_READY);
    }
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
    t.line[0] = a;
    t.line[1] = b;
    t.line[2] = c;
    t.lines = (uint8_t)(c ? 3u : (b ? 2u : 1u));
    t.orbs = orbs;
    return t;
}

FtQuestTalk ft_quest_talk(FtQuests* q, FtQuestId id) {
    if(id >= FT_QUEST_COUNT) return say3("...", NULL, NULL, 0);

    switch(ft_quest_state(q, id)) {
    case FT_QUEST_UNKNOWN:
        set_state(q, id, FT_QUEST_ACTIVE);
        return say3("Touch the Cold Gate", "and come back here.",
                    "No fights. 2 orbs.", 0);

    case FT_QUEST_ACTIVE:
        return say3("Still no fights?", "Good. The gate is", "four rooms east.", 0);

    case FT_QUEST_FAILED:
        /* Failing re-offers rather than closing the door: the quest is a
         * route to practise, and one that can only be failed once is a
         * punishment for trying it early. */
        set_state(q, id, FT_QUEST_ACTIVE);
        return say3("You fought one.", "Start over: gate,", "back, no fights.", 0);

    case FT_QUEST_READY:
        set_state(q, id, FT_QUEST_DONE);
        return say3("Clean the whole way.", "Take these.", "+2 orbs", FT_QUESTS[id].reward_orbs);

    case FT_QUEST_DONE:
    default:
        return say3("Nothing else for", "you today.", NULL, 0);
    }
}

const char* ft_quest_status_line(const FtQuests* q, FtQuestId id) {
    switch(ft_quest_state(q, id)) {
    case FT_QUEST_ACTIVE: return "Clean Run: on";
    case FT_QUEST_FAILED: return "Clean Run: failed";
    case FT_QUEST_READY:  return "Clean Run: collect";
    case FT_QUEST_DONE:   return "Clean Run: done";
    case FT_QUEST_UNKNOWN:
    default:              return "Clean Run: not met";
    }
}
