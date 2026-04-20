/*
 * Moon GPS — live readout of the paired phone's GPS fix.
 *
 * Subscribes to the Moon Companion position stream on startup, renders
 * the latest fix every time one lands (and otherwise on a 1 Hz tick so
 * the "age" line stays fresh). Back exits.
 *
 * Depends on the moon_companion service for the link; if the phone
 * isn't paired/connected the screen shows the connection state instead
 * of a fake fix.
 */

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <input/input.h>

#include <moon_companion/moon_companion.h>

#define TAG "MoonGps"

typedef enum {
    EvKey,
    EvTick,
    EvPosition,
} EvKind;

typedef struct {
    EvKind kind;
    InputEvent input;
} Ev;

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    MoonCompanion* moon;
    FuriMessageQueue* queue;
    FuriTimer* tick;

    FuriMutex* mutex;
    MoonPosition last_pos;
    bool has_fix;
} App;

static void on_position(const MoonPosition* pos, void* ctx) {
    App* app = ctx;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->last_pos = *pos;
    app->has_fix = true;
    furi_mutex_release(app->mutex);
    Ev e = {.kind = EvPosition};
    furi_message_queue_put(app->queue, &e, 0);
    view_port_update(app->view_port);
}

static void on_input(InputEvent* event, void* ctx) {
    App* app = ctx;
    Ev e = {.kind = EvKey, .input = *event};
    furi_message_queue_put(app->queue, &e, FuriWaitForever);
}

static void on_tick(void* ctx) {
    App* app = ctx;
    Ev e = {.kind = EvTick};
    furi_message_queue_put(app->queue, &e, 0);
    view_port_update(app->view_port);
}

/* fixed-point degrees formatter: value is degrees * 1e7 → "XX.XXXXXXX". */
static void format_deg(int32_t e7, char* buf, size_t n) {
    int32_t whole = e7 / 10000000;
    int32_t frac = e7 - whole * 10000000;
    if(frac < 0) frac = -frac;
    snprintf(buf, n, "%ld.%07ld", (long)whole, (long)frac);
}

static const char* state_label(MoonConnectionState s) {
    switch(s) {
    case MoonConnStateDisconnected: return "Disconnected";
    case MoonConnStateScanning:     return "Scanning...";
    case MoonConnStateConnecting:   return "Connecting...";
    case MoonConnStateConnected:    return "Connected";
    case MoonConnStateYielded:      return "Radio yielded";
    }
    return "?";
}

static const char* fix_label(MoonFixQuality q) {
    switch(q) {
    case MoonFixNone: return "none";
    case MoonFix2D:   return "2D";
    case MoonFix3D:   return "3D";
    }
    return "?";
}

static void draw_callback(Canvas* canvas, void* ctx) {
    App* app = ctx;
    canvas_clear(canvas);

    MoonConnectionState state = moon_companion_get_state(app->moon);

    /* Header */
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Moon GPS");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 66, 10, state_label(state));
    canvas_draw_line(canvas, 0, 12, 128, 12);

    /* Snapshot under mutex */
    MoonPosition pos;
    bool have;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    pos = app->last_pos;
    have = app->has_fix;
    furi_mutex_release(app->mutex);

    char buf[48];
    if(!have || pos.fix_quality == MoonFixNone) {
        canvas_draw_str(canvas, 2, 30, "Waiting for fix...");
        if(state != MoonConnStateConnected) {
            canvas_draw_str(canvas, 2, 44, "Pair phone in Settings");
            canvas_draw_str(canvas, 2, 54, "> Moon Companion");
        } else {
            canvas_draw_str(canvas, 2, 44, "Open the Moon Companion");
            canvas_draw_str(canvas, 2, 54, "app on the phone");
        }
        canvas_draw_str(canvas, 2, 62, "[Back] exit");
        return;
    }

    /* Lat / Lon */
    char lat[16], lon[16];
    format_deg(pos.lat_e7, lat, sizeof(lat));
    format_deg(pos.lon_e7, lon, sizeof(lon));
    snprintf(buf, sizeof(buf), "%s", lat);
    canvas_draw_str(canvas, 2, 22, "Lat");
    canvas_draw_str(canvas, 24, 22, buf);
    snprintf(buf, sizeof(buf), "%s", lon);
    canvas_draw_str(canvas, 2, 31, "Lon");
    canvas_draw_str(canvas, 24, 31, buf);

    /* Accuracy + satellites */
    snprintf(buf, sizeof(buf), "+/- %lu m  sats %u",
             (unsigned long)(pos.accuracy_mm / 1000), pos.satellites);
    canvas_draw_str(canvas, 2, 40, buf);

    /* Altitude + fix quality */
    snprintf(buf, sizeof(buf), "alt %ld m  fix %s",
             (long)(pos.alt_mm / 1000), fix_label(pos.fix_quality));
    canvas_draw_str(canvas, 2, 49, buf);

    /* Speed (km/h) + age */
    uint32_t age_s = (furi_get_tick() - pos.tick) / furi_kernel_get_tick_frequency();
    if(age_s > 9999) age_s = 9999;
    uint32_t kmh_x10 = (pos.speed_mmps * 36) / 10000; /* mm/s → km/h * 10 */
    snprintf(buf, sizeof(buf), "spd %lu.%lu km/h  %lus",
             (unsigned long)(kmh_x10 / 10),
             (unsigned long)(kmh_x10 % 10),
             (unsigned long)age_s);
    canvas_draw_str(canvas, 2, 58, buf);
}

int32_t moon_gps_app(void* p) {
    UNUSED(p);

    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(*app));

    app->queue = furi_message_queue_alloc(16, sizeof(Ev));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, draw_callback, app);
    view_port_input_callback_set(app->view_port, on_input, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    app->moon = furi_record_open(RECORD_MOON_COMPANION);

    /* Prime with a cached fix if one is already available. */
    MoonPosition pos;
    if(moon_companion_get_position(app->moon, &pos, 0)) {
        app->last_pos = pos;
        app->has_fix = true;
    }
    moon_companion_subscribe_position(app->moon, on_position, app);

    /* 1 Hz tick just so the age counter refreshes when the stream is
     * quiet (phone reports at ~0.5 Hz by default). */
    app->tick = furi_timer_alloc(on_tick, FuriTimerTypePeriodic, app);
    furi_timer_start(app->tick, furi_ms_to_ticks(1000));

    Ev e;
    bool running = true;
    while(running) {
        if(furi_message_queue_get(app->queue, &e, FuriWaitForever) != FuriStatusOk) continue;
        if(e.kind == EvKey && e.input.type == InputTypeShort && e.input.key == InputKeyBack) {
            running = false;
        }
    }

    furi_timer_stop(app->tick);
    furi_timer_free(app->tick);
    moon_companion_unsubscribe_position(app->moon, on_position, app);
    furi_record_close(RECORD_MOON_COMPANION);
    gui_remove_view_port(app->gui, app->view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(app->view_port);
    furi_message_queue_free(app->queue);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
