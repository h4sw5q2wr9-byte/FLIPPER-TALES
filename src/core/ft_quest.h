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
    uint8_t     goal_room; /* reaching this flips ACTIVE to READY */
    bool        no_battle; /* a fight fails it */
    int16_t     reward_orbs;
} FtQuestDef;

const FtQuestDef* ft_quest_def(FtQuestId id);

void         ft_quests_init(FtQuests* q);
FtQuestState ft_quest_state(const FtQuests* q, FtQuestId id);

/* Walking into a room. Advances anything waiting on this room. */
void ft_quest_enter_room(FtQuests* q, uint8_t room);

/* A fight started. Fails every live quest that asked you not to. */
void ft_quest_battle(FtQuests* q);

/* Talking to a giver. The state moves on, and what was said comes back as up
 * to three lines of at most 20 characters each. */
#define FT_QUEST_LINES 3

typedef struct {
    const char* line[FT_QUEST_LINES];
    uint8_t     lines;
    int16_t     orbs; /* paid out by this conversation, usually 0 */
} FtQuestTalk;

FtQuestTalk ft_quest_talk(FtQuests* q, FtQuestId id);

/* The quest this room's giver carries, or -1 when nobody here has one. */
int ft_quest_for_room(uint8_t room);

/* One short line of status for the pause menu, never NULL. */
const char* ft_quest_status_line(const FtQuests* q, FtQuestId id);

#endif /* FT_QUEST_H */
