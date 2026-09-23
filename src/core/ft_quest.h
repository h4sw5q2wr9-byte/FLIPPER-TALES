/* Quests: things someone asks you to do, and what they pay.
 *
 * Pure core like everything beside it. A quest here is a condition over the
 * state the world already tracks — which room you walked into, whether a
 * fight started — rather than a script with its own idea of where you are.
 * That is what keeps it testable, and what stops a quest and the world
 * disagreeing about what happened.
 *
 * Payment is in Orbs (see ft_progress.h), so a quest is worth exactly what a
 * level is worth, and what it buys is a stat you can move again later. */
#ifndef FT_QUEST_H
#define FT_QUEST_H

#include "ft_types.h"

typedef enum {
    /* "Get to the Cold Gate and back to me, and don't fight anything." */
    FT_QUEST_CLEAN_RUN = 0,

    /* Weldhome's gate does not open for a unit that walks like the things
     * taking their people. Bring the kid back and it will. See STORY.md 6. */
    FT_QUEST_WREN,

    /* The Scrapline has no line out, so it has no trade. Wake the relay,
     * east past the broken spans. Ma Rivet gives you Infrared to do it. */
    FT_QUEST_RIVET,

    FT_QUEST_COUNT
} FtQuestId;

typedef enum {
    FT_QUEST_UNKNOWN = 0, /* never offered */
    FT_QUEST_ACTIVE,      /* taken, and still on */
    FT_QUEST_FAILED,      /* the condition broke; can be taken again */
    FT_QUEST_READY,       /* done, go and collect */
    FT_QUEST_DONE
} FtQuestState;

/* One byte per quest, with headroom so adding a quest does not change the
 * save layout. ft_quest.c asserts the fit. */
#define FT_QUEST_BYTES 4

typedef struct {
    uint8_t state[FT_QUEST_BYTES];
} FtQuests;

/* What a quest is and what it wants. Data, so the rules below never have to
 * know which quest they are running. */
typedef struct {
    const char* name;      /* at most 20 characters */
    uint8_t     giver_room;

    /* Reaching this flips ACTIVE to READY. FT_QUEST_NO_ROOM for a quest that
     * is advanced by something other than arriving somewhere — freeing Wren
     * is a conversation, not a doorway. */
    uint8_t     goal_room;

    bool        no_battle; /* a fight fails it */

    /* What it pays: supplies, now that orbs are switched off. */
    uint8_t     reward_item;  /* an FtItemId */
    uint8_t     reward_count;
} FtQuestDef;

#define FT_QUEST_NO_ROOM 255

const FtQuestDef* ft_quest_def(FtQuestId id);

void         ft_quests_init(FtQuests* q);
FtQuestState ft_quest_state(const FtQuests* q, FtQuestId id);

/* Walking into a room. Advances anything waiting on this room. */
void ft_quest_enter_room(FtQuests* q, uint8_t room);

/* A fight started. Fails every live quest that asked you not to. */
void ft_quest_battle(FtQuests* q);

/* ---- Conversations ------------------------------------------------------
 *
 * A conversation is a list of beats, each one somebody saying up to two
 * lines, and it may end in a question.
 *
 * It used to be three lines in a box with the quest's name over them, which
 * is a sign, not a conversation: only one person ever spoke and the player
 * never said anything back. Beats alternate, the header names whoever is
 * talking, and the one that matters ends on a choice. */

typedef enum {
    FT_SAY_THEM = 0, /* whoever you are talking to */
    FT_SAY_YOU       /* the Courier */
} FtSayWho;

typedef struct {
    FtSayWho    who;
    const char* a;   /* at most 20 characters */
    const char* b;   /* a second line, or NULL */
} FtBeat;

#define FT_TALK_MAX_BEATS 14

/* Whose voice the text types out in. Every character gets a short blip of
 * their own pitch as their words appear, the way the genre has always done
 * it — a line with no sound under it reads like a sign. */
typedef enum {
    FT_VOICE_KEEPER = 0,
    FT_VOICE_COLL,
    FT_VOICE_HALE,
    FT_VOICE_WREN,
    FT_VOICE_YOU,
    FT_VOICE_HUSH, /* smooth, level, and much too calm */
    FT_VOICE_RIVET,
    FT_VOICE_COUNT
} FtVoice;

typedef struct {
    const char*   speaker; /* their name, for the header */
    FtVoice       voice;
    const FtBeat* beats;
    uint8_t       count;

    /* True when the last beat is a question. The app shows the two answers
     * and hands the result back to ft_quest_answer. */
    bool        ask;
    const char* yes;
    const char* no;

    /* What the header says when you are the one talking: your name, once
     * Wren has given you one. NULL reads "You". The app fills it in. */
    const char* you;
} FtTalk;

/* What this quest's giver says right now. Pure: talking does not change
 * anything until the conversation is finished, which is what lets a player
 * back out of a question they did not mean to open.
 *
 * `again` is how many times you have already talked to them about this.
 * Zero gets the whole conversation; after that they say something short,
 * and different each time round. Hearing a whole scene replayed word for
 * word is what makes a person read as a sign. The app keeps the count, and
 * resets it whenever the again==0 conversation changes (the story moved). */
FtTalk ft_quest_talk(const FtQuests* q, FtQuestId id, uint8_t again);

/* Talking to Wren herself. Her own function because she is not the giver:
 * the quest is Coll's, and Wren is the middle of it. */
FtTalk ft_quest_wren_talk(const FtQuests* q, uint8_t again);

/* What a finished conversation did. */
typedef struct {
    uint8_t item;    /* paid out: an FtItemId, if `items` is not 0 */
    uint8_t items;   /* how many; usually 0 */
    bool    follows; /* somebody just started walking with you */
    bool    ended;   /* the quest is now done */
    bool    leads;   /* somebody just set off to show you the way */
    bool    infrared; /* you were just handed Infrared */
} FtQuestOutcome;

/* Apply the outcome of a finished conversation. `yes` is only read by a
 * talk that asked; a refusal leaves everything exactly as it was. */
FtQuestOutcome ft_quest_answer(FtQuests* q, FtQuestId id, bool yes);
FtQuestOutcome ft_quest_wren_answer(FtQuests* q);

/* Hale, the other guard on the gate. He is not a giver either — he is how
 * you get where Coll sent you — so what he says depends on how far the Wren
 * quest has got, whether he has shown you the pit yet, and whether he is
 * standing beside it. Pure, like the others. */
FtTalk ft_quest_hale_talk(const FtQuests* q, bool pit_found, bool by_the_pit, uint8_t again);
FtQuestOutcome ft_quest_hale_answer(const FtQuests* q, bool pit_found, bool by_the_pit);

/* Having Infrared: Ma Rivet gave it to you with her job, and it is the job
 * that remembers it. */
bool ft_quest_has_infrared(const FtQuests* q);

/* The Keeper, on the Relay's terminal, once it is awake (STORY.md §6). */
FtTalk ft_quest_keeper_call(void);

/* ---- Your name -----------------------------------------------------------
 *
 * You do not have one until Wren gives you one, on the walk home from the
 * Hollow (STORY.md §5). She offers four and you can turn each down; turn
 * them all down and you are Lunchbox, forever. Saved as one number. */
#define FT_NAME_COUNT 5
#define FT_NAME_NONE  255u

/* Your name, or "robot" before you have one. */
const char* ft_quest_name(uint8_t name);

/* What Hale calls you instead. He gets it wrong on purpose, every time. */
const char* ft_quest_hale_name(uint8_t name);

/* Wren's naming, try `tries` (0 = the first name). The first four ask; the
 * fifth does not — by then she has decided. FT_NAME_COUNT is what she says
 * once you have said yes to one, and it says your name, so expand it. */
FtTalk ft_quest_naming_talk(uint8_t tries);

/* A line can say your name: '@' is your name and '#' is what Hale calls you.
 * Writes at most cap-1 characters and always terminates. Returns `out`. */
char* ft_quest_expand(const char* line, uint8_t name, char* out, uint8_t cap);

/* The longest either can come out ("Lunchbox", "Lunchbag"), so the tests can
 * measure every line with the worst name in it rather than with a single '@'. */
#define FT_NAME_MAX_CHARS 8

/* ---- The opening ----------------------------------------------------------
 *
 * Before you wake: Hush's announcement, on a terminal, one card at a time,
 * until it is cut off mid-word. Up to three lines a card, each at most 20
 * characters, like every other line in the game. */
#define FT_INTRO_CARDS      7
#define FT_INTRO_CARD_LINES 3

/* Narrower than a talk line: the words sit inside a drawn terminal, and a
 * twenty-character line ran off its screen. */
#define FT_INTRO_LINE_MAX   17

/* The lines of one card; unused lines are NULL. */
const char* ft_quest_intro_line(uint8_t card, uint8_t line);

/* ---- Barks --------------------------------------------------------------
 *
 * A line said out loud in the overworld, in a bubble over whoever said it,
 * without stopping anything: no box, no button. Hale says where he is going
 * while he walks you there, and Wren does not stop talking on the walk home,
 * because she is a kid and you rescued her. The world decides when; the words
 * live here with the rest of the words. */
typedef enum {
    FT_BARK_HALE_SET_OFF = 0,
    FT_BARK_HALE_KEEP_UP,
    FT_BARK_HALE_COMING,
    FT_BARK_HALE_THIS_WAY,
    FT_BARK_HALE_FOUND,
    FT_BARK_HALE_WAIT,
    FT_BARK_HALE_POSTED,
    FT_BARK_HALE_TURNED,
    FT_BARK_WREN_START,
    FT_BARK_WREN_CHATTER, /* the first of FT_BARK_WREN_CHATTER_N, in order */
} FtBark;

#define FT_BARK_WREN_CHATTER_N 8

/* Every terminal is Hush's, and it is polite to you when you save. In
 * order, one per use, after Wren's lines. */
#define FT_BARK_HUSH   ((uint8_t)FT_BARK_WREN_CHATTER + FT_BARK_WREN_CHATTER_N)
#define FT_BARK_HUSH_N 4

/* Echo, across the gap in the Scrapline. */
#define FT_BARK_ECHO   (FT_BARK_HUSH + FT_BARK_HUSH_N)

#define FT_BARK_COUNT (FT_BARK_ECHO + 1)

/* A bubble is narrower than the talk box: it sits over somebody's head. */
#define FT_BARK_MAX_CHARS 18

const char* ft_quest_bark(uint8_t bark);

/* What the Courier says to themselves at a way they have no reason to take.
 * The world asks this before refusing a gated exit. */
const char* ft_quest_refusal(FtQuestId id, FtQuestState need);

/* Has this quest reached at least `need`? The overworld's gated exits are
 * the only reason this exists as its own question. */
bool ft_quest_at_least(const FtQuests* q, FtQuestId id, FtQuestState need);

/* Force a state. The app uses it for the one transition that is neither a
 * doorway nor a conversation with the giver: Wren agreeing to follow. */
void ft_quest_advance(FtQuests* q, FtQuestId id, FtQuestState to);

/* The quest this room's giver carries, or -1 when nobody here has one. */
int ft_quest_for_room(uint8_t room);

/* One short word of status for the quest list, never NULL. */
const char* ft_quest_status_line(const FtQuests* q, FtQuestId id);

#endif /* FT_QUEST_H */
