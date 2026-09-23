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
    int16_t     reward_orbs;
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

#define FT_TALK_MAX_BEATS 8

/* Whose voice the text types out in. Every character gets a short blip of
 * their own pitch as their words appear, the way the genre has always done
 * it — a line with no sound under it reads like a sign. */
typedef enum {
    FT_VOICE_KEEPER = 0,
    FT_VOICE_COLL,
    FT_VOICE_HALE,
    FT_VOICE_WREN,
    FT_VOICE_YOU,
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
    int16_t orbs;    /* paid out, usually 0 */
    bool    follows; /* somebody just started walking with you */
    bool    ended;   /* the quest is now done */
    bool    leads;   /* somebody just set off to show you the way */
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
#define FT_BARK_COUNT ((uint8_t)FT_BARK_WREN_CHATTER + FT_BARK_WREN_CHATTER_N)

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
