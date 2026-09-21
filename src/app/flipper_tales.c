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
#include "ft_render.h"

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
    FT_MODE_PAUSE
} FtMode;

typedef struct {
    FuriMessageQueue* queue;
    FuriMutex*        mutex;
    ViewPort*         view_port;
    Gui*              gui;

    FtMode      mode;
    FtWorld     world;
    FtEncounter encounter;

    /* Which entity started the current battle, so it can be removed on a win. */
    int  battle_entity;
    bool battle_first_strike;

    /* D-pad is level-triggered: the queue gives presses and releases, and the
     * walk needs to know what is held right now. */
    uint8_t held;

    const char* toast;
    uint32_t    toast_ms;

    bool    show_help;
    uint8_t help_page;
    bool    coach;

    FtMode  paused_from;
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
    } else if(app->mode == FT_MODE_BATTLE) {
        ft_render_battle(canvas, &app->encounter);
    } else {
        ft_overworld_render(canvas, &app->world);

        if(app->toast_ms > 0) ft_overworld_toast(canvas, app->toast);
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

static void ft_begin_battle(FlipperTales* app, int entity, bool first_strike) {
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

static void ft_end_battle(FlipperTales* app, bool won) {
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
        ft_toast(app, "Cleared.");
    } else {
        /* Downed: back to the first terminal, patched up. Terminals are the
         * only save point, so they are also where you come back. */
        app->world.stats.charge = app->world.stats.charge_max;
        ft_world_enter(&app->world, 0, 5, 3);
        ft_toast(app, "Rebooted.");
    }

    app->battle_entity = -1;
    app->mode = FT_MODE_OVERWORLD;
}

/* ---- Input ----------------------------------------------------------- */

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
        ft_toast(app, "Restored.");
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
    case InputKeyUp:
        if(ft_encounter_over(&app->encounter)) {
            app->show_help = true;
        } else if(app->encounter.menu_level == FT_MENU_ATTACK) {
            ft_encounter_menu_move(&app->encounter, -1);
        } else {
            ft_encounter_target_move(&app->encounter, -1);
        }
        break;
    case InputKeyDown:
        if(app->encounter.menu_level == FT_MENU_ATTACK) {
            ft_encounter_menu_move(&app->encounter, 1);
        } else {
            ft_encounter_target_move(&app->encounter, 1);
        }
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

    if(app->show_help || app->mode == FT_MODE_PAUSE) return;

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
    app->held = 0;
    app->toast = NULL;
    app->toast_ms = 0;
    app->coach = true;

    app->show_help = true;
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
