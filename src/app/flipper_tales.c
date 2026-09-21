/* Flipper Tales — application entry point.
 *
 * This layer is deliberately thin. It owns the GUI, the input queue and the
 * clock; all game logic lives in src/core, which has no Flipper dependency
 * and is unit-tested on a host machine (see test/). */

#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>

#include "../core/ft_encounter.h"
#include "ft_render.h"

#define FT_TAG        "FlipperTales"
#define FT_TICK_MS    10   /* ~100 Hz: the capture window is only 50 ms, so the
                            * press must be timestamped finer than that */
#define FT_QUEUE_SIZE 8

typedef enum {
    FtEventInput = 0,
    FtEventTick
} FtEventType;

typedef struct {
    FtEventType type;
    InputEvent  input;
} FtEvent;

typedef struct {
    FuriMessageQueue* queue;
    FuriMutex*        mutex;
    ViewPort*         view_port;
    Gui*              gui;

    FtLoadout   loadout;
    FtEncounter encounter;
    uint8_t     enemy_index;
    bool        coach;

    /* Shown on launch: the timing windows are the whole game and are not
     * self-evident, so the rules go up before the first turn rather than
     * hiding behind a hint in the corner. */
    bool    show_help;
    uint8_t help_page;
    bool    running;
} FlipperTales;

/* ---- GUI callbacks --------------------------------------------------- */

static void ft_draw_callback(Canvas* canvas, void* ctx) {
    FlipperTales* app = ctx;

    /* Never block the GUI thread: if the state is mid-update, skip this frame
     * rather than stall the compositor. */
    if(furi_mutex_acquire(app->mutex, 25) != FuriStatusOk) return;

    if(app->show_help) {
        ft_render_help(canvas, app->help_page);
    } else {
        ft_render_battle(canvas, &app->encounter);
    }

    furi_mutex_release(app->mutex);
}

static void ft_input_callback(InputEvent* event, void* ctx) {
    FlipperTales* app = ctx;
    const FtEvent msg = {.type = FtEventInput, .input = *event};
    furi_message_queue_put(app->queue, &msg, FuriWaitForever);
}

/* ---- Input ----------------------------------------------------------- */

static void ft_start_encounter(FlipperTales* app, uint8_t enemy_index) {
    /* ft_encounter_init resets coaching to on, so carry the player's choice
     * across fights rather than nagging them again each time. */
    const bool coach = app->coach;

    app->enemy_index = (uint8_t)(enemy_index % FT_ENEMY_COUNT);
    ft_encounter_init(&app->encounter, (FtEnemyId)app->enemy_index, &app->loadout,
                      furi_get_tick());

    app->encounter.coach = coach;
}

static void ft_handle_input(FlipperTales* app, const InputEvent* event) {
    /* Guard and action timing must react to the physical press, not the
     * debounced short-press, or the 50 ms capture window is unreachable. */
    const bool pressed = (event->type == InputTypePress);
    const bool repeated = (event->type == InputTypeRepeat);

    if(!pressed && !repeated) return;

    /* OK acts on the physical press only. Holding it emits Repeat events, and
     * accepting those would let a held confirm in the menu fall straight
     * through into the action command and register a press at t=0 — an
     * automatic miss for anyone who does not tap cleanly. */
    if(event->key == InputKeyOk && !pressed) return;

    if(app->show_help) {
        if(!pressed && !repeated) return;

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

    switch(event->key) {
    case InputKeyBack:
        if(!pressed) return;
        app->running = false;
        break;

    case InputKeyOk:
        if(ft_encounter_over(&app->encounter)) {
            /* Cycle through the M1 enemies so all three locks are reachable. */
            ft_start_encounter(app, (uint8_t)(app->enemy_index + 1u));
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

    /* The menu is a 2x2 grid, so vertical movement is a step of two. */
    case InputKeyUp:
        if(ft_encounter_over(&app->encounter)) {
            app->show_help = true;
        } else {
            ft_encounter_menu_move(&app->encounter, -2);
        }
        break;

    case InputKeyDown:
        if(app->encounter.phase == FT_PHASE_MENU) {
            ft_encounter_menu_move(&app->encounter, 2);
        } else {
            /* Outside the grid, DOWN silences or restores the coach. */
            app->coach = !app->coach;
            app->encounter.coach = app->coach;
        }
        break;

    default:
        break;
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

    ft_loadout_init(&app->loadout);
    app->enemy_index = 0;
    app->coach = true;
    ft_start_encounter(app, 0);

    app->show_help = true;
    app->help_page = 0;
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

    const uint32_t tick_hz = furi_kernel_get_tick_frequency();
    uint32_t last_tick = furi_get_tick();

    while(app->running) {
        FtEvent event;
        const FuriStatus status =
            furi_message_queue_get(app->queue, &event, FT_TICK_MS);

        /* Real elapsed time, so the guard windows stay honest even if the
         * queue wakes us early or late. */
        const uint32_t now = furi_get_tick();
        uint32_t dt_ms = 0;
        if(now != last_tick && tick_hz > 0) {
            dt_ms = ((now - last_tick) * 1000u) / tick_hz;
            last_tick = now;
        }

        furi_mutex_acquire(app->mutex, FuriWaitForever);

        if(status == FuriStatusOk && event.type == FtEventInput) {
            ft_handle_input(app, &event.input);
        }

        if(dt_ms > 0) ft_encounter_tick(&app->encounter, dt_ms);

        furi_mutex_release(app->mutex);

        view_port_update(app->view_port);
    }

    ft_free(app);

    return 0;
}
