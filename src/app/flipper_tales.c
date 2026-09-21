/* Flipper Tales — application entry point.
 *
 * This layer is deliberately thin. It owns the GUI, the input queue and the
 * clock; all game logic lives in src/core, which has no Flipper dependency
 * and is unit-tested on a host machine (see test/). */

#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>

#include "../core/ft_encounter.h"
#include "../core/ft_world.h"
#include "ft_overworld.h"
#include "../core/ft_practice.h"
#include "ft_render.h"
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
    FT_MODE_BATTLE,
    FT_MODE_PAUSE,
    FT_MODE_PRACTICE, /* the arena's setup screen */
    FT_MODE_LEVELUP,  /* spending the levels a win just paid out */
    FT_MODE_CONFIRM,  /* the gate in front of erasing a run */
    FT_MODE_DEBUG     /* the testing tools, kept out of the player's way */
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

    /* Levels a win owes but the player has not spent yet. They are spent one
     * at a time, because each one is a choice. */
    int16_t levels_owed;
    uint8_t levelup_item;

    /* The New game confirmation. Defaults to No. */
    bool confirm_yes;

    /* The debug menu, and the room its Travel row is pointing at. */
    uint8_t debug_item;
    uint8_t travel_room;

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

    bool running;
} FlipperTales;

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
    } else if(app->mode == FT_MODE_PAUSE) {
        ft_render_pause(canvas, app->pause_item, app->coach);
    } else if(app->mode == FT_MODE_PRACTICE) {
        ft_render_practice(canvas, &app->practice);
    } else if(app->mode == FT_MODE_DEBUG) {
        ft_render_debug(canvas, app->debug_item,
                        ft_room(app->travel_room)->map->name);
    } else if(app->mode == FT_MODE_CONFIRM) {
        ft_render_confirm(canvas, "Erase this run?", app->confirm_yes);
    } else if(app->mode == FT_MODE_LEVELUP) {
        ft_render_levelup(
            canvas, &app->world.stats, app->levelup_item, app->levels_owed);
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

static void ft_enter_battle_now(FlipperTales* app, int entity, bool first_strike) {
    const FtRoom* room = ft_room(app->world.room);
    const FtRoster* roster = ft_roster(room->ents[entity].roster);

    ft_encounter_init(
        &app->encounter, roster->foes, roster->count, &app->world.loadout,
        furi_get_tick());

    /* Carry the player across: an encounter builds a level-one character on
     * its own, which is right for a standalone fight and wrong here. */
    app->encounter.stats = app->world.stats;
    ft_roll_init(&app->encounter.roll, app->world.stats.charge);
    app->encounter.lib = app->world.lib;
    app->encounter.coach = app->coach;

    if(first_strike) {
        /* Hitting it out here means it enters already hurt. */
        app->encounter.foes[0].charge =
            (int16_t)(app->encounter.foes[0].charge - FT_FIRST_STRIKE_DAMAGE);
        if(app->encounter.foes[0].charge < 1) app->encounter.foes[0].charge = 1;
    }

    app->battle_entity = entity;
    app->battle_first_strike = first_strike;
    app->mode = FT_MODE_BATTLE;
}

static void ft_leave_battle_now(FlipperTales* app, bool won) {
    /* Carry the player back out, including anything captured in the fight. */
    app->world.stats = app->encounter.stats;
    app->world.stats.charge = app->encounter.roll.current;
    app->world.lib = app->encounter.lib;
    app->coach = false;

    if(won) {
        if(app->battle_entity >= 0) {
            ft_world_clear_entity(&app->world, (uint8_t)app->battle_entity);
        }
        if(app->world.stats.charge < 1) app->world.stats.charge = 1;

        /* XP is banked on the way out, and any levels it bought are spent on
         * the next frame — the level-up screen owns that, not this. */
        const int16_t xp = ft_encounter_xp(&app->encounter);
        app->levels_owed = (int16_t)(
            app->levels_owed +
            ft_xp_gain(&app->world.stats, xp, ft_level_cap(app->chapters_done)));

        ft_toast(app, "Cleared.");
    } else {
        /* Downed: back to the terminal you last saved at, patched up. It used
         * to be hardcoded to the first room, which quietly undid any progress
         * past it. */
        app->world.stats.charge = app->world.stats.charge_max;
        ft_world_enter(
            &app->world, app->world.save_room, app->world.save_tx, app->world.save_ty);
        ft_toast(app, "Rebooted.");
    }

    app->battle_entity = -1;

    /* A level owed takes the screen before the world comes back. */
    if(app->levels_owed > 0) {
        app->levelup_item = 0;
        app->mode = FT_MODE_LEVELUP;
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

static void ft_begin_battle(FlipperTales* app, int entity, bool first_strike) {
    if(ft_wiping(app)) return;

    app->pend_entity = entity;
    app->pend_first_strike = first_strike;
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
            ft_enter_battle_now(app, app->pend_entity, app->pend_first_strike);
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

        app->levels_owed = (int16_t)(app->levels_owed + owed);
        if(app->levels_owed > 0) {
            app->levelup_item = 0;
            app->mode = FT_MODE_LEVELUP;
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
    ft_save_from_world(&app->world, app->coach, &data);
    return ft_storage_save(&data);
}

static void ft_overworld_ok(FlipperTales* app) {
    /* A foe you are facing is struck before it can react. */
    const int ahead = ft_world_foe_ahead(&app->world);
    if(ahead >= 0) {
        ft_begin_battle(app, ahead, true);
        return;
    }

    if(ft_world_terminal_near(&app->world)) {
        app->world.stats.charge = app->world.stats.charge_max;
        app->world.stats.ram = app->world.stats.ram_max;

        /* A terminal is the save point, so using one saves. Restoring without
         * saving would mean the thing you walked across the room for did only
         * half of what it is for. */
        ft_save_here(app);
        ft_toast(app, ft_save_now(app) ? "Saved. Restored." : "Restored. No card.");
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

    if(app->mode == FT_MODE_PAUSE) {
        switch(event->key) {
        case InputKeyUp:
            app->pause_item = (uint8_t)((app->pause_item + FT_PAUSE_COUNT - 1u) % FT_PAUSE_COUNT);
            break;
        case InputKeyDown:
            app->pause_item = (uint8_t)((app->pause_item + 1u) % FT_PAUSE_COUNT);
            break;
        case InputKeyBack:
            app->mode = app->paused_from;
            break;
        case InputKeyOk:
            switch(app->pause_item) {
            case FT_PAUSE_RESUME:
                app->mode = app->paused_from;
                break;
            case FT_PAUSE_HELP:
                app->mode = app->paused_from;
                app->show_help = true;
                app->help_page = 0;
                break;
            case FT_PAUSE_SAVE:
                /* Terminals are the save point, so this says where to find
                 * one rather than quietly doing nothing. */
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
            case FT_PAUSE_DEBUG:
                app->debug_item = 0;
                app->mode = FT_MODE_DEBUG;
                break;
            case FT_PAUSE_NEWGAME:
                app->confirm_yes = false;
    app->debug_item = 0;
    app->travel_room = 0;
                app->mode = FT_MODE_CONFIRM;
                break;
            case FT_PAUSE_TIPS:
                app->coach = !app->coach;
                app->encounter.coach = app->coach;
                break;
            case FT_PAUSE_QUIT:
            default:
                app->running = false;
                break;
            }
            break;
        default:
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
                ft_storage_erase();
                ft_world_init(&app->world);
                app->coach = true;
                app->levels_owed = 0;
                app->battle_entity = -1;
                ft_toast(app, "New run.");
            }
            app->mode = FT_MODE_OVERWORLD;
            break;
        case InputKeyBack:
        default:
            app->mode = FT_MODE_PAUSE;
            break;
        }
        return;
    }

    if(app->mode == FT_MODE_LEVELUP) {
        switch(event->key) {
        case InputKeyUp:
            app->levelup_item = (uint8_t)((app->levelup_item + 2u) % 3u);
            break;
        case InputKeyDown:
            app->levelup_item = (uint8_t)((app->levelup_item + 1u) % 3u);
            break;
        case InputKeyOk: {
            static const FtLevelChoice CHOICE[3] = {
                FT_UP_CHARGE, FT_UP_RAM, FT_UP_FLASH};

            /* A capped stat refuses, and the level stays owed — there is no
             * way to lose one by pressing OK on the wrong row. */
            if(!ft_level_apply(&app->world.stats, CHOICE[app->levelup_item])) break;

            app->levels_owed--;
            if(app->levels_owed > 0) break; /* straight on to the next one */

            /* Levelling is progress worth keeping even if the player walks
             * into something and goes down on the next screen. */
            ft_save_now(app);
            app->mode = FT_MODE_OVERWORLD;
            break;
        }
        default:
            /* BACK does not dismiss it: the level is owed either way, and a
             * screen you can escape from is a stat you can lose. */
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

    /* Back closes the attack panel first, and only then opens the pause menu:
     * backing out of a submenu should not quit the game. */
    if(event->key == InputKeyBack) {
        if(pressed && app->mode == FT_MODE_BATTLE &&
           ft_encounter_menu_back(&app->encounter)) {
            return;
        }
        if(pressed) {
            app->paused_from = app->mode;
            app->pause_item = FT_PAUSE_RESUME;
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
    /* UP and DOWN move the cursor too, so the whole menu can be driven on
     * either axis. They used to pick a target; targeting is automatic now. */
    case InputKeyUp:
        if(ft_encounter_over(&app->encounter)) app->show_help = true;
        else ft_encounter_menu_move(&app->encounter, -1);
        break;
    case InputKeyDown:
        ft_encounter_menu_move(&app->encounter, 1);
        break;
    default:
        break;
    }
}

/* ---- Update ---------------------------------------------------------- */

static void ft_update(FlipperTales* app, uint32_t dt_ms) {
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
    if(app->mode == FT_MODE_PAUSE || app->mode == FT_MODE_PRACTICE) return;
    if(app->mode == FT_MODE_LEVELUP || app->mode == FT_MODE_CONFIRM) return;
    if(app->mode == FT_MODE_DEBUG) return;

    if(app->mode == FT_MODE_BATTLE) {
        ft_encounter_tick(&app->encounter, dt_ms);
        return;
    }

    int8_t dx = 0, dy = 0;
    if(app->held & HELD_LEFT) dx -= 1;
    if(app->held & HELD_RIGHT) dx += 1;
    if(app->held & HELD_UP) dy -= 1;
    if(app->held & HELD_DOWN) dy += 1;

    ft_world_update(&app->world, dx, dy, dt_ms);

    /* Doors take themselves the moment you finish stepping onto one: having
     * to stop and press to change room turns a corridor into paperwork. */
    if(app->world.arrived) {
        const FtExit* exit = ft_world_exit_under(&app->world);
        if(exit) {
            ft_world_enter(&app->world, exit->dest_room, exit->dest_tx, exit->dest_ty);
            return;
        }
    }

    /* Walking into a foe — or one walking into you — starts the fight without
     * the free hit. */
    const int touched = ft_world_foe_contact(&app->world);
    if(touched >= 0) ft_begin_battle(app, touched, false);
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
    app->pend_won = false;
    app->in_practice = false;
    app->levels_owed = 0;
    app->levelup_item = 0;
    app->chapters_done = 0;
    app->confirm_yes = false;
    ft_practice_init(&app->practice, furi_get_tick());
    app->held = 0;
    app->toast = NULL;
    app->toast_ms = 0;
    app->coach = true;

    /* Pick the run back up where it was left. A missing, corrupt or
     * wrong-version file just means a new game — never a refusal to start.
     *
     * This is also why the tutorial is skipped on a resume: someone with a
     * save has already seen it. */
    FtSaveData saved;
    const bool resumed = ft_storage_load(&saved);
    if(resumed) ft_save_to_world(&saved, &app->world, &app->coach);

    app->show_help = !resumed;
    app->help_page = 0;
    app->paused_from = FT_MODE_OVERWORLD;
    app->pause_item = FT_PAUSE_RESUME;
    app->running = true;

    return app;
}

static void ft_free(FlipperTales* app) {
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
