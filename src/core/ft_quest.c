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
 * test walks every branch of every conversation, every repeat included, and
 * measures them.
 *
 * Three rounds of this, so the notes are worth keeping:
 *
 *   1. Three lines in a box with the quest's name over them. A sign.
 *   2. Beats that alternate, but every line a clipped fragment in the same
 *      flat, grim voice. Read like a machine talking to a machine.
 *   3. This. People who are a bit funny, who each sound like themselves,
 *      and who do not recite the same scene at you twice.
 *
 * What the player said landed in round 2 was the joke — "Coll does the
 * talking round here." / "And you?" / "I do the standing." — so that is the
 * register: warm, dry, short, and one laugh a conversation where there is
 * room for it. Twenty characters is room for a joke. It is not room for
 * exposition, so nobody explains anything they would not say out loud.
 *
 * Voices, so a line could only be one person's:
 *
 *   The Keeper   old, dry, fond of you and pretending not to be
 *   Coll         short with you because she is frightened; softens late
 *   Hale         easy, lazy-cheerful, a bit scared of holes
 *   Wren         a kid. Talks in capitals when she means it. Not scared.
 */

typedef struct {
    const FtBeat* beats;
    uint8_t       count;
} FtLines;

#define LINES(arr) {arr, (uint8_t)(sizeof(arr) / sizeof((arr)[0]))}
#define COUNT_OF(arr) ((uint8_t)(sizeof(arr) / sizeof((arr)[0])))

/* The whole conversation the first time, then round the short ones. */
static FtTalk say(const char* who, FtVoice voice, FtLines first, const FtLines* more,
                  uint8_t n_more, uint8_t again) {
    FtLines use = first;
    if(again > 0u && n_more > 0u) use = more[(uint8_t)(again - 1u) % n_more];

    FtTalk t;
    t.speaker = who;
    t.voice = voice;
    t.beats = use.beats;
    t.count = use.count;
    t.ask = false;
    t.yes = NULL;
    t.no = NULL;
    t.you = NULL;
    return t;
}

static FtTalk ask(FtTalk t, const char* yes, const char* no) {
    t.ask = true;
    t.yes = yes;
    t.no = no;
    return t;
}

/* ---- The Keeper, in Cold Boot ---- */

static const char* KEEPER = "The Keeper";

/* The first thing anybody says to you. You are brand new — he built you,
 * after the Silence — and he hands you the sealed message straight away and
 * will not say who it is for (STORY.md §6, and the threads in §7). The
 * favour at the end is a test he does not call a test. */
static const FtBeat KEEPER_OFFER[] = {
    {FT_SAY_THEM, "Oh good, you work.", "Wasn't sure."},
    {FT_SAY_YOU,  "Who are you?", NULL},
    {FT_SAY_THEM, "The Keeper. I built", "you. Don't stare."},
    {FT_SAY_THEM, "This is for you to", "carry. It's sealed."},
    {FT_SAY_YOU,  "What is it?", NULL},
    {FT_SAY_THEM, "A message. Who for,", "you'll learn later."},
    {FT_SAY_THEM, "First, a favour. The", "Gate and back."},
    {FT_SAY_THEM, "No fighting. I need", "to know you're kind."},
};
static const FtBeat KEEPER_OFFER_2[] = {
    {FT_SAY_THEM, "Back for that", "favour, then?"},
    {FT_SAY_THEM, "Gate and back.", "No fighting."},
};
static const FtLines KEEPER_OFFER_MORE[] = {LINES(KEEPER_OFFER_2)};

static const FtBeat KEEPER_ON[] = {
    {FT_SAY_THEM, "Still no fights?", NULL},
    {FT_SAY_YOU,  "Not one.", NULL},
    {FT_SAY_THEM, "Good. Four rooms", "out, four back."},
};
static const FtBeat KEEPER_ON_2[] = {
    {FT_SAY_THEM, "Go on. The Gate", "won't come to you."},
};
static const FtBeat KEEPER_ON_3[] = {
    {FT_SAY_THEM, "Still here? I can", "hear you not going."},
};
static const FtLines KEEPER_ON_MORE[] = {LINES(KEEPER_ON_2), LINES(KEEPER_ON_3)};

/* The first hint that there was a Courier before you, and how it went. */
static const FtBeat KEEPER_FAILED[] = {
    {FT_SAY_THEM, "You've got that", "fought-in look."},
    {FT_SAY_YOU,  "It started it.", NULL},
    {FT_SAY_THEM, "The last one I built", "said that too."},
    {FT_SAY_THEM, "...Go on. Again.", NULL},
};
static const FtBeat KEEPER_FAILED_2[] = {
    {FT_SAY_THEM, "Walk round them.", "It's allowed."},
};
static const FtLines KEEPER_FAILED_MORE[] = {LINES(KEEPER_FAILED_2)};

static const FtBeat KEEPER_PAID[] = {
    {FT_SAY_THEM, "Not a scratch on", "you. Well done."},
    {FT_SAY_YOU,  "Easy.", NULL},
    {FT_SAY_THEM, "Don't get cocky.", NULL},
    {FT_SAY_THEM, "I've waited a long", "time for you."},
    {FT_SAY_THEM, "Two orbs. Now go", "east. Keep going."},
};

static const FtBeat KEEPER_DONE_1[] = {
    {FT_SAY_THEM, "That's all I've got.", "Go be a hero."},
};
static const FtBeat KEEPER_DONE_2[] = {
    {FT_SAY_THEM, "Still here. Still", "old."},
};
static const FtBeat KEEPER_DONE_3[] = {
    {FT_SAY_YOU,  "Any more favours?", NULL},
    {FT_SAY_THEM, "Ask me in a year.", NULL},
};
static const FtLines KEEPER_DONE_MORE[] = {LINES(KEEPER_DONE_2), LINES(KEEPER_DONE_3)};

/* ---- Warden Coll, at Weldhome's gate ---- */

static const char* COLL = "Warden Coll";

static const FtBeat COLL_OFFER[] = {
    {FT_SAY_THEM, "Stop. Gate's shut.", NULL},
    {FT_SAY_YOU,  "Can you open it?", NULL},
    {FT_SAY_THEM, "Could. Won't.", NULL},
    {FT_SAY_THEM, "You walk like the", "things that take us."},
    {FT_SAY_YOU,  "I walk like me.", NULL},
    {FT_SAY_THEM, "Hm. Prove it, then.", NULL},
    {FT_SAY_THEM, "A kid's missing.", "Wren. Two days now."},
    {FT_SAY_THEM, "Hale knows where.", "Go with him."},
};
static const FtBeat COLL_OFFER_2[] = {
    {FT_SAY_THEM, "Back again. Kid's", "still gone."},
    {FT_SAY_THEM, "Going with Hale?", NULL},
};
static const FtLines COLL_OFFER_MORE[] = {LINES(COLL_OFFER_2)};

static const FtBeat COLL_ON[] = {
    {FT_SAY_THEM, "Why are you still", "talking to me?"},
    {FT_SAY_YOU,  "Just checking in.", NULL},
    {FT_SAY_THEM, "Check in on Wren.", NULL},
};
static const FtBeat COLL_ON_2[] = {
    {FT_SAY_THEM, "Hale. Grass. Wren.", "Go."},
};
static const FtBeat COLL_ON_3[] = {
    {FT_SAY_THEM, "Every minute you", "stand here, she's"},
    {FT_SAY_THEM, "down there. Go.", NULL},
};
static const FtLines COLL_ON_MORE[] = {LINES(COLL_ON_2), LINES(COLL_ON_3)};

/* Nothing fails Wren's quest, but a state with no lines is a bug waiting. */
static const FtBeat COLL_FAILED[] = {
    {FT_SAY_THEM, "She's still out", "there, isn't she."},
};

static const FtBeat COLL_PAID[] = {
    {FT_SAY_THEM, "Wren! Inside. Now.", NULL},
    {FT_SAY_THEM, "...Thank you.", NULL},
    {FT_SAY_YOU,  "She named me.", "I'm @."},
    {FT_SAY_THEM, "Course she did.", NULL},
    {FT_SAY_THEM, "Gate's open. You've", "earned it."},

    /* The first real thing anybody tells you, and the set-up for Echo
     * (STORY.md §7): it looked like you, it only said Hush's words, and it
     * went the way you are about to go. */
    {FT_SAY_THEM, "You're not the first", "one through here."},
    {FT_SAY_YOU,  "Who was?", NULL},
    {FT_SAY_THEM, "One like you. Same", "box head."},
    {FT_SAY_THEM, "Didn't talk. Just", "said 'Please hold.'"},
    {FT_SAY_THEM, "Went east. Never", "came back."},
};

static const FtBeat COLL_DONE_1[] = {
    {FT_SAY_THEM, "East gate's open.", "Go on, @."},
};
static const FtBeat COLL_DONE_2[] = {
    {FT_SAY_THEM, "Wren's grounded.", "Forever."},
};
static const FtBeat COLL_DONE_3[] = {
    {FT_SAY_YOU,  "Thanks, Coll.", NULL},
    {FT_SAY_THEM, "Don't make it weird.", "...@."},
};
static const FtBeat COLL_DONE_4[] = {
    {FT_SAY_THEM, "Still watching you,", "you know."},
};
static const FtBeat COLL_DONE_5[] = {
    {FT_SAY_YOU,  "The other one...", NULL},
    {FT_SAY_THEM, "Please hold. That's", "all it ever said."},
};
static const FtLines COLL_DONE_MORE[] = {LINES(COLL_DONE_2), LINES(COLL_DONE_5),
                                         LINES(COLL_DONE_3), LINES(COLL_DONE_4)};

/* ---- Hale, the other guard on the gate ---- */

static const char* HALE = "Hale";

static const FtBeat HALE_IDLE[] = {
    {FT_SAY_THEM, "Coll does the", "talking round here."},
    {FT_SAY_YOU,  "And you?", NULL},
    {FT_SAY_THEM, "I do the standing.", NULL},
};
static const FtBeat HALE_IDLE_2[] = {
    {FT_SAY_THEM, "Still standing.", "Going great."},
};
static const FtBeat HALE_IDLE_3[] = {
    {FT_SAY_THEM, "Ask Coll. She loves", "being asked things."},
};
static const FtBeat HALE_IDLE_4[] = {
    {FT_SAY_THEM, "Nice day for", "standing, this."},
};
static const FtLines HALE_IDLE_MORE[] = {LINES(HALE_IDLE_2), LINES(HALE_IDLE_3),
                                         LINES(HALE_IDLE_4)};

/* You turned back before he got you there. Talking sets him off again. */
static const FtBeat HALE_AGAIN[] = {
    {FT_SAY_THEM, "Lost your nerve?", NULL},
    {FT_SAY_YOU,  "Show me again.", NULL},
    {FT_SAY_THEM, "Stay close, then.", NULL},
};

static const FtBeat HALE_PITSIDE[] = {
    {FT_SAY_THEM, "She's down there.", "Bet you anything."},
    {FT_SAY_YOU,  "You're not coming?", NULL},
    {FT_SAY_THEM, "Someone has to", "guard the hole."},
    {FT_SAY_THEM, "...Important job.", NULL},
};
static const FtBeat HALE_PITSIDE_2[] = {
    {FT_SAY_THEM, "Still guarding it.", "Hole's still here."},
};
static const FtBeat HALE_PITSIDE_3[] = {
    {FT_SAY_THEM, "It's a standing job", "too. I'm a natural."},
};
static const FtLines HALE_PITSIDE_MORE[] = {LINES(HALE_PITSIDE_2), LINES(HALE_PITSIDE_3)};

/* Back at the gate, with the pit found and Wren still down it. */
static const FtBeat HALE_KNOWN[] = {
    {FT_SAY_THEM, "You know the way.", "Long grass, south."},
    {FT_SAY_THEM, "Hole in the middle.", "Can't miss it."},
};
static const FtBeat HALE_KNOWN_2[] = {
    {FT_SAY_THEM, "Grass. Hole. Kid.", NULL},
};
static const FtLines HALE_KNOWN_MORE[] = {LINES(HALE_KNOWN_2)};

static const FtBeat HALE_HOME[] = {
    {FT_SAY_THEM, "She's back!", NULL},
    {FT_SAY_THEM, "Nice one, #.", NULL},
    {FT_SAY_YOU,  "It's @.", NULL},
    {FT_SAY_THEM, "That's what I said.", NULL},
    {FT_SAY_THEM, "Coll almost smiled.", "I saw it."},
    {FT_SAY_YOU,  "Did she?", NULL},
    {FT_SAY_THEM, "Almost.", NULL},
};
static const FtBeat HALE_HOME_2[] = {
    {FT_SAY_THEM, "Back to standing.", "Living the dream."},
};
static const FtBeat HALE_HOME_3[] = {
    {FT_SAY_THEM, "If you find more", "holes, don't."},
};
static const FtLines HALE_HOME_MORE[] = {LINES(HALE_HOME_2), LINES(HALE_HOME_3)};

/* ---- Wren ---- */

static const char* WREN = "Wren";

static const FtBeat WREN_FOUND[] = {
    {FT_SAY_THEM, "Don't come closer!", "I bite!"},
    {FT_SAY_YOU,  "Coll sent me.", NULL},
    {FT_SAY_THEM, "...Is she mad?", NULL},
    {FT_SAY_YOU,  "Very.", NULL},
    {FT_SAY_THEM, "Yeah. That's Coll.", NULL},
    {FT_SAY_THEM, "I wasn't scared.", "I was HIDING."},
    {FT_SAY_YOU,  "Can you walk?", NULL},
    {FT_SAY_THEM, "I can RUN.", NULL},
};

/* Found, then lost on the way home: a reload, or you wandered off. */
static const FtBeat WREN_LOST[] = {
    {FT_SAY_THEM, "You LEFT me!", NULL},
    {FT_SAY_YOU,  "Sorry. Come on.", NULL},
    {FT_SAY_THEM, "Don't do it again.", NULL},
};

/* Only reachable by the debug travel menu, but it should still be her. */
static const FtBeat WREN_WAIT[] = {
    {FT_SAY_THEM, "Go away. I'm", "hiding."},
};

static const FtBeat WREN_HOME_1[] = {
    {FT_SAY_THEM, "I'm grounded.", NULL},
    {FT_SAY_THEM, "Worth it, @.", NULL},
};
static const FtBeat WREN_HOME_2[] = {
    {FT_SAY_THEM, "Next time take me", "somewhere good."},
};
static const FtBeat WREN_HOME_3[] = {
    {FT_SAY_THEM, "Coll says you're", "okay. For a robot."},
};
static const FtBeat WREN_HOME_4[] = {
    {FT_SAY_THEM, "Are there more", "holes? For science."},
};
static const FtLines WREN_HOME_MORE[] = {LINES(WREN_HOME_2), LINES(WREN_HOME_3),
                                         LINES(WREN_HOME_4)};

/* ---- Your name ---- */

/* STORY.md §5. Five names, all of them what a kid sees when she looks at
you — a box, a screen for a face, short legs, the nub on top — and the last
is the one you get for being fussy. */
static const char* const NAMES[FT_NAME_COUNT] = {"Boxy", "Telly", "Stumpy", "Nubbin",
                                                 "Lunchbox"};

/* Hale's, one each. Close enough to be a joke and never close enough to be
 * a mistake. */
static const char* const HALE_NAMES[FT_NAME_COUNT] = {"Roxy", "Kelly", "Grumpy", "Muffin",
                                                      "Lunchbag"};

const char* ft_quest_name(uint8_t name) {
    return (name < FT_NAME_COUNT) ? NAMES[name] : "robot";
}

const char* ft_quest_hale_name(uint8_t name) {
    return (name < FT_NAME_COUNT) ? HALE_NAMES[name] : "pal";
}

char* ft_quest_expand(const char* line, uint8_t name, char* out, uint8_t cap) {
    if(out == NULL || cap == 0u) return out;

    uint8_t n = 0;
    for(const char* c = line; c && *c; c++) {
        const char* sub = (*c == '@') ? ft_quest_name(name) :
                          (*c == '#') ? ft_quest_hale_name(name) :
                                        NULL;
        if(sub) {
            while(*sub && n + 1u < cap) out[n++] = *sub++;
        } else if(n + 1u < cap) {
            out[n++] = *c;
        }
    }
    out[n] = '\0';
    return out;
}

/* She stops you on the way home. Each name you turn down, she tries harder
 * and minds more. */
static const FtBeat NAMING_0[] = {
    {FT_SAY_THEM, "Hey. Robot.", NULL},
    {FT_SAY_YOU,  "Yes?", NULL},
    {FT_SAY_THEM, "You need a name.", "Robot's rude."},
    {FT_SAY_THEM, "You're a box. With", "legs. So... Boxy."},
};
static const FtBeat NAMING_1[] = {
    {FT_SAY_THEM, "Okay. Um.", NULL},
    {FT_SAY_THEM, "Your face is a TV.", "Telly!"},
    {FT_SAY_YOU,  "It's a screen.", NULL},
    {FT_SAY_THEM, "TELLY.", NULL},
};
static const FtBeat NAMING_2[] = {
    {FT_SAY_THEM, "Fine. FINE.", NULL},
    {FT_SAY_THEM, "Your legs are SO", "short. Stumpy!"},
};
static const FtBeat NAMING_3[] = {
    {FT_SAY_THEM, "You're SO picky.", NULL},
    {FT_SAY_THEM, "You've got a nub on", "your head. Nubbin."},
};
static const FtBeat NAMING_4[] = {
    {FT_SAY_THEM, "FINE. You're", "Lunchbox."},
    {FT_SAY_THEM, "Forever.", NULL},
    {FT_SAY_YOU,  "...Lunchbox.", NULL},
    {FT_SAY_THEM, "I'm SO hungry.", "It's a compliment."},
};
static const FtBeat NAMED[] = {
    {FT_SAY_THEM, "Hi, @!", NULL},
    {FT_SAY_YOU,  "Hi, Wren.", NULL},
    {FT_SAY_THEM, "Okay. Home now.", "I'm SO hungry."},
};

/* ---- The opening ---- */

/* Hush, the first voice in the game, before you have a body to hear it
 * with. Polite, level, and it never gets to finish. */
static const char* const INTRO[FT_INTRO_CARDS][FT_INTRO_CARD_LINES] = {
    {"Good evening.", "This is the", "Carrier."},
    {"Your call is", "important to us.", NULL},
    {"All lines are", "closed, for your", "safety."},
    {"Please stay home.", NULL, NULL},
    {"Please hold.", NULL, NULL},
    {"Please hold.", NULL, NULL},
    {"Please ho", NULL, NULL},
};

const char* ft_quest_intro_line(uint8_t card, uint8_t line) {
    if(card >= FT_INTRO_CARDS || line >= FT_INTRO_CARD_LINES) return NULL;
    return INTRO[card][line];
}

/* ---- Barks ---- */

static const char* const BARKS[FT_BARK_COUNT] = {
    [FT_BARK_HALE_SET_OFF]  = "Right. Follow me!",
    [FT_BARK_HALE_KEEP_UP]  = "Keep up!",
    [FT_BARK_HALE_COMING]   = "You coming?",
    [FT_BARK_HALE_THIS_WAY] = "This way!",
    [FT_BARK_HALE_FOUND]    = "There! After you.",
    [FT_BARK_HALE_WAIT]     = "Wait for me!",
    [FT_BARK_HALE_POSTED]   = "Back to standing.",
    [FT_BARK_HALE_TURNED]   = "Oh. Home, then.",
    [FT_BARK_WREN_START]    = "Don't walk fast!",

    /* On the walk home, in this order, one every few seconds. */
    [FT_BARK_WREN_CHATTER + 0] = "I wasn't scared.",
    [FT_BARK_WREN_CHATTER + 1] = "You walk funny.",
    [FT_BARK_WREN_CHATTER + 2] = "Coll's gonna yell.",
    [FT_BARK_WREN_CHATTER + 3] = "Are you a robot?",
    [FT_BARK_WREN_CHATTER + 4] = "...Cool.",
    [FT_BARK_WREN_CHATTER + 5] = "Can I hold an orb?",
    [FT_BARK_WREN_CHATTER + 6] = "I'm SO hungry.",
    [FT_BARK_WREN_CHATTER + 7] = "Are we there yet?",

    /* The terminal, when you save at it. */
    [FT_BARK_HUSH + 0] = "Please hold.",
    [FT_BARK_HUSH + 1] = "Please stay home.",
    [FT_BARK_HUSH + 2] = "Lines are closed.",
    [FT_BARK_HUSH + 3] = "We value you.",

    /* Echo, the first time you see it. The only words it has. */
    [FT_BARK_ECHO] = "Please hold.",
};

const char* ft_quest_bark(uint8_t bark) {
    return (bark < FT_BARK_COUNT && BARKS[bark]) ? BARKS[bark] : "";
}

/* ---- Dispatch ---------------------------------------------------------- */

/* A compound literal here, because LINES is a brace initializer and cannot
 * be passed as an argument on its own. */
#define ONLY(arr) (FtLines)LINES(arr), NULL, 0u
#define WITH(arr, more) (FtLines)LINES(arr), more, COUNT_OF(more)

static FtTalk keeper_talk(FtQuestState at, uint8_t again) {
    switch(at) {
    case FT_QUEST_UNKNOWN:
        return ask(say(KEEPER, FT_VOICE_KEEPER, WITH(KEEPER_OFFER, KEEPER_OFFER_MORE), again),
                   "Deal", "Later");
    case FT_QUEST_ACTIVE:
        return say(KEEPER, FT_VOICE_KEEPER, WITH(KEEPER_ON, KEEPER_ON_MORE), again);
    case FT_QUEST_FAILED:
        return say(KEEPER, FT_VOICE_KEEPER, WITH(KEEPER_FAILED, KEEPER_FAILED_MORE), again);
    case FT_QUEST_READY:
        return say(KEEPER, FT_VOICE_KEEPER, ONLY(KEEPER_PAID), again);
    case FT_QUEST_DONE:
    default:
        return say(KEEPER, FT_VOICE_KEEPER, WITH(KEEPER_DONE_1, KEEPER_DONE_MORE), again);
    }
}

static FtTalk coll_talk(FtQuestState at, uint8_t again) {
    switch(at) {
    case FT_QUEST_UNKNOWN:
        return ask(say(COLL, FT_VOICE_COLL, WITH(COLL_OFFER, COLL_OFFER_MORE), again),
                   "I'll go", "Not yet");
    case FT_QUEST_ACTIVE:
        return say(COLL, FT_VOICE_COLL, WITH(COLL_ON, COLL_ON_MORE), again);
    case FT_QUEST_FAILED:
        return say(COLL, FT_VOICE_COLL, ONLY(COLL_FAILED), again);
    case FT_QUEST_READY:
        return say(COLL, FT_VOICE_COLL, ONLY(COLL_PAID), again);
    case FT_QUEST_DONE:
    default:
        return say(COLL, FT_VOICE_COLL, WITH(COLL_DONE_1, COLL_DONE_MORE), again);
    }
}

FtTalk ft_quest_talk(const FtQuests* q, FtQuestId id, uint8_t again) {
    const FtQuestState at = ft_quest_state(q, id);

    switch(id) {
    case FT_QUEST_CLEAN_RUN: return keeper_talk(at, again);
    case FT_QUEST_WREN:      return coll_talk(at, again);
    default:                 return say("", FT_VOICE_YOU, ONLY(WREN_WAIT), again);
    }
}

FtTalk ft_quest_wren_talk(const FtQuests* q, uint8_t again) {
    switch(ft_quest_state(q, FT_QUEST_WREN)) {
    case FT_QUEST_ACTIVE: return say(WREN, FT_VOICE_WREN, ONLY(WREN_FOUND), again);
    case FT_QUEST_READY:  return say(WREN, FT_VOICE_WREN, ONLY(WREN_LOST), again);
    case FT_QUEST_DONE:
        return say(WREN, FT_VOICE_WREN, WITH(WREN_HOME_1, WREN_HOME_MORE), again);
    case FT_QUEST_UNKNOWN:
    case FT_QUEST_FAILED:
    default:
        return say(WREN, FT_VOICE_WREN, ONLY(WREN_WAIT), again);
    }
}

FtTalk ft_quest_hale_talk(const FtQuests* q, bool pit_found, bool by_the_pit, uint8_t again) {
    const FtQuestState at = ft_quest_state(q, FT_QUEST_WREN);

    if(at == FT_QUEST_READY || at == FT_QUEST_DONE) {
        return say(HALE, FT_VOICE_HALE, WITH(HALE_HOME, HALE_HOME_MORE), again);
    }
    if(at != FT_QUEST_ACTIVE) {
        return say(HALE, FT_VOICE_HALE, WITH(HALE_IDLE, HALE_IDLE_MORE), again);
    }
    if(by_the_pit) return say(HALE, FT_VOICE_HALE, WITH(HALE_PITSIDE, HALE_PITSIDE_MORE), again);
    if(pit_found) return say(HALE, FT_VOICE_HALE, WITH(HALE_KNOWN, HALE_KNOWN_MORE), again);
    return say(HALE, FT_VOICE_HALE, ONLY(HALE_AGAIN), again);
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

    switch(ft_quest_state(q, FT_QUEST_WREN)) {
    case FT_QUEST_ACTIVE:
        set_state(q, FT_QUEST_WREN, FT_QUEST_READY);
        o.follows = true;
        break;

    case FT_QUEST_READY:
        /* Found already, and lost on the way home. She comes again — the
         * first version said "go away" here, and the kid you had already
         * rescued refused to be rescued twice. */
        o.follows = true;
        break;

    default:
        break;
    }
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

FtTalk ft_quest_naming_talk(uint8_t tries) {
    static const FtLines TRIES[FT_NAME_COUNT] = {
        LINES(NAMING_0), LINES(NAMING_1), LINES(NAMING_2), LINES(NAMING_3), LINES(NAMING_4),
    };

    /* Past the last name is her saying it, now that it is settled. */
    if(tries > FT_NAME_COUNT - 1u) return say(WREN, FT_VOICE_WREN, ONLY(NAMED), 0u);

    const FtTalk t = say(WREN, FT_VOICE_WREN, TRIES[tries], NULL, 0u, 0u);
    return (tries < FT_NAME_COUNT - 1u) ? ask(t, "That's me", "No") : t;
}
