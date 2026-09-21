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
#define FT_FRAME_MS   33   /* ~30 fps. The logic still ticks at FT_TICK_MS;
                            * only drawing is capped. Redrawing a sprite-heavy
                            * scene at 100 Hz saturates the GUI thread. */
#define FT_QUEUE_SIZE 16

/* A single tap produces Press, Release and Short, so the queue needs slack for
 * bursts. Anything beyond that is dropped rather than blocking — see
 * ft_input_callback. */

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

    /* Never wait on the GUI thread. A skipped frame is invisible at 30 fps;
     * a stalled compositor also stalls input dispatch. */
    if(furi_mutex_acquire(app->mutex, 0) != FuriStatusOk) return;

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

    /* Timeout 0, never FuriWaitForever. This runs on the GUI service thread:
     * blocking here when the queue is full wedges that thread, which then
     * stops dispatching input and stops running the draw callback, and nothing
     * can recover it. Dropping an event is survivable — the player presses
     * again — so the queue must never be allowed to block the caller. */
    furi_message_queue_put(app->queue, &msg, 0);
}

/* ---- Input ----------------------------------------------------------- */

/* A short gauntlet: each fight introduces one more idea, ending with a mixed
 * group so broadcast-versus-contact actually has to be chosen. */
typedef struct {
    uint8_t   count;
    FtEnemyId foes[FT_MAX_ENEMIES];
} FtRoster;

static const FtRoster FT_ROSTERS[] = {
    {1, {FT_ENEMY_STRAY_PACKET, 0, 0}},
    {1, {FT_ENEMY_DRIFT_BEACON, 0, 0}},
    {1, {FT_ENEMY_SEALED_LOCK, 0, 0}},
    {2, {FT_ENEMY_STRAY_PACKET, FT_ENEMY_STRAY_PACKET, 0}},
    {2, {FT_ENEMY_DRIFT_BEACON, FT_ENEMY_SEALED_LOCK, 0}},
    {3, {FT_ENEMY_STRAY_PACKET, FT_ENEMY_DRIFT_BEACON, FT_ENEMY_SEALED_LOCK}},
};
#define FT_ROSTER_COUNT (sizeof(FT_ROSTERS) / sizeof(FT_ROSTERS[0]))

static void ft_start_encounter(FlipperTales* app, uint8_t index) {
    /* ft_encounter_init resets coaching to on, so carry the player's state
     * across fights rather than nagging them again each time. */
    const bool coach = app->coach;

    app->enemy_index = (uint8_t)(index % FT_ROSTER_COUNT);
    const FtRoster* r = &FT_ROSTERS[app->enemy_index];

    ft_encounter_init(&app->encounter, r->foes, r->count, &app->loadout, furi_get_tick());

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
            /* One fight is enough to learn the timings; the help deck stays
             * reachable with UP rather than the coach nagging forever. */
            app->coach = false;
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
    /* The menu is a single row, so UP and DOWN are free to pick a target. */
    case InputKeyUp:
        if(ft_encounter_over(&app->encounter)) {
            app->show_help = true;
        } else {
            ft_encounter_target_move(&app->encounter, -1);
        }
        break;

    case InputKeyDown:
        ft_encounter_target_move(&app->encounter, 1);
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

        /* A long stall (debugger, SD access) must not teleport the battle
         * through several phases at once. */
        if(dt_ms > 250u) dt_ms = 250u;

        bool acted = false;

        furi_mutex_acquire(app->mutex, FuriWaitForever);

        if(status == FuriStatusOk && event.type == FtEventInput) {
            ft_handle_input(app, &event.input);
            acted = true;
        }

        if(dt_ms > 0) ft_encounter_tick(&app->encounter, dt_ms);

        furi_mutex_release(app->mutex);

        /* Draw on input immediately so the controls feel instant, otherwise at
         * the frame cap. Asking for a redraw every 10 ms buries the GUI thread
         * under work it cannot finish, which is what eventually backed the
         * input queue up. */
        if(acted || (uint32_t)(now - last_draw) >= FT_FRAME_MS) {
            last_draw = now;
            view_port_update(app->view_port);
        }
    }

    ft_free(app);

    return 0;
}
