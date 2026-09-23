/* Flipper Tales — application entry point.
 *
 * This layer is deliberately thin. It owns the GUI, the input queue and the
 * clock; all game logic lives in src/core, which has no Flipper dependency
 * and is unit-tested on a host machine (see test/). */

#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <string.h>

#include "../core/ft_encounter.h"
#include "../core/ft_world.h"
#include "ft_overworld.h"
#include "../core/ft_practice.h"
#include "ft_render.h"
#include "ft_sound.h"
#include "ft_storage.h"

#define FT_TAG        "FlipperTales"
#define FT_TICK_MS    10   /* ~100 Hz: the capture window is only 50 ms, so the
                            * press must be timestamped finer than that */
#define FT_FRAME_MS   33   /* ~30 fps. The logic still ticks at FT_TICK_MS;
                            * only drawing is capped. Redrawing a sprite-heavy
                            * scene at 100 Hz saturates the GUI thread. */
#define FT_QUEUE_SIZE 16

/* Damage a foe takes for being hit in the overworld before the fight. */
#define FT_FIRST_STRIKE_DAMAGE 3

/* How long a one-line overworld message stays up. */
#define FT_TOAST_MS 1400

typedef enum {
    FtEventInput = 0,
    FtEventTick
} FtEventType;

typedef struct {
    FtEventType type;
    InputEvent  input;
} FtEvent;

typedef enum {
    FT_MODE_OVERWORLD = 0,
    FT_MODE_TITLE,    /* the start screen: where the app opens */
    FT_MODE_SETTINGS, /* sound, voices, tips — from the start screen or pause */
    FT_MODE_INTRO,    /* the opening: Hush on a terminal, before you wake */
    FT_MODE_BATTLE,
    FT_MODE_PAUSE,
    FT_MODE_PRACTICE, /* the arena's setup screen */
    FT_MODE_ORBS,     /* placing orbs — from a level-up, or from the menu */
    FT_MODE_QUESTS,   /* what has been asked of you */
    FT_MODE_POCKETS,  /* what you are carrying */
    FT_MODE_TALK,     /* somebody saying something */
    FT_MODE_CONFIRM,  /* the gate in front of erasing a run */
    FT_MODE_DEBUG,    /* the testing tools, kept out of the player's way */
    FT_MODE_GUIDE,    /* the field guide's index */
    FT_MODE_GUIDE_ENTRY
} FtMode;

/* What the wipe is hiding. */
typedef enum {
    FT_PEND_NONE = 0,
    FT_PEND_BEGIN,
    FT_PEND_END,
    FT_PEND_PRACTICE_FIGHT, /* setup screen -> arena match */
    FT_PEND_PRACTICE_SETUP  /* arena match -> setup screen */
} FtPend;

typedef struct {
    FuriMessageQueue* queue;
    FuriMutex*        mutex;
    ViewPort*         view_port;
    Gui*              gui;

    FtMode      mode;
    FtWorld     world;
    FtEncounter encounter;

    /* The practice arena. A match here touches nothing in the world: the
     * point is to try things, not to gain or lose anything. */
    FtPractice practice;
    bool       in_practice;

    /* One voice, ticked from the game's own update so a fanfare never holds
     * up a frame. */
    FtSound sound;

    /* Which stat row the orb screen is on, and how it was opened: a level-up
     * hands you straight back to the world, the pause menu back to the menu. */
    uint8_t orb_item;
    bool    orbs_from_pause;

    /* Did the fight that just ended pay out a level? Only then is the orb
     * screen pushed at the player unasked. */
    bool    levelled;

    /* Which quest row is highlighted, and the conversation in progress. */
    uint8_t quest_item;
    uint8_t pocket_item;

    FtTalk    talk;
    uint8_t   talk_beat;
    bool      talk_choosing;
    bool      talk_yes;
    FtQuestId talk_quest;
    bool      talk_is_wren;
    bool      talk_is_hale;

    /* Wren naming you, and which name she is on. See ft_start_naming. */
    bool      talk_is_naming;
    uint8_t   naming_try;

    /* The conversation's lines with your name written into them. The talk
     * points here rather than at the lines in the core, which are shared. */
    FtBeat    talk_lines[FT_TALK_MAX_BEATS];
    char      talk_text[FT_TALK_MAX_BEATS][2][24];

    /* The typewriter: characters of this beat shown so far, and time banked
     * toward the next one. Signed, because a full stop banks a pause. */
    uint16_t talk_shown;
    int32_t  talk_type_ms;

    /* Whoever you are talking to stands here, for framing the camera. */
    int32_t talk_fx, talk_fy;

    /* How many times you have heard from each person since what they have
     * to say last changed, so the second visit is not the first scene read
     * back to you. Keyed on the again==0 conversation: when that changes,
     * the story moved, and the count starts over. Not saved; a reload is a
     * fair time for somebody to repeat themselves. */
    uint8_t       talk_slot;
    const FtBeat* talk_first[FT_VOICE_COUNT];
    uint8_t       talk_again[FT_VOICE_COUNT];

    /* The New game confirmation. Defaults to No. */
    bool confirm_yes;

    /* The debug menu, and the room its Travel row is pointing at. */
    uint8_t debug_item;
    uint8_t travel_room;

    /* Which field-guide entry is highlighted, and which one is open. */
    uint8_t guide_item;

    /* What the battle was doing last frame, so a cue fires on the change
     * rather than every tick. */
    FtPhase sound_phase;
    bool    sound_struck;

    /* Which entity started the current battle, so it can be removed on a win. */
    int  battle_entity;
    bool battle_first_strike;

    /* The scene wipe. Starting or ending a fight does not swap the screen on
     * the spot: the iris closes, the swap happens behind it, and it opens on
     * the new scene. Input and the world are both frozen while it runs, so
     * nothing can happen behind the black. */
    uint32_t wipe_ms;
    bool     wipe_active;
    bool     wipe_swapped;
    FtPend   pend;
    int      pend_entity;
    bool     pend_first_strike;
    bool     pend_ambush;
    bool     pend_won;

    /* D-pad is level-triggered: the queue gives presses and releases, and the
     * walk needs to know what is held right now. */
    uint8_t held;

    const char* toast;
    uint32_t    toast_ms;

    bool    show_help;
    uint8_t help_page;
    bool    coach;

    FtMode  paused_from;

    /* Chapters finished, which is what caps the level. The prologue is
     * chapter zero, so this stays at 0 until Chapter 1 exists. */
    int16_t chapters_done;
    uint8_t pause_item;

    /* The start screen and Settings, and where each was opened from so Back
     * goes back there. */
    uint8_t title_item;
    uint8_t settings_item;
    FtMode  settings_from;
    FtMode  confirm_from;
    bool    has_save;

    /* Voices on or off, separately from sound: somebody may want the fight
     * to be loud and the talking to be quiet. */
    bool voices;

    /* How far into the Debug combination (Up, Up, Down, Down on the pause
     * menu) the last few presses have got. The testing tools are behind it
     * so the pause menu is only the things you use while playing. */
    uint8_t combo;

    /* The opening: which card of Hush's announcement is up, for how long,
     * and how many of its letters have had a blip. */
    uint8_t  intro_line;
    uint32_t intro_ms;
    uint16_t intro_voiced;

    bool running;
} FlipperTales;

/* Declared ahead: the start screen, the opening and the pause menu sit above
 * the talking and saving code in this file, and call into both. */
static uint8_t ft_talk_again(FlipperTales* app, const FtTalk* first);
static void    ft_start_talk(FlipperTales* app, int32_t tx, int32_t ty);
static void    ft_save_here(FlipperTales* app);
static bool    ft_save_now(FlipperTales* app);

#define HELD_UP    (1u << 0)
#define HELD_DOWN  (1u << 1)
#define HELD_LEFT  (1u << 2)
#define HELD_RIGHT (1u << 3)

/* ---- GUI callbacks --------------------------------------------------- */

static void ft_draw_callback(Canvas* canvas, void* ctx) {
    FlipperTales* app = ctx;

    /* Never wait on the GUI thread. A skipped frame is invisible at 30 fps;
     * a stalled compositor also stalls input dispatch. */
    if(furi_mutex_acquire(app->mutex, 0) != FuriStatusOk) return;

    if(app->show_help) {
        ft_render_help(canvas, app->help_page);
    } else if(app->mode == FT_MODE_TITLE) {
        ft_render_title(canvas, app->title_item, app->has_save);
    } else if(app->mode == FT_MODE_SETTINGS) {
        ft_render_settings(canvas, app->settings_item, app->sound.on, app->voices, app->coach);
    } else if(app->mode == FT_MODE_INTRO) {
        ft_render_intro(canvas, app->intro_line, app->intro_ms);
    } else if(app->mode == FT_MODE_PAUSE) {
        ft_render_pause(canvas, app->pause_item, app->world.stats.orbs,
                        app->paused_from == FT_MODE_BATTLE);
    } else if(app->mode == FT_MODE_PRACTICE) {
        ft_render_practice(canvas, &app->practice);
    } else if(app->mode == FT_MODE_GUIDE) {
        ft_render_guide_list(canvas, &app->world.guide, app->guide_item);
    } else if(app->mode == FT_MODE_GUIDE_ENTRY) {
        ft_render_guide_entry(
            canvas, ft_guide_nth(&app->world.guide, app->guide_item));
    } else if(app->mode == FT_MODE_DEBUG) {
        ft_render_debug(canvas, app->debug_item,
                        ft_room(app->travel_room)->map->name);
    } else if(app->mode == FT_MODE_CONFIRM) {
        ft_render_confirm(canvas, "Erase your save?", app->confirm_yes);
    } else if(app->mode == FT_MODE_ORBS) {
        ft_render_orbs(canvas, &app->world.stats, app->orb_item);
    } else if(app->mode == FT_MODE_QUESTS) {
        ft_render_quests(canvas, &app->world.quests, app->quest_item);
    } else if(app->mode == FT_MODE_POCKETS) {
        ft_render_pockets(canvas, &app->world.pockets, app->pocket_item,
                          &app->world.stats);
    } else if(app->mode == FT_MODE_TALK) {
        ft_overworld_render_talk(canvas, &app->world, app->talk_fx, app->talk_fy);
        ft_render_talk(canvas, &app->talk, app->talk_beat, app->talk_shown,
                       app->talk_choosing, app->talk_yes);
    } else if(app->mode == FT_MODE_BATTLE) {
        ft_render_battle(canvas, &app->encounter);
    } else {
        ft_overworld_render(canvas, &app->world);

        /* A toast under the wipe is a toast nobody reads. */
        if(app->toast_ms > 0 && !app->wipe_active) {
            ft_overworld_toast(canvas, app->toast);
        }
    }

    /* Over everything, including the pause menu, so nothing outruns it. */
    if(app->wipe_active) {
        const FtWipe wipe = ft_wipe_at(app->wipe_ms);
        ft_render_iris(canvas, wipe.amount);
    }

    furi_mutex_release(app->mutex);
}

static void ft_input_callback(InputEvent* event, void* ctx) {
    FlipperTales* app = ctx;
    const FtEvent msg = {.type = FtEventInput, .input = *event};

    /* Timeout 0, never FuriWaitForever. This runs on the GUI service thread:
     * blocking here when the queue is full wedges that thread, which then
     * stops dispatching input and stops running the draw callback, and nothing
     * can recover it. Dropping an event is survivable — the player presses
     * again — so the queue must never be allowed to block the caller. */
    furi_message_queue_put(app->queue, &msg, 0);
}

/* ---- Mode changes ---------------------------------------------------- */

static void ft_toast(FlipperTales* app, const char* text) {
    app->toast = text;
    app->toast_ms = FT_TOAST_MS;
}

static void ft_enter_battle_now(FlipperTales* app, int entity, bool first_strike,
                                bool ambush) {
    const FtRoom* room = ft_room(app->world.room);
    const FtRoster* roster = ft_roster(room->ents[entity].roster);

    ft_encounter_init(&app->encounter, roster->foes, roster->count, furi_get_tick());

    /* Carry the player across: an encounter builds a level-one character on
     * its own, which is right for a standalone fight and wrong here. */
    app->encounter.stats = app->world.stats;
    app->encounter.pockets = app->world.pockets;
    ft_roll_init(&app->encounter.roll, app->world.stats.charge);
    app->encounter.coach = app->coach;

    if(first_strike) {
        /* Hitting it out here means it enters already hurt. */
        app->encounter.foes[0].charge =
            (int16_t)(app->encounter.foes[0].charge - FT_FIRST_STRIKE_DAMAGE);
        if(app->encounter.foes[0].charge < 1) app->encounter.foes[0].charge = 1;
    }

    /* Met, therefore known. Recorded on the way in rather than on a win:
     * the thing that beat you is exactly the one you want to look up. */
    ft_guide_note_encounter(&app->world.guide, &app->encounter);

    /* Reached you rather than the other way round: they open. */
    if(ambush) ft_encounter_enemy_opens(&app->encounter);

    /* Some things you are asked to do are asked of you not fighting. */
    ft_quest_battle(&app->world.quests);

    app->battle_entity = entity;
    app->battle_first_strike = first_strike;
    app->mode = FT_MODE_BATTLE;
}

static void ft_leave_battle_now(FlipperTales* app, bool won) {
    /* Carry the player back out, including whatever they ate. */
    app->world.stats = app->encounter.stats;
    app->world.stats.charge = app->encounter.roll.current;
    app->world.pockets = app->encounter.pockets;
    app->coach = false;

    if(won) {
        if(app->battle_entity >= 0) {
            ft_world_clear_entity(&app->world, (uint8_t)app->battle_entity);
        }
        if(app->world.stats.charge < 1) app->world.stats.charge = 1;

        /* XP is banked on the way out. Each level it bought is taken here and
         * pays out an orb; what the orbs become is the player's business, and
         * stays the player's business — they can be moved again later. */
        const int16_t xp = ft_encounter_xp(&app->encounter);
        const int16_t levels =
            ft_xp_gain(&app->world.stats, xp, ft_level_cap(app->chapters_done));
        for(int16_t i = 0; i < levels; i++) ft_level_take(&app->world.stats);
        app->levelled = (levels > 0);
        if(app->levelled) ft_sound_play(&app->sound, FT_SFX_LEVEL);

        /* Echo does not go down the first time. It goes. */
        ft_toast(app, app->encounter.retreated ? "Echo got away!" : "Cleared.");
    } else {
        /* Downed: the run goes back to the last save, in full.
         *
         * It used to heal you to the brim, move you to the save point and
         * leave everything else exactly as it was — so losing cost nothing
         * and was strictly better than walking away hurt. You kept the
         * every foe you had beaten since saving, and got a free top-up for
         * the trouble.
         *
         * Reloading is what a checkpoint means. Everything since it is gone:
         * the stats, the captures, the cleared encounters. That is the cost,
         * and it is also what makes a terminal worth walking to. */
        FtSaveData saved;

        if(ft_storage_load(&saved)) {
            ft_save_to_world(&saved, &app->world, &app->coach, NULL);
            app->voices = saved.voices;
            ft_toast(app, "Back to your save.");
        } else {
            /* Never saved: there is no checkpoint to go back to, so the run
             * starts over. The first room has a terminal and no foe, which
             * is exactly so this cannot happen by surprise. */
            ft_world_init(&app->world);
            ft_toast(app, "No save. Restarted.");
        }

        app->world.stats.charge = app->world.stats.charge_max;
        app->world.stats.ram = app->world.stats.ram_max;
    }

    app->battle_entity = -1;

    /* A level just earned takes the screen before the world comes back. Orbs
     * banked on purpose do not: a player who is saving them should not have
     * the screen pushed at them after every fight. The pause menu's Orbs row
     * shows the count, which is where a reminder belongs. */
    if(won && app->levelled && app->world.stats.orbs > 0) {
        app->orb_item = 0;
        app->orbs_from_pause = false;
        app->mode = FT_MODE_ORBS;
        return;
    }

    app->mode = FT_MODE_OVERWORLD;
}

/* ---- The scene wipe --------------------------------------------------- */

static bool ft_wiping(const FlipperTales* app) {
    return app->wipe_active;
}

static void ft_start_wipe(FlipperTales* app, FtPend pend) {
    app->wipe_active = true;
    app->wipe_swapped = false;
    app->wipe_ms = 0;
    app->pend = pend;
}

static void ft_begin_battle(FlipperTales* app, int entity, bool first_strike,
                            bool ambush) {
    if(ft_wiping(app)) return;

    app->pend_entity = entity;
    app->pend_first_strike = first_strike;
    app->pend_ambush = ambush;

    /* Under the closing iris, so the fight is heard coming. */
    ft_sound_play(&app->sound, FT_SFX_ENCOUNTER);
    ft_start_wipe(app, FT_PEND_BEGIN);
}

static void ft_end_battle(FlipperTales* app, bool won) {
    if(ft_wiping(app)) return;

    /* An arena match is not part of the run: it cannot clear an entity, cost
     * you Charge or send you back to a terminal. It just ends. */
    if(app->in_practice) {
        ft_start_wipe(app, FT_PEND_PRACTICE_SETUP);
        return;
    }

    app->pend_won = won;
    ft_start_wipe(app, FT_PEND_END);
}

static void ft_wipe_update(FlipperTales* app, uint32_t dt_ms) {
    app->wipe_ms += dt_ms;

    /* The swap happens at full black, once. */
    if(!app->wipe_swapped && app->wipe_ms >= FT_WIPE_SWAP) {
        app->wipe_swapped = true;

        switch(app->pend) {
        case FT_PEND_BEGIN:
            ft_enter_battle_now(
                app, app->pend_entity, app->pend_first_strike, app->pend_ambush);
            break;
        case FT_PEND_END:
            ft_leave_battle_now(app, app->pend_won);
            break;
        case FT_PEND_PRACTICE_FIGHT:
            ft_practice_start(&app->practice, &app->encounter);
            app->battle_entity = -1;
            app->in_practice = true;
            app->mode = FT_MODE_BATTLE;
            break;
        case FT_PEND_PRACTICE_SETUP:
            app->in_practice = false;
            app->mode = FT_MODE_PRACTICE;
            break;
        case FT_PEND_NONE:
        default:
            break;
        }
        app->pend = FT_PEND_NONE;
    }

    if(app->wipe_ms >= FT_WIPE_MS) app->wipe_active = false;
}

/* ---- Debug ------------------------------------------------------------ */

/* The testing tools. Everything here changes the run, so it all lives behind
 * one door rather than being sprinkled through the player's menus. */
static void ft_debug_pick(FlipperTales* app) {
    switch((FtDebugItem)app->debug_item) {
    case FT_DEBUG_TRAVEL: {
        /* Land on the room's first exit, which is guaranteed to be a door
         * the player can stand in — the tests check that for every room. */
        const FtRoom* dest = ft_room(app->travel_room);

        ft_world_enter(&app->world, app->travel_room, dest->exits[0].tx,
                       dest->exits[0].ty);
        app->mode = FT_MODE_OVERWORLD;
        ft_toast(app, dest->map->name);
        break;
    }

    case FT_DEBUG_PRACTICE:
        /* The arena replaces whatever is on screen, so it never resumes into
         * a half-finished fight. */
        app->paused_from = FT_MODE_PRACTICE;
        app->in_practice = false;
        app->mode = FT_MODE_PRACTICE;
        break;

    case FT_DEBUG_HEAL:
        app->world.stats.charge = app->world.stats.charge_max;
        app->world.stats.ram = app->world.stats.ram_max;
        ft_toast(app, "Topped up.");
        break;

    case FT_DEBUG_XP: {
        const int16_t owed =
            ft_xp_gain(&app->world.stats, 100, ft_level_cap(app->chapters_done));

        for(int16_t i = 0; i < owed; i++) ft_level_take(&app->world.stats);

        if(app->world.stats.orbs > 0) {
            app->orb_item = 0;
            app->orbs_from_pause = false;
            app->mode = FT_MODE_ORBS;
        } else {
            ft_toast(app, "XP banked.");
        }
        break;
    }

    case FT_DEBUG_CLEAR: {
        const FtRoom* room = ft_room(app->world.room);
        for(uint8_t i = 0; i < room->ent_count; i++) {
            ft_world_clear_entity(&app->world, i);
        }
        app->mode = FT_MODE_OVERWORLD;
        ft_toast(app, "Room cleared.");
        break;
    }

    case FT_DEBUG_BACK:
    default:
        app->mode = FT_MODE_PAUSE;
        break;
    }
}

/* ---- The opening ------------------------------------------------------- */

static uint16_t ft_intro_card_len(uint8_t card) {
    uint16_t n = 0;
    for(uint8_t i = 0; i < FT_INTRO_CARD_LINES; i++) {
        const char* line = ft_quest_intro_line(card, i);
        if(line) n = (uint16_t)(n + strlen(line));
    }
    return n;
}

static void ft_start_intro(FlipperTales* app) {
    app->intro_line = 0;
    app->intro_ms = 0;
    app->intro_voiced = 0;
    app->mode = FT_MODE_INTRO;
}

/* The opening is over: you are standing in Cold Boot, and the Keeper is
 * already talking to you. The first conversation in the game is not one you
 * have to walk up to. */
static void ft_intro_done(FlipperTales* app) {
    app->paused_from = FT_MODE_OVERWORLD;
    app->mode = FT_MODE_OVERWORLD;

    const FtRoom* room = ft_room(app->world.room);
    for(uint8_t i = 0; i < room->ent_count; i++) {
        const FtEntity* e = &room->ents[i];
        if(e->kind != FT_ENT_NPC) continue;

        app->talk_quest = (FtQuestId)e->roster;
        app->talk_is_wren = false;
        app->talk_is_hale = false;
        app->talk_is_naming = false;

        const FtTalk first = ft_quest_talk(&app->world.quests, app->talk_quest, 0);
        (void)ft_talk_again(app, &first);
        app->talk = first;
        ft_start_talk(app, e->tx, e->ty);
        return;
    }
}

static void ft_intro_next(FlipperTales* app) {
    app->intro_line++;
    app->intro_ms = 0;
    app->intro_voiced = 0;

    /* The last card is cut off; the terminal dies with a noise. */
    if(app->intro_line == FT_INTRO_STATIC) ft_sound_play(&app->sound, FT_SFX_HURT);
    if(app->intro_line > FT_INTRO_DARK) ft_intro_done(app);
}

static void ft_intro_tick(FlipperTales* app, uint32_t dt_ms) {
    app->intro_ms += dt_ms;

    if(app->intro_line < FT_INTRO_CARDS) {
        const uint16_t len = ft_intro_card_len(app->intro_line);
        const uint32_t typed = app->intro_ms / FT_INTRO_CHAR_MS;

        /* Hush's voice under the words, every other letter. */
        while(app->intro_voiced < len && app->intro_voiced < typed) {
            app->intro_voiced++;
            if(app->voices && (app->intro_voiced % 2u) == 1u) {
                ft_sound_play(&app->sound, FT_SFX_VOICE_HUSH);
            }
        }

        /* The last card does not get to finish. */
        const uint32_t hold = (app->intro_line == FT_INTRO_CARDS - 1u) ? 150u : FT_INTRO_HOLD_MS;
        if(typed >= len && app->intro_ms >= (uint32_t)len * FT_INTRO_CHAR_MS + hold) {
            ft_intro_next(app);
        }
    } else if(app->intro_line == FT_INTRO_STATIC) {
        if(app->intro_ms >= FT_INTRO_STATIC_MS) ft_intro_next(app);
    } else if(app->intro_ms >= FT_INTRO_DARK_MS) {
        ft_intro_next(app);
    }
}

static void ft_intro_input(FlipperTales* app, InputKey key) {
    if(key == InputKeyBack) {
        /* Skip the lot. */
        ft_intro_done(app);
        return;
    }
    if(key != InputKeyOk) return;

    /* OK finishes the line, and a second OK moves on — like talking. */
    if(app->intro_line < FT_INTRO_CARDS) {
        const uint16_t len = ft_intro_card_len(app->intro_line);
        if(app->intro_ms < (uint32_t)len * FT_INTRO_CHAR_MS) {
            app->intro_ms = (uint32_t)len * FT_INTRO_CHAR_MS;
            app->intro_voiced = len;
        } else {
            ft_intro_next(app);
        }
    }
}

/* ---- Start screen, pause, settings ------------------------------------ */

/* A run from nothing: the save goes, the world starts again, and the opening
 * plays. */
static void ft_new_run(FlipperTales* app) {
    ft_storage_erase();
    ft_world_init(&app->world);
    app->battle_entity = -1;
    app->has_save = false;
    for(uint8_t i = 0; i < FT_VOICE_COUNT; i++) {
        app->talk_first[i] = NULL;
        app->talk_again[i] = 0;
    }
    ft_start_intro(app);
}

/* Back to where the save left off. Read fresh from the card every time, so
 * Quit then Continue is the save, not whatever was in memory. */
static void ft_continue(FlipperTales* app) {
    FtSaveData saved;
    if(ft_storage_load(&saved)) {
        bool sound_on = app->sound.on;
        ft_save_to_world(&saved, &app->world, &app->coach, &sound_on);
        ft_sound_set(&app->sound, sound_on);
        app->voices = saved.voices;
    }
    app->paused_from = FT_MODE_OVERWORLD;
    app->mode = FT_MODE_OVERWORLD;
}

static void ft_open_settings(FlipperTales* app, FtMode from) {
    app->settings_item = 0;
    app->settings_from = from;
    app->mode = FT_MODE_SETTINGS;
}

static void ft_to_title(FlipperTales* app) {
    FtSaveData probe;
    app->has_save = ft_storage_load(&probe);
    app->title_item = app->has_save ? FT_TITLE_CONTINUE : FT_TITLE_NEW;
    app->mode = FT_MODE_TITLE;
}

static void ft_title_input(FlipperTales* app, InputKey key) {
    const uint8_t first = app->has_save ? FT_TITLE_CONTINUE : FT_TITLE_NEW;

    switch(key) {
    case InputKeyUp:
        app->title_item = (app->title_item > first) ? (uint8_t)(app->title_item - 1u) :
                                                      (uint8_t)(FT_TITLE_COUNT - 1u);
        ft_sound_play(&app->sound, FT_SFX_MOVE);
        break;
    case InputKeyDown:
        app->title_item = (app->title_item + 1u < FT_TITLE_COUNT) ?
                              (uint8_t)(app->title_item + 1u) :
                              first;
        ft_sound_play(&app->sound, FT_SFX_MOVE);
        break;
    case InputKeyOk:
        switch(app->title_item) {
        case FT_TITLE_CONTINUE:
            ft_continue(app);
            break;
        case FT_TITLE_NEW:
            /* A save is somebody's run. Starting over asks first. */
            if(app->has_save) {
                app->confirm_yes = false;
                app->confirm_from = FT_MODE_TITLE;
                app->mode = FT_MODE_CONFIRM;
            } else {
                ft_new_run(app);
            }
            break;
        case FT_TITLE_SETTINGS:
        default:
            ft_open_settings(app, FT_MODE_TITLE);
            break;
        }
        break;
    case InputKeyBack:
        app->running = false;
        break;
    default:
        break;
    }
}

static void ft_settings_input(FlipperTales* app, InputKey key) {
    switch(key) {
    case InputKeyUp:
        app->settings_item = (uint8_t)((app->settings_item + FT_SET_COUNT - 1u) % FT_SET_COUNT);
        return;
    case InputKeyDown:
        app->settings_item = (uint8_t)((app->settings_item + 1u) % FT_SET_COUNT);
        return;
    case InputKeyBack:
        app->mode = app->settings_from;
        return;
    case InputKeyOk:
    case InputKeyLeft:
    case InputKeyRight:
        break;
    default:
        return;
    }

    /* Left and right flip a switch as well as OK does; only OK opens things. */
    const bool ok = (key == InputKeyOk);
    switch(app->settings_item) {
    case FT_SET_SOUND:
        ft_sound_set(&app->sound, !app->sound.on);
        ft_sound_play(&app->sound, FT_SFX_PICK);
        break;
    case FT_SET_VOICES:
        app->voices = !app->voices;
        if(app->voices) ft_sound_play(&app->sound, FT_SFX_VOICE_WREN);
        break;
    case FT_SET_TIPS:
        app->coach = !app->coach;
        app->encounter.coach = app->coach;
        break;
    case FT_SET_NEWGAME:
        if(!ok) break;
        app->confirm_yes = false;
        app->confirm_from = FT_MODE_SETTINGS;
        app->mode = FT_MODE_CONFIRM;
        break;
    case FT_SET_BACK:
    default:
        if(ok) app->mode = app->settings_from;
        break;
    }
}

static void ft_pause_input(FlipperTales* app, InputKey key) {
    /* Up, Up, Down, Down opens the testing tools. The moves still move the
     * cursor — it lands back where it started — so the combination is
     * invisible to anybody not looking for it. */
    static const InputKey COMBO[4] = {InputKeyUp, InputKeyUp, InputKeyDown, InputKeyDown};
    if(key == COMBO[app->combo]) {
        app->combo++;
    } else {
        app->combo = (key == COMBO[0]) ? 1u : 0u;
    }

    switch(key) {
    case InputKeyLeft:
        app->pause_item = (uint8_t)((app->pause_item + FT_PAUSE_COUNT - 1u) % FT_PAUSE_COUNT);
        break;
    case InputKeyRight:
        app->pause_item = (uint8_t)((app->pause_item + 1u) % FT_PAUSE_COUNT);
        break;
    case InputKeyUp:
    case InputKeyDown:
        app->pause_item = (uint8_t)((app->pause_item + FT_PAUSE_COLS) % FT_PAUSE_COUNT);
        break;
    case InputKeyBack:
        app->mode = app->paused_from;
        return;
    case InputKeyOk:
        break;
    default:
        return;
    }

    if(app->combo >= 4u) {
        app->combo = 0;
        app->debug_item = 0;
        app->mode = FT_MODE_DEBUG;
        return;
    }
    if(key != InputKeyOk) return;

    switch(app->pause_item) {
    case FT_PAUSE_POCKETS:
        app->pocket_item = 0;
        app->mode = FT_MODE_POCKETS;
        break;
    case FT_PAUSE_ORBS:
        /* Not mid-fight: moving a point to escape a hit you have already
         * taken is not a build decision. */
        if(app->paused_from == FT_MODE_BATTLE) {
            ft_toast(app, "Not in a fight.");
            app->mode = app->paused_from;
            break;
        }
        app->orb_item = 0;
        app->orbs_from_pause = true;
        app->mode = FT_MODE_ORBS;
        break;
    case FT_PAUSE_QUESTS:
        app->quest_item = 0;
        app->mode = FT_MODE_QUESTS;
        break;
    case FT_PAUSE_GUIDE:
        app->guide_item = 0;
        app->mode = FT_MODE_GUIDE;
        break;
    case FT_PAUSE_SAVE:
        /* Terminals are the save point, so this says where to find one
         * rather than quietly doing nothing. */
        if(app->paused_from != FT_MODE_OVERWORLD) {
            ft_toast(app, "Not in a fight.");
        } else if(!ft_world_terminal_near(&app->world)) {
            ft_toast(app, "Find a terminal.");
        } else {
            ft_save_here(app);
            ft_toast(app, ft_save_now(app) ? "Saved." : "No card.");
        }
        app->mode = app->paused_from;
        break;
    case FT_PAUSE_HELP:
        app->mode = app->paused_from;
        app->show_help = true;
        app->help_page = 0;
        break;
    case FT_PAUSE_SETTINGS:
        ft_open_settings(app, FT_MODE_PAUSE);
        break;
    case FT_PAUSE_QUIT:
    default:
        /* To the start screen, not out of the app. Continue there picks up
         * from the last save, so nothing is lost that was not already. */
        ft_to_title(app);
        break;
    }
}

/* ---- Input ----------------------------------------------------------- */

/* ---- Saving ----------------------------------------------------------- */

/* Mark where the player is standing as the place a reboot returns to. */
static void ft_save_here(FlipperTales* app) {
    app->world.save_room = app->world.room;
    app->world.save_tx = app->world.mv.tx;
    app->world.save_ty = app->world.mv.ty;
}

/* Write the run out. False means no card, a full card, or a failed write —
 * never a reason to stop the game, only a reason to say so. */
static bool ft_save_now(FlipperTales* app) {
    FtSaveData data;
    ft_save_from_world(&app->world, app->coach, app->sound.on, &data);
    data.voices = app->voices;
    return ft_storage_save(&data);
}

/* ---- Talking ----------------------------------------------------------- */

/* The typewriter's pace. A character every 28ms is quick enough that nobody
 * waits for it and slow enough to read as somebody saying it; the pauses
 * after punctuation are what make it read as speech rather than as a
 * printer. OK at any point shows the rest of the line at once. */
#define FT_TALK_CHAR_MS  28
#define FT_TALK_STOP_MS  150 /* after . ! ? */
#define FT_TALK_COMMA_MS 70  /* after , */

static uint16_t ft_beat_len(const FtBeat* b) {
    return (uint16_t)((b->a ? strlen(b->a) : 0u) + (b->b ? strlen(b->b) : 0u));
}

static char ft_beat_char(const FtBeat* b, uint16_t i) {
    const size_t la = b->a ? strlen(b->a) : 0u;
    if(i < la) return b->a[i];
    return b->b ? b->b[i - la] : ' ';
}

/* Pick the again count for the conversation this person would open with. */
static uint8_t ft_talk_again(FlipperTales* app, const FtTalk* first) {
    const uint8_t slot = (first->voice < FT_VOICE_COUNT) ? (uint8_t)first->voice : 0u;

    if(app->talk_first[slot] != first->beats) {
        app->talk_first[slot] = first->beats;
        app->talk_again[slot] = 0;
    }
    app->talk_slot = slot;
    return app->talk_again[slot];
}

static void ft_talk_beat_start(FlipperTales* app) {
    app->talk_shown = 0;
    app->talk_type_ms = 0;
}

/* Open whatever conversation has just been loaded into app->talk, with
 * whoever is saying it standing on (tx, ty). */
static void ft_start_talk(FlipperTales* app, int32_t tx, int32_t ty) {
    /* Your name into every line that says it, and over your own lines. */
    const uint8_t name = app->world.name;
    const uint8_t n = (app->talk.count < FT_TALK_MAX_BEATS) ? app->talk.count :
                                                               (uint8_t)FT_TALK_MAX_BEATS;
    for(uint8_t i = 0; i < n; i++) {
        const FtBeat* from = &app->talk.beats[i];
        FtBeat*       to = &app->talk_lines[i];

        to->who = from->who;
        to->a = ft_quest_expand(from->a, name, app->talk_text[i][0], sizeof(app->talk_text[i][0]));
        to->b = from->b ? ft_quest_expand(from->b, name, app->talk_text[i][1],
                                          sizeof(app->talk_text[i][1])) :
                          NULL;
    }
    app->talk.beats = app->talk_lines;
    app->talk.count = n;
    app->talk.you = (name < FT_NAME_COUNT) ? ft_quest_name(name) : NULL;

    app->talk_beat = 0;
    app->talk_choosing = false;
    app->talk_yes = true;
    app->talk_fx = tx;
    app->talk_fy = ty;
    ft_talk_beat_start(app);

    ft_sound_play(&app->sound, FT_SFX_TALK);
    app->mode = FT_MODE_TALK;
}

/* Type the words out, with a blip of the speaker's voice every other letter. */
static void ft_talk_tick(FlipperTales* app, uint32_t dt_ms) {
    if(app->talk_choosing || app->talk.count == 0u) return;

    const FtBeat*  b = &app->talk.beats[app->talk_beat];
    const uint16_t total = ft_beat_len(b);
    if(app->talk_shown >= total) return;

    const FtVoice voice = (b->who == FT_SAY_YOU) ? FT_VOICE_YOU : app->talk.voice;
    const FtSfxId blip = (FtSfxId)((uint8_t)FT_SFX_VOICE_KEEPER + (uint8_t)voice);

    app->talk_type_ms += (int32_t)dt_ms;
    while(app->talk_type_ms >= FT_TALK_CHAR_MS && app->talk_shown < total) {
        app->talk_type_ms -= FT_TALK_CHAR_MS;

        const char ch = ft_beat_char(b, app->talk_shown);
        app->talk_shown++;

        if(app->voices && ch != ' ' && (app->talk_shown % 2u) == 1u) {
            ft_sound_play(&app->sound, blip);
        }

        /* A breath after the end of a sentence, a shorter one after a comma
         * — unless it is the last thing on the line, where there is nothing
         * to wait for. */
        if(app->talk_shown < total) {
            if(ch == '.' || ch == '!' || ch == '?') app->talk_type_ms -= FT_TALK_STOP_MS;
            else if(ch == ',') app->talk_type_ms -= FT_TALK_COMMA_MS;
        }
    }
}

/* Wren stops you on the walk home and gives you a name (STORY.md §5). Each
 * no is the next name; the fifth is not a question. */
static void ft_start_naming(FlipperTales* app, uint8_t tries, int32_t tx, int32_t ty) {
    app->naming_try = tries;
    app->talk = ft_quest_naming_talk(tries);
    app->talk_is_wren = false;
    app->talk_is_hale = false;
    app->talk_is_naming = true;
    ft_start_talk(app, tx, ty);
}

static void ft_finish_naming(FlipperTales* app, bool yes) {
    FtWorld* w = &app->world;

    /* Her saying it back to you. Nothing left to decide. */
    if(app->naming_try >= FT_NAME_COUNT) {
        app->talk_is_naming = false;
        app->mode = FT_MODE_OVERWORLD;
        return;
    }

    const bool last = app->naming_try + 1u >= FT_NAME_COUNT;
    if(yes || last) {
        w->name = app->naming_try;
        ft_sound_play(&app->sound, FT_SFX_LEVEL);
        ft_save_now(app);

        /* Lunchbox already ends on her deciding; the others get a hello. */
        if(last) {
            app->talk_is_naming = false;
            app->mode = FT_MODE_OVERWORLD;
            return;
        }
        ft_start_naming(app, FT_NAME_COUNT, app->talk_fx, app->talk_fy);
        return;
    }

    ft_start_naming(app, (uint8_t)(app->naming_try + 1u), app->talk_fx, app->talk_fy);
}

/* And apply what it did, once it is over. Nothing changes until here, so a
 * question opened by accident can be walked away from. */
static void ft_finish_talk(FlipperTales* app, bool yes) {
    FtWorld* w = &app->world;

    if(app->talk_is_naming) {
        ft_finish_naming(app, yes);
        return;
    }
    const FtQuestOutcome out =
        app->talk_is_wren ? ft_quest_wren_answer(&w->quests) :
        app->talk_is_hale ?
                            ft_quest_hale_answer(&w->quests, (w->revealed & FT_REVEAL_PIT) != 0u,
                                                 w->hale == (uint8_t)FT_HALE_WAIT) :
                            ft_quest_answer(&w->quests, app->talk_quest, yes);

    if(out.follows) ft_world_escort_start(&app->world);

    /* Ma Rivet's clicker. */
    if(out.infrared) {
        ft_toast(app, "Got Infrared!");
        ft_sound_play(&app->sound, FT_SFX_LEVEL);
    }

    /* Somebody setting off to show you the way. It is Hale either way: Coll
     * says "go with him", and he goes. */
    if(out.leads) ft_world_hale_lead(&app->world);

    /* Handing her back is what ends the escort. */
    if(out.ended) ft_world_escort_stop(&app->world);

    if(out.orbs > 0) {
        app->world.stats.orbs = (int16_t)(app->world.stats.orbs + out.orbs);
        ft_sound_play(&app->sound, FT_SFX_LEVEL);
    }

    /* Anything a conversation changed is progress worth keeping even if the
     * walk home goes badly. */
    if(out.orbs > 0 || out.follows || out.ended || out.leads || out.infrared) ft_save_now(app);

    /* Heard it. Next time they say something shorter, and different. */
    if(app->talk_again[app->talk_slot] < 250u) app->talk_again[app->talk_slot]++;

    app->mode = FT_MODE_OVERWORLD;
}

static void ft_overworld_ok(FlipperTales* app) {
    /* A foe you are facing is struck before it can react. */
    const int ahead = ft_world_foe_ahead(&app->world);
    if(ahead >= 0) {
        ft_begin_battle(app, ahead, true, false);
        return;
    }

    /* Something somebody left. */
    const int pick = ft_world_pick_ahead(&app->world);
    if(pick >= 0) {
        if(ft_pockets_full(&app->world.pockets)) {
            ft_toast(app, "Pockets are full.");
            ft_sound_play(&app->sound, FT_SFX_DENY);
        } else {
            const FtItemId got = ft_world_pick(&app->world, (uint8_t)pick);

            if(got < FT_ITEM_COUNT) {
                ft_sound_play(&app->sound, FT_SFX_PICK);

                char line[24];
                snprintf(line, sizeof(line), "Took a %s.", ft_item_def(got)->name);
                ft_toast(app, line);
            }
        }
        return;
    }

    /* The kid at the end of the junction. Talking to her is what frees her,
     * and she walks out with you. */
    const int kid = ft_world_wren_ahead(&app->world);
    if(kid >= 0) {
        const FtQuests* q = &app->world.quests;
        const FtTalk    first = ft_quest_wren_talk(q, 0);
        const uint8_t   again = ft_talk_again(app, &first);
        const FtEntity* e = &ft_room(app->world.room)->ents[kid];

        /* Home, and she never got to name you: a save from before names,
         * or a debug warp. She does it now. */
        if(ft_quest_state(q, FT_QUEST_WREN) == FT_QUEST_DONE &&
           app->world.name == FT_NAME_NONE) {
            ft_start_naming(app, 0, e->tx, e->ty);
            return;
        }

        app->talk = again ? ft_quest_wren_talk(q, again) : first;
        app->talk_is_wren = true;
        app->talk_is_hale = false;
        app->talk_is_naming = false;
        ft_start_talk(app, e->tx, e->ty);
        return;
    }

    /* Hale, while he is standing still: at his post, or beside the pit. */
    if(ft_world_hale_ahead(&app->world)) {
        const FtWorld* w = &app->world;
        const bool     found = (w->revealed & FT_REVEAL_PIT) != 0u;
        const bool     by = w->hale == (uint8_t)FT_HALE_WAIT;
        const FtTalk   first = ft_quest_hale_talk(&w->quests, found, by, 0);
        const uint8_t  again = ft_talk_again(app, &first);

        app->talk = again ? ft_quest_hale_talk(&w->quests, found, by, again) : first;
        app->talk_is_wren = false;
        app->talk_is_hale = true;
        app->talk_is_naming = false;
        ft_start_talk(app, w->hale_mv.tx, w->hale_mv.ty);
        return;
    }

    /* Somebody you are facing is talked to. An NPC is solid, so walking into
     * one and pressing OK is the whole interaction. */
    const int who = ft_world_npc_ahead(&app->world);
    if(who >= 0) {
        const FtRoom* room = ft_room(app->world.room);

        app->talk_quest = (FtQuestId)room->ents[who].roster;
        app->talk_is_wren = false;
        app->talk_is_hale = false;
        app->talk_is_naming = false;

        const FtTalk  first = ft_quest_talk(&app->world.quests, app->talk_quest, 0);
        const uint8_t again = ft_talk_again(app, &first);
        app->talk = again ? ft_quest_talk(&app->world.quests, app->talk_quest, again) : first;
        ft_start_talk(app, room->ents[who].tx, room->ents[who].ty);
        return;
    }

    /* Under a tree, or facing its trunk: shake it and see. Whether anything
     * is up there was rolled when you walked in, so shaking twice is not a
     * second chance — come back later. */
    const int tree = ft_world_tree_near(&app->world);
    if(tree >= 0) {
        if(ft_pockets_full(&app->world.pockets)) {
            (void)ft_world_shake(&app->world, (uint8_t)tree);
            ft_toast(app, "Pockets are full.");
            ft_sound_play(&app->sound, FT_SFX_DENY);
            return;
        }

        const FtItemId got = ft_world_shake(&app->world, (uint8_t)tree);
        if(got < FT_ITEM_COUNT) {
            char line[24];
            snprintf(line, sizeof(line), "%s! Lucky.", ft_item_def(got)->name);
            ft_toast(app, line);
            ft_sound_play(&app->sound, FT_SFX_PICK);
        } else {
            ft_toast(app, "Nothing fell.");
            ft_sound_play(&app->sound, FT_SFX_MOVE);
        }
        return;
    }

    if(ft_world_terminal_near(&app->world)) {
        app->world.stats.charge = app->world.stats.charge_max;
        app->world.stats.ram = app->world.stats.ram_max;

        /* A terminal is the save point, so using one saves. Restoring without
         * saving would mean the thing you walked across the room for did only
         * half of what it is for. */
        ft_save_here(app);

        /* The relay is awake, so this terminal can reach somebody other than
         * Hush — and the first time, somebody is already calling. */
        if(ft_world_call_due(&app->world)) {
            app->world.revealed |= FT_REVEAL_KEEPER_CALL;
            ft_save_now(app);

            app->talk = ft_quest_keeper_call();
            app->talk_quest = FT_QUEST_COUNT; /* nobody's quest: it changes nothing */
            app->talk_is_wren = false;
            app->talk_is_hale = false;
            app->talk_is_naming = false;
            ft_start_talk(app, app->world.mv.tx, app->world.mv.ty);
            return;
        }

        ft_toast(app, ft_save_now(app) ? "Saved. Healed." : "Healed. No card.");

        /* It is Hush's terminal, and it is very polite about it. */
        ft_world_terminal_speaks(&app->world);
        return;
    }

    /* Infrared: a receiver across a gap. Pointing needs the clicker Ma Rivet
     * hands you; without it, the post is just something over there. */
    if(ft_world_ir_target(&app->world)) {
        if(!ft_quest_has_infrared(&app->world.quests)) {
            ft_toast(app, "Out of reach.");
            ft_sound_play(&app->sound, FT_SFX_DENY);
            return;
        }
        (void)ft_world_ir_fire(&app->world);
        ft_toast(app, "Click! Bridge down.");
        ft_sound_play(&app->sound, FT_SFX_REVEAL);
        ft_save_now(app);
        return;
    }

    /* The relay. Waking it is the job; once it is awake it just hums. */
    if(ft_world_relay_ahead(&app->world)) {
        FtQuests* q = &app->world.quests;
        const FtQuestState at = ft_quest_state(q, FT_QUEST_RIVET);

        if(at == FT_QUEST_ACTIVE) {
            ft_quest_advance(q, FT_QUEST_RIVET, FT_QUEST_READY);
            ft_toast(app, "The relay wakes up!");
            ft_sound_play(&app->sound, FT_SFX_LEVEL);
            ft_save_now(app);
        } else if(at == FT_QUEST_UNKNOWN) {
            ft_toast(app, "A dead relay.");
            ft_sound_play(&app->sound, FT_SFX_MOVE);
        } else {
            ft_toast(app, "It hums. Loudly.");
            ft_sound_play(&app->sound, FT_SFX_MOVE);
        }
        return;
    }

    ft_toast(app, "Nothing here.");
}

static void ft_handle_input(FlipperTales* app, const InputEvent* event) {
    const bool pressed = (event->type == InputTypePress);
    const bool released = (event->type == InputTypeRelease);
    const bool repeated = (event->type == InputTypeRepeat);

    /* Track the d-pad as held state so the overworld can walk continuously. */
    if(pressed || released) {
        uint8_t bit = 0;
        switch(event->key) {
        case InputKeyUp:    bit = HELD_UP; break;
        case InputKeyDown:  bit = HELD_DOWN; break;
        case InputKeyLeft:  bit = HELD_LEFT; break;
        case InputKeyRight: bit = HELD_RIGHT; break;
        default: break;
        }
        if(bit) {
            if(pressed) app->held |= bit;
            else app->held &= (uint8_t)~bit;
        }
    }

    if(!pressed && !repeated) return;

    /* Nothing is actionable behind the wipe. Held direction is still tracked
     * above, so walking resumes the instant the screen opens. */
    if(app->wipe_active) return;

    /* OK acts on the physical press only. Holding it emits Repeat events, and
     * accepting those would let a held confirm in a menu fall straight through
     * into an action command and register a press at t=0. */
    if(event->key == InputKeyOk && !pressed) return;

    if(app->show_help) {
        switch(event->key) {
        case InputKeyRight:
            if(app->help_page + 1 < FT_HELP_PAGES) app->help_page++;
            break;
        case InputKeyLeft:
            if(app->help_page > 0) app->help_page--;
            break;
        case InputKeyOk:
        case InputKeyBack:
            app->show_help = false;
            app->help_page = 0;
            break;
        default:
            break;
        }
        return;
    }

    if(app->mode == FT_MODE_TITLE) {
        ft_title_input(app, event->key);
        return;
    }

    if(app->mode == FT_MODE_SETTINGS) {
        ft_settings_input(app, event->key);
        return;
    }

    if(app->mode == FT_MODE_INTRO) {
        ft_intro_input(app, event->key);
        return;
    }

    if(app->mode == FT_MODE_PAUSE) {
        ft_pause_input(app, event->key);
        return;
    }

    if(app->mode == FT_MODE_GUIDE_ENTRY) {
        /* Any key out: the page is a page, not a menu. */
        app->mode = FT_MODE_GUIDE;
        return;
    }

    if(app->mode == FT_MODE_GUIDE) {
        const uint8_t n = ft_guide_count(&app->world.guide);

        switch(event->key) {
        case InputKeyUp:
            if(n > 0u) app->guide_item = (uint8_t)((app->guide_item + n - 1u) % n);
            break;
        case InputKeyDown:
            if(n > 0u) app->guide_item = (uint8_t)((app->guide_item + 1u) % n);
            break;
        case InputKeyOk:
            if(n > 0u) app->mode = FT_MODE_GUIDE_ENTRY;
            break;
        case InputKeyBack:
        default:
            app->mode = FT_MODE_PAUSE;
            break;
        }
        return;
    }

    if(app->mode == FT_MODE_DEBUG) {
        switch(event->key) {
        case InputKeyUp:
            app->debug_item =
                (uint8_t)((app->debug_item + FT_DEBUG_COUNT - 1u) % FT_DEBUG_COUNT);
            break;
        case InputKeyDown:
            app->debug_item = (uint8_t)((app->debug_item + 1u) % FT_DEBUG_COUNT);
            break;
        case InputKeyLeft:
        case InputKeyRight:
            if(app->debug_item == FT_DEBUG_TRAVEL) {
                const uint8_t n = ft_room_count();
                const int16_t d = (event->key == InputKeyLeft) ? -1 : 1;
                app->travel_room = (uint8_t)((app->travel_room + n + d) % n);
            }
            break;
        case InputKeyOk:
            ft_debug_pick(app);
            break;
        case InputKeyBack:
        default:
            app->mode = FT_MODE_PAUSE;
            break;
        }
        return;
    }

    if(app->mode == FT_MODE_CONFIRM) {
        switch(event->key) {
        case InputKeyLeft:
        case InputKeyRight:
            app->confirm_yes = !app->confirm_yes;
            break;
        case InputKeyOk:
            if(app->confirm_yes) {
                ft_new_run(app);
            } else {
                app->mode = app->confirm_from;
            }
            break;
        case InputKeyBack:
        default:
            app->mode = app->confirm_from;
            break;
        }
        return;
    }

    if(app->mode == FT_MODE_ORBS) {
        static const FtLevelChoice CHOICE[FT_UP_COUNT] = {
            FT_UP_CHARGE, FT_UP_RAM, FT_UP_POWER};

        switch(event->key) {
        case InputKeyUp:
            app->orb_item =
                (uint8_t)((app->orb_item + FT_UP_COUNT - 1u) % FT_UP_COUNT);
            break;
        case InputKeyDown:
            app->orb_item = (uint8_t)((app->orb_item + 1u) % FT_UP_COUNT);
            break;

        case InputKeyOk:
        case InputKeyRight:
            /* A capped stat, or an empty hand, simply refuses: there is no
             * way to lose an orb by pressing the wrong row. */
            if(ft_orb_spend(&app->world.stats, CHOICE[app->orb_item])) {
                ft_sound_play(&app->sound, FT_SFX_LEVEL);
                ft_save_now(app);
            }
            break;

        case InputKeyLeft:
            /* And back out again, at any time. This is the whole point: a
             * build you are stuck with is one you had to be told about. */
            if(ft_orb_refund(&app->world.stats, CHOICE[app->orb_item])) {
                ft_sound_play(&app->sound, FT_SFX_DENY);
                ft_save_now(app);
            }
            break;

        case InputKeyBack:
        default:
            /* Leaving with orbs in hand is fine — they keep, and the pause
             * menu says how many. The screen used to refuse to close, which
             * made a level-up a modal interruption. */
            app->mode = app->orbs_from_pause ? FT_MODE_PAUSE : FT_MODE_OVERWORLD;
            break;
        }
        return;
    }

    if(app->mode == FT_MODE_POCKETS) {
        const uint8_t kinds = ft_pockets_kinds(&app->world.pockets);

        switch(event->key) {
        case InputKeyUp:
            if(kinds > 0u) {
                app->pocket_item =
                    (uint8_t)((app->pocket_item + kinds - 1u) % kinds);
            }
            break;
        case InputKeyDown:
            if(kinds > 0u) app->pocket_item = (uint8_t)((app->pocket_item + 1u) % kinds);
            break;
        case InputKeyOk: {
            if(kinds == 0u) break;
            if(app->pocket_item >= kinds) app->pocket_item = (uint8_t)(kinds - 1u);

            const FtItemId id = ft_pockets_nth(&app->world.pockets, app->pocket_item);
            if(id >= FT_ITEM_COUNT) break;

            FtStats* st = &app->world.stats;

            /* Eating at full health throws the thing away, and the pocket is
             * too small for that to be the player's mistake to make. */
            if(!ft_item_useful(id, st->charge, st->charge_max, st->ram, st->ram_max)) {
                break;
            }
            if(!ft_pockets_take(&app->world.pockets, id)) break;

            const FtItemDef* d = ft_item_def(id);

            st->charge = (int16_t)(st->charge + d->heal);
            if(st->charge > st->charge_max) st->charge = st->charge_max;
            st->ram = (int16_t)(st->ram + d->ram);
            if(st->ram > st->ram_max) st->ram = st->ram_max;

            /* Keep the cursor on something that still exists. */
            const uint8_t left = ft_pockets_kinds(&app->world.pockets);
            if(left == 0u) app->pocket_item = 0;
            else if(app->pocket_item >= left) app->pocket_item = (uint8_t)(left - 1u);

            ft_save_now(app);
            break;
        }
        case InputKeyBack:
        default:
            app->mode = FT_MODE_PAUSE;
            break;
        }
        return;
    }

    if(app->mode == FT_MODE_QUESTS) {
        switch(event->key) {
        case InputKeyUp:
            app->quest_item =
                (uint8_t)((app->quest_item + FT_QUEST_COUNT - 1u) % FT_QUEST_COUNT);
            break;
        case InputKeyDown:
            app->quest_item = (uint8_t)((app->quest_item + 1u) % FT_QUEST_COUNT);
            break;
        case InputKeyBack:
        default:
            app->mode = FT_MODE_PAUSE;
            break;
        }
        return;
    }

    if(app->mode == FT_MODE_TALK) {
        if(app->talk_choosing) {
            switch(event->key) {
            case InputKeyUp:
            case InputKeyDown:
            case InputKeyLeft:
            case InputKeyRight:
                app->talk_yes = !app->talk_yes;
                ft_sound_play(&app->sound, FT_SFX_MOVE);
                break;
            case InputKeyOk:
                ft_finish_talk(app, app->talk_yes);
                break;
            case InputKeyBack:
            default:
                /* Backing out of a question is the same as saying no, and
                 * costs nothing: nothing has changed yet. */
                ft_finish_talk(app, false);
                break;
            }
            return;
        }

        switch(event->key) {
        case InputKeyOk:
            /* Still typing: OK shows the rest of the line. A second OK
             * moves on. */
            if(app->talk_shown < ft_beat_len(&app->talk.beats[app->talk_beat])) {
                app->talk_shown = ft_beat_len(&app->talk.beats[app->talk_beat]);
            } else if(app->talk_beat + 1u < app->talk.count) {
                app->talk_beat++;
                ft_talk_beat_start(app);
            } else if(app->talk.ask) {
                app->talk_choosing = true;
            } else {
                ft_finish_talk(app, true);
            }
            break;

        case InputKeyBack:
            /* Leaving early is leaving: a conversation you cannot walk out
             * of is one you resent. Nothing has been applied.
             *
             * Except Wren's naming, which would only stop you again on the
             * next step. Walking off on a name is turning it down. */
            if(app->talk_is_naming) {
                ft_finish_talk(app, false);
                break;
            }
            app->mode = FT_MODE_OVERWORLD;
            break;

        default:
            break;
        }
        return;
    }

    if(app->mode == FT_MODE_PRACTICE) {
        switch(event->key) {
        case InputKeyUp:    ft_practice_move(&app->practice, -1); break;
        case InputKeyDown:  ft_practice_move(&app->practice, 1); break;
        case InputKeyLeft:  ft_practice_adjust(&app->practice, -1); break;
        case InputKeyRight: ft_practice_adjust(&app->practice, 1); break;
        case InputKeyOk:
            if(app->practice.row == FT_PRACTICE_FIGHT) {
                ft_start_wipe(app, FT_PEND_PRACTICE_FIGHT);
            } else {
                /* OK on a setting cycles it, so the arena can be driven with
                 * one thumb without hunting for LEFT and RIGHT. */
                ft_practice_adjust(&app->practice, 1);
            }
            break;
        case InputKeyBack:
        default:
            /* Out of the arena and back to where you were standing. */
            app->paused_from = FT_MODE_OVERWORLD;
            app->mode = FT_MODE_OVERWORLD;
            break;
        }
        return;
    }

    /* Back opens the pause menu. It used to close the attack panel first,
     * back when there was one. */
    if(event->key == InputKeyBack) {
        if(pressed) {
            app->paused_from = app->mode;
            app->pause_item = FT_PAUSE_POCKETS;
            app->combo = 0;
            app->mode = FT_MODE_PAUSE;
        }
        return;
    }

    if(app->mode == FT_MODE_OVERWORLD) {
        if(event->key == InputKeyOk) ft_overworld_ok(app);
        return;
    }

    /* --- battle --- */
    switch(event->key) {
    case InputKeyOk:
        if(ft_encounter_over(&app->encounter)) {
            ft_end_battle(app, app->encounter.phase == FT_PHASE_WIN);
        } else if(app->encounter.phase == FT_PHASE_MENU) {
            /* Attack opens the panel; anything else commits. */
            ft_encounter_menu_confirm(&app->encounter);
        } else {
            ft_encounter_press_ok(&app->encounter);
        }
        break;
    case InputKeyLeft:
        ft_encounter_menu_move(&app->encounter, -1);
        break;
    case InputKeyRight:
        ft_encounter_menu_move(&app->encounter, 1);
        break;
    /* The action row is horizontal, so it is steered horizontally and
     * nothing else. UP and DOWN used to move the cursor as well, which meant
     * a stray thumb changed what you were about to do. */
    case InputKeyUp:
        if(ft_encounter_over(&app->encounter)) {
            app->show_help = true;
        } else if(app->encounter.phase == FT_PHASE_MENU &&
                  app->encounter.menu_index == (uint8_t)FT_ACTION_ITEM) {
            /* UP and DOWN do nothing on the action row by design — a stray
             * thumb must not change what you are about to do. On Use they
             * pick *which* item, which is a different question. */
            ft_encounter_item_move(&app->encounter, -1);
        }
        break;
    case InputKeyDown:
        if(!ft_encounter_over(&app->encounter) &&
           app->encounter.phase == FT_PHASE_MENU &&
           app->encounter.menu_index == (uint8_t)FT_ACTION_ITEM) {
            ft_encounter_item_move(&app->encounter, 1);
        }
        break;
    default:
        break;
    }
}

/* What the fight sounds like.
 *
 * Driven off phase changes and the animation's own strike frame rather than
 * from the resolver, so core stays free of the speaker and a cue lands when
 * the sprite moves rather than when the maths happened. */
static void ft_battle_sound(FlipperTales* app) {
    FtEncounter* e = &app->encounter;

    if(e->phase != app->sound_phase) {
        app->sound_phase = e->phase;
        app->sound_struck = false;

        switch(e->phase) {
        case FT_PHASE_RESULT:
            /* The swing goes out now; what it did lands at the strike frame. */
            ft_sound_play(&app->sound, FT_SFX_SWING);
            break;
        case FT_PHASE_WIN:
            ft_sound_play(&app->sound, FT_SFX_WIN);
            break;
        case FT_PHASE_LOSE:
            ft_sound_play(&app->sound, FT_SFX_LOSE);
            break;
        default:
            break;
        }
        return;
    }

    /* The frame the attack actually connects. */
    if(app->sound_struck) return;
    if(e->phase != FT_PHASE_RESULT && e->phase != FT_PHASE_IMPACT) return;
    if(ft_encounter_anim_progress(e) < FT_ANIM_STRIKE) return;

    app->sound_struck = true;

    if(e->phase == FT_PHASE_RESULT) {
        if(e->last_total_damage > 0) ft_sound_play(&app->sound, FT_SFX_HIT);
        return;
    }

    /* Their turn. Best news first: a bounce outranks a block, and a block
     * outranks taking it. */
    if(e->last_deflect_fired) {
        ft_sound_play(&app->sound, FT_SFX_DEFLECT);
    } else if(e->last_enemy_hit.perfect) {
        ft_sound_play(&app->sound, FT_SFX_PERFECT);
    } else if(e->last_guard == FT_GUARD_JAM) {
        ft_sound_play(&app->sound, FT_SFX_JAM);
    } else if(e->last_enemy_hit.damage > 0) {
        ft_sound_play(&app->sound, FT_SFX_HURT);
    }
}

/* ---- Update ---------------------------------------------------------- */

static void ft_update(FlipperTales* app, uint32_t dt_ms) {
    /* Before the early returns: a cue must keep playing through the pause
     * menu, the wipe and every other screen that stops the world. */
    ft_sound_tick(&app->sound, dt_ms);

    if(app->toast_ms > 0) {
        app->toast_ms = (app->toast_ms > dt_ms) ? app->toast_ms - dt_ms : 0u;
    }

    /* The wipe owns the frame: the world does not walk and the fight does not
     * tick behind the black. */
    if(app->wipe_active) {
        ft_wipe_update(app, dt_ms);
        return;
    }

    if(app->show_help) return;
    if(app->mode == FT_MODE_TITLE || app->mode == FT_MODE_SETTINGS) return;
    if(app->mode == FT_MODE_INTRO) {
        ft_intro_tick(app, dt_ms);
        return;
    }
    if(app->mode == FT_MODE_PAUSE || app->mode == FT_MODE_PRACTICE) return;
    if(app->mode == FT_MODE_ORBS || app->mode == FT_MODE_CONFIRM) return;
    if(app->mode == FT_MODE_TALK) {
        ft_talk_tick(app, dt_ms);
        return;
    }
    if(app->mode == FT_MODE_QUESTS) return;
    if(app->mode == FT_MODE_POCKETS) return;
    if(app->mode == FT_MODE_DEBUG) return;
    if(app->mode == FT_MODE_GUIDE || app->mode == FT_MODE_GUIDE_ENTRY) return;

    if(app->mode == FT_MODE_BATTLE) {
        ft_encounter_tick(&app->encounter, dt_ms);
        ft_battle_sound(app);
        return;
    }

    /* Something seeing you is worth hearing, because the half-second beat
     * before it moves is only useful if you know it started. */
    bool was_noticing = false;
    for(uint8_t i = 0; i < FT_MAX_ROOM_ENTS; i++) {
        if(ft_world_foe_noticing(&app->world, i)) was_noticing = true;
    }

    int8_t dx = 0, dy = 0;
    if(app->held & HELD_LEFT) dx -= 1;
    if(app->held & HELD_RIGHT) dx += 1;
    if(app->held & HELD_UP) dy -= 1;
    if(app->held & HELD_DOWN) dy += 1;

    ft_world_update(&app->world, dx, dy, dt_ms);

    if(!was_noticing) {
        for(uint8_t i = 0; i < FT_MAX_ROOM_ENTS; i++) {
            if(ft_world_foe_noticing(&app->world, i)) {
                ft_sound_play(&app->sound, FT_SFX_SPOT);
                break;
            }
        }
    }

    /* Hale has just shown you the pit. The world keeps the bit; the app
     * makes the noise, says what happened, and keeps it on the card. */
    if(app->world.revealed_now) {
        ft_sound_play(&app->sound, FT_SFX_REVEAL);
        ft_toast(app, "A way down!");
        ft_save_now(app);
    }

    /* Echo sees you. It speaks in Hush's voice, because that is all it has
     * left, and it is kept on the card so it is only ever seen once. */
    if(app->world.echo_now) {
        ft_sound_play(&app->sound, FT_SFX_VOICE_HUSH);
        ft_save_now(app);
    }

    /* Doors take themselves the moment you finish stepping onto one: having
     * to stop and press to change room turns a corridor into paperwork. */
    if(app->world.arrived) ft_sound_play(&app->sound, FT_SFX_MOVE);

    if(app->world.arrived) {
        const FtExit* exit = ft_world_exit_under(&app->world);

        if(exit && !ft_world_exit_open(&app->world, exit)) {
            /* A way you have no reason to take, or a gate somebody is
             * holding. Both say so and leave you standing on the threshold,
             * which is what makes coming back here later mean something. */
            ft_toast(app, ft_world_exit_refusal(exit));
            ft_sound_play(&app->sound, FT_SFX_DENY);
        } else if(exit) {
            ft_world_enter(&app->world, exit->dest_room, exit->dest_tx, exit->dest_ty);
            return;
        }
    }

    /* Walking into a foe — or one walking into you — starts the fight without
     * the free hit. */
    const int touched = ft_world_foe_contact(&app->world);
    if(touched >= 0) {
        const bool ambush = app->world.ambushed;

        if(ambush) ft_toast(app, "Ambushed!");
        ft_begin_battle(app, touched, false, ambush);
        return;
    }

    /* Out of the hole with Wren behind you, and still nameless. */
    if(ft_world_naming_due(&app->world)) {
        ft_start_naming(app, 0, app->world.escort_mv.tx, app->world.escort_mv.ty);
    }
}

/* ---- Lifecycle ------------------------------------------------------- */

static FlipperTales* ft_alloc(void) {
    FlipperTales* app = malloc(sizeof(FlipperTales));

    app->queue = furi_message_queue_alloc(FT_QUEUE_SIZE, sizeof(FtEvent));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, ft_draw_callback, app);
    view_port_input_callback_set(app->view_port, ft_input_callback, app);

    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    ft_world_init(&app->world);

    app->mode = FT_MODE_OVERWORLD;
    app->battle_entity = -1;
    app->battle_first_strike = false;
    app->wipe_ms = 0;
    app->wipe_active = false;
    app->wipe_swapped = false;
    app->pend = FT_PEND_NONE;
    app->pend_entity = -1;
    app->pend_first_strike = false;
    app->pend_ambush = false;
    app->pend_won = false;
    app->in_practice = false;
    /* These three were being reset inside the pause menu's New game case
     * instead of here, so they started a run holding whatever malloc left
     * behind. Nothing crashed — ft_room clamps, and the list cursors are
     * taken modulo before they are moved — but the first frame of the debug
     * menu and the field guide could each highlight a row at random. */
    app->debug_item = 0;
    app->travel_room = 0;
    app->guide_item = 0;

    app->orb_item = 0;
    app->orbs_from_pause = false;
    app->levelled = false;
    app->sound_phase = FT_PHASE_MENU;
    app->sound_struck = false;
    app->quest_item = 0;
    app->pocket_item = 0;
    app->talk_beat = 0;
    app->talk_choosing = false;
    app->talk_yes = true;
    app->talk_quest = FT_QUEST_CLEAN_RUN;
    app->talk_is_wren = false;
    app->talk_is_hale = false;
    app->talk_is_naming = false;
    app->naming_try = 0;
    app->talk.count = 0;
    app->talk_shown = 0;
    app->talk_type_ms = 0;
    app->talk_fx = 0;
    app->talk_fy = 0;
    app->talk_slot = 0;
    for(uint8_t i = 0; i < FT_VOICE_COUNT; i++) {
        app->talk_first[i] = NULL;
        app->talk_again[i] = 0;
    }
    app->chapters_done = 0;
    app->confirm_yes = false;
    ft_practice_init(&app->practice, furi_get_tick());
    app->held = 0;
    app->toast = NULL;
    app->toast_ms = 0;
    app->coach = true;
    ft_sound_init(&app->sound, true);

    /* Pick the run back up where it was left. A missing, corrupt or
     * wrong-version file just means a new game — never a refusal to start.
     *
     * This is also why the tutorial is skipped on a resume: someone with a
     * save has already seen it. */
    app->voices = true;
    app->combo = 0;
    app->settings_item = 0;
    app->settings_from = FT_MODE_TITLE;
    app->confirm_from = FT_MODE_TITLE;
    app->intro_line = 0;
    app->intro_ms = 0;
    app->intro_voiced = 0;

    /* The settings live in the save, so read them now even though the run
     * itself waits for Continue: the start screen should already sound the
     * way the player left it. */
    FtSaveData saved;
    if(ft_storage_load(&saved)) {
        ft_sound_set(&app->sound, saved.sound);
        app->coach = saved.coach;
        app->voices = saved.voices;
    }

    app->show_help = false;
    app->help_page = 0;
    app->paused_from = FT_MODE_OVERWORLD;
    app->pause_item = FT_PAUSE_POCKETS;
    app->running = true;

    ft_to_title(app);

    return app;
}

static void ft_free(FlipperTales* app) {
    /* Hand the speaker back before anything else: leaving it held would
     * lock every other app out of it until the Flipper is restarted. */
    ft_sound_stop(&app->sound);

    gui_remove_view_port(app->gui, app->view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(app->view_port);

    furi_mutex_free(app->mutex);
    furi_message_queue_free(app->queue);

    free(app);
}

int32_t flipper_tales_app(void* p) {
    UNUSED(p);

    FlipperTales* app = ft_alloc();

    const uint32_t tick_hz = furi_kernel_get_tick_frequency() ?
                                 furi_kernel_get_tick_frequency() :
                                 1000u;
    uint32_t last_tick = furi_get_tick();
    uint32_t last_draw = last_tick;

    while(app->running) {
        FtEvent event;
        const FuriStatus status =
            furi_message_queue_get(app->queue, &event, FT_TICK_MS);

        /* Real elapsed time, so the guard windows stay honest even if the
         * queue wakes us early or late. */
        const uint32_t now = furi_get_tick();
        uint32_t dt_ms = 0;
        if(now != last_tick) {
            dt_ms = ((now - last_tick) * 1000u) / tick_hz;
            last_tick = now;
        }

        /* A long stall (debugger, SD access) must not teleport the game
         * through several phases at once. */
        if(dt_ms > 250u) dt_ms = 250u;

        bool acted = false;

        furi_mutex_acquire(app->mutex, FuriWaitForever);

        if(status == FuriStatusOk && event.type == FtEventInput) {
            ft_handle_input(app, &event.input);
            acted = true;
        }

        if(dt_ms > 0) ft_update(app, dt_ms);

        furi_mutex_release(app->mutex);

        /* Draw on input immediately so the controls feel instant, otherwise at
         * the frame cap. Asking for a redraw every 10 ms buries the GUI thread
         * under work it cannot finish, which is what backed the input queue up
         * and wedged an earlier build. */
        if(acted || (uint32_t)(now - last_draw) >= FT_FRAME_MS) {
            last_draw = now;
            view_port_update(app->view_port);
        }
    }

    ft_free(app);

    return 0;
}
