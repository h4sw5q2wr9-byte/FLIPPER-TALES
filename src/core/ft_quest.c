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
 * Beats alternate on purpose. The first version was three lines in a box with
 * the quest's name over them, so only one person ever spoke and the player
 * never answered.
 *
 * The second version alternated but still read like a machine: every line was
 * a clipped three-word fragment, nobody used a contraction, and all three
 * characters had the same flat voice. Twenty characters a line is a tight
 * budget, and the trap it sets is writing stubs. A beat has TWO lines, which
 * is forty characters — enough for a whole sentence, spoken the way a person
 * would speak it — and the lines below use both wherever the sentence wants
 * the room.
 *
 * Each of the three has a voice of their own, because that is most of what
 * makes written dialogue read as people: the Keeper is dry and old and has
 * decided to look after you; Coll is frightened and busy and says so by
 * being short with you; Wren is a kid, so she says too much and then says
 * something true by accident. */

#define TALK(spk, arr) {spk, arr, (uint8_t)(sizeof(arr) / sizeof((arr)[0])), \
                        false, NULL, NULL}
#define ASK(spk, arr, y, n) {spk, arr, (uint8_t)(sizeof(arr) / sizeof((arr)[0])), \
                             true, y, n}

/* ---- The Keeper, in Cold Boot ---- */

static const char* KEEPER = "The Keeper";

static const FtBeat KEEPER_OFFER[] = {
    {FT_SAY_THEM, "Well. Look at that.", "You're up."},
    {FT_SAY_YOU,  "How long was I out?", NULL},
    {FT_SAY_THEM, "Long enough that I", "stopped checking."},
    {FT_SAY_YOU,  "Where is this?", NULL},
    {FT_SAY_THEM, "Cold Boot. Last stop", "on a line that died."},
    {FT_SAY_THEM, "Do me a favour and", "I'll see you paid."},
    {FT_SAY_YOU,  "What sort of favour?", NULL},
    {FT_SAY_THEM, "Go east to the Gate", "and back. No fights."},
};

static const FtBeat KEEPER_ON[] = {
    {FT_SAY_THEM, "Still no fights?", NULL},
    {FT_SAY_YOU,  "None yet.", NULL},
    {FT_SAY_THEM, "Four rooms east.", "Four rooms back."},
    {FT_SAY_THEM, "The walking out is", "the easy half."},
};

static const FtBeat KEEPER_FAILED[] = {
    {FT_SAY_THEM, "You've been in a", "fight. I can tell."},
    {FT_SAY_YOU,  "It found me first.", NULL},
    {FT_SAY_THEM, "They do that.", NULL},
    {FT_SAY_THEM, "Start again when", "you're ready to."},
};

static const FtBeat KEEPER_PAID[] = {
    {FT_SAY_THEM, "All the way out and", "back, untouched."},
    {FT_SAY_YOU,  "Nothing laid a hand", "on me."},
    {FT_SAY_THEM, "Then I owe you.", NULL},
    {FT_SAY_THEM, "Two orbs. Don't", "spend them all here."},
};

static const FtBeat KEEPER_DONE[] = {
    {FT_SAY_THEM, "That's all I have", "to give you."},
    {FT_SAY_THEM, "Go carefully.", NULL},
};

/* ---- Warden Coll, at Weldhome's gate ---- */

static const char* COLL = "Warden Coll";

static const FtBeat COLL_OFFER[] = {
    {FT_SAY_THEM, "Gate's shut. Week", "now, going on two."},
    {FT_SAY_YOU,  "I just want through.", NULL},
    {FT_SAY_THEM, "So does everybody.", NULL},
    {FT_SAY_THEM, "You move like the", "things that take us."},
    {FT_SAY_YOU,  "I'm not one of them.", NULL},
    {FT_SAY_THEM, "Then prove it. A", "girl's gone. Wren."},
    {FT_SAY_YOU,  "Gone where?", NULL},
    {FT_SAY_THEM, "Hale knows. Go with", "him. Bring her back."},
};

static const FtBeat COLL_ON[] = {
    {FT_SAY_THEM, "Hale knows where she", "went. Go with him."},
    {FT_SAY_YOU,  "Why would she go", "down there?"},
    {FT_SAY_THEM, "Because I told her", "not to."},
};

static const FtBeat COLL_FAILED[] = {
    {FT_SAY_THEM, "She's still out", "there, isn't she."},
    {FT_SAY_YOU,  "I know.", NULL},
    {FT_SAY_THEM, "Then don't stand", "here telling me."},
};

static const FtBeat COLL_PAID[] = {
    {FT_SAY_THEM, "Wren. Inside. Now.", NULL},
    {FT_SAY_YOU,  "She isn't hurt.", NULL},
    {FT_SAY_THEM, "I can see that.", NULL},
    {FT_SAY_THEM, "Gate's open to you.", NULL},
    {FT_SAY_YOU,  "Just like that?", NULL},
    {FT_SAY_THEM, "You're not the first", "one through here."},
    {FT_SAY_YOU,  "Who was?", NULL},
    {FT_SAY_THEM, "Ask me again when", "you've seen a span."},
};

static const FtBeat COLL_DONE[] = {
    {FT_SAY_THEM, "Gate's open. Go on.", NULL},
    {FT_SAY_YOU,  "Thanks.", NULL},
    {FT_SAY_THEM, "Mind the spans.", NULL},
};

/* ---- Hale, the other guard on the gate ----
 *
 * Young, easy, and more frightened of what is under the grass than he lets
 * on. Coll talks; he does the walking. */

static const char* HALE = "Hale";

static const FtBeat HALE_IDLE[] = {
    {FT_SAY_THEM, "Coll does the", "talking round here."},
    {FT_SAY_YOU,  "And you?", NULL},
    {FT_SAY_THEM, "I do the standing.", NULL},
};

/* You turned back before he got you there. Talking sets him off again. */
static const FtBeat HALE_AGAIN[] = {
    {FT_SAY_THEM, "Changed your mind?", NULL},
    {FT_SAY_YOU,  "Show me again.", NULL},
    {FT_SAY_THEM, "Stay close, then.", NULL},
};

static const FtBeat HALE_PITSIDE[] = {
    {FT_SAY_THEM, "She went down there.", "I'd bet on it."},
    {FT_SAY_YOU,  "You're not coming?", NULL},
    {FT_SAY_THEM, "Someone has to be", "here when you're up."},
};

/* Back at the gate, with the pit found and Wren still down it. */
static const FtBeat HALE_KNOWN[] = {
    {FT_SAY_THEM, "You know the way", "now. The long grass."},
    {FT_SAY_YOU,  "I'm going back.", NULL},
    {FT_SAY_THEM, "Eat something first.", NULL},
};

static const FtBeat HALE_HOME[] = {
    {FT_SAY_THEM, "She's home.", NULL},
    {FT_SAY_THEM, "Thank you. Really.", NULL},
};

/* ---- Wren, at the end of the junction ---- */

static const char* WREN = "Wren";

static const FtBeat WREN_FOUND[] = {
    {FT_SAY_THEM, "Stay back. I mean", "it. Stay back."},
    {FT_SAY_YOU,  "Coll sent me.", NULL},
    {FT_SAY_THEM, "...Coll sent you.", NULL},
    {FT_SAY_YOU,  "She's angry.", NULL},
    {FT_SAY_THEM, "She's always angry.", NULL},
    {FT_SAY_THEM, "That's how I know", "you're not lying."},
    {FT_SAY_YOU,  "Can you walk?", NULL},
    {FT_SAY_THEM, "I can run.", NULL},
};

static const FtBeat WREN_WAIT[] = {
    {FT_SAY_THEM, "Whoever you are,", "go away."},
};

/* ---- Dispatch ---------------------------------------------------------- */

static FtTalk keeper_talk(FtQuestState at) {
    switch(at) {
    case FT_QUEST_UNKNOWN: return (FtTalk)ASK(KEEPER, KEEPER_OFFER, "All right", "Later");
    case FT_QUEST_ACTIVE:  return (FtTalk)TALK(KEEPER, KEEPER_ON);
    case FT_QUEST_FAILED:  return (FtTalk)TALK(KEEPER, KEEPER_FAILED);
    case FT_QUEST_READY:   return (FtTalk)TALK(KEEPER, KEEPER_PAID);
    case FT_QUEST_DONE:
    default:               return (FtTalk)TALK(KEEPER, KEEPER_DONE);
    }
}

static FtTalk coll_talk(FtQuestState at) {
    switch(at) {
    case FT_QUEST_UNKNOWN: return (FtTalk)ASK(COLL, COLL_OFFER, "I'll go", "I can't");
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

FtTalk ft_quest_hale_talk(const FtQuests* q, bool pit_found, bool by_the_pit) {
    const FtQuestState at = ft_quest_state(q, FT_QUEST_WREN);

    if(at == FT_QUEST_READY || at == FT_QUEST_DONE) return (FtTalk)TALK(HALE, HALE_HOME);
    if(at != FT_QUEST_ACTIVE) return (FtTalk)TALK(HALE, HALE_IDLE);

    if(by_the_pit) return (FtTalk)TALK(HALE, HALE_PITSIDE);
    if(pit_found) return (FtTalk)TALK(HALE, HALE_KNOWN);
    return (FtTalk)TALK(HALE, HALE_AGAIN);
}

/* ---- What a finished conversation did ---------------------------------- */

static FtQuestOutcome nothing(void) {
    FtQuestOutcome o = {0, false, false, false};
    return o;
}

FtQuestOutcome ft_quest_answer(FtQuests* q, FtQuestId id, bool yes) {
    FtQuestOutcome o = nothing();
    if(id >= FT_QUEST_COUNT) return o;

    switch(ft_quest_state(q, id)) {
    case FT_QUEST_UNKNOWN:
        /* Saying no leaves everything exactly as it was, so a question you
         * did not mean to open costs nothing. */
        if(yes) {
            set_state(q, id, FT_QUEST_ACTIVE);

            /* Coll's last line is "Hale knows. Go with him." — so he goes,
             * the moment you say yes, and following him is the next thing
             * the game asks of you. */
            if(id == FT_QUEST_WREN) o.leads = true;
        }
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

FtQuestOutcome ft_quest_hale_answer(const FtQuests* q, bool pit_found, bool by_the_pit) {
    FtQuestOutcome o = nothing();

    /* Only one of his conversations does anything: the one where you turned
     * back before he got you there, and asked again. */
    if(ft_quest_state(q, FT_QUEST_WREN) == FT_QUEST_ACTIVE && !pit_found && !by_the_pit) {
        o.leads = true;
    }
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
