/*
 * Moon API — call the moon-os.dev API endpoints through the paired
 * phone's HTTP proxy and show the result on screen.
 *
 * UI: UP/DOWN cycles endpoints, OK fetches, BACK exits. The fetch runs
 * on a worker thread so the UI stays responsive during the ~few-second
 * round trip; a spinner-style "Fetching..." status shows while it's in
 * flight.
 *
 * Inline-only: responses bigger than ~150 B (the single-notification
 * budget after envelope overhead) come back MOON_NOT_AVAILABLE from the
 * phone. /api/health, /api/apps, and /api/firmware all fit comfortably.
 */

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <input/input.h>

#include <moon_companion/moon_companion.h>

#define TAG "MoonApi"

static const char* ENDPOINTS[] = {
    "/api/health",
    "/api/apps",
    "/api/firmware",
};
#define NUM_ENDPOINTS (sizeof(ENDPOINTS) / sizeof(ENDPOINTS[0]))

#define BODY_PREVIEW_MAX 128
#define BASE_URL         "https://moon-os.dev"

typedef enum {
    EvKey,
    EvHttpDone,
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
    FuriMutex* mutex;

    /* Guarded by `mutex`. */
    size_t   selected;
    bool     in_flight;
    bool     have_response;
    bool     last_failed;
    uint32_t last_status;
    char     body_preview[BODY_PREVIEW_MAX];
    size_t   body_len;

    /* Worker thread for the in-flight request, or NULL. */
    FuriThread* worker;
} App;

static void on_input(InputEvent* event, void* ctx) {
    App* app = ctx;
    Ev e = {.kind = EvKey, .input = *event};
    furi_message_queue_put(app->queue, &e, FuriWaitForever);
}

/* Strip non-printable bytes (control chars, high-bit-set) in place so
 * we can safely blit a JSON preview with canvas_draw_str. */
static void sanitize_preview(char* buf, size_t len) {
    for(size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)buf[i];
        if(c < 0x20 || c > 0x7E) buf[i] = '.';
    }
}

static int32_t http_worker(void* ctx) {
    App* app = ctx;

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    size_t sel = app->selected;
    furi_mutex_release(app->mutex);

    char url[160];
    snprintf(url, sizeof(url), "%s%s", BASE_URL, ENDPOINTS[sel]);

    MoonHttpRequest req = {
        .method     = "GET",
        .url        = url,
        .timeout_ms = 15000,
    };
    MoonHttpResponse resp;
    memset(&resp, 0, sizeof(resp));
    bool ok = moon_companion_http_request(app->moon, &req, &resp);

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->have_response = true;
    app->last_failed = !ok;
    if(ok) {
        app->last_status = resp.status_code;
        size_t copy_len = resp.body_len;
        if(copy_len >= sizeof(app->body_preview)) copy_len = sizeof(app->body_preview) - 1;
        if(copy_len && resp.body) memcpy(app->body_preview, resp.body, copy_len);
        app->body_preview[copy_len] = '\0';
        app->body_len = copy_len;
        sanitize_preview(app->body_preview, copy_len);
    } else {
        app->last_status = 0;
        app->body_preview[0] = '\0';
        app->body_len = 0;
    }
    app->in_flight = false;
    furi_mutex_release(app->mutex);

    if(ok) moon_companion_http_response_free(&resp);

    Ev e = {.kind = EvHttpDone};
    furi_message_queue_put(app->queue, &e, 0);
    view_port_update(app->view_port);
    return 0;
}

static void start_fetch(App* app) {
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    if(app->in_flight) {
        furi_mutex_release(app->mutex);
        return;
    }
    app->in_flight = true;
    app->have_response = false;
    app->last_failed = false;
    furi_mutex_release(app->mutex);

    /* Reap any previous worker before spawning a new one. We only start
     * a new one after the prior returned, so this join is instant. */
    if(app->worker) {
        furi_thread_join(app->worker);
        furi_thread_free(app->worker);
    }
    app->worker = furi_thread_alloc_ex("MoonApiWorker", 2048, http_worker, app);
    furi_thread_start(app->worker);

    view_port_update(app->view_port);
}

static const char* state_label(MoonConnectionState s) {
    switch(s) {
    case MoonConnStateDisconnected: return "Disconnected";
    case MoonConnStateScanning:     return "Scanning";
    case MoonConnStateConnecting:   return "Connecting";
    case MoonConnStateConnected:    return "Connected";
    case MoonConnStateYielded:      return "Yielded";
    }
    return "?";
}

static void draw_callback(Canvas* canvas, void* ctx) {
    App* app = ctx;
    canvas_clear(canvas);

    MoonConnectionState state = moon_companion_get_state(app->moon);

    /* Snapshot under lock so the canvas draws a consistent frame. */
    size_t sel;
    bool in_flight, have_resp, failed;
    uint32_t status;
    char body[BODY_PREVIEW_MAX];
    size_t body_len;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    sel = app->selected;
    in_flight = app->in_flight;
    have_resp = app->have_response;
    failed = app->last_failed;
    status = app->last_status;
    body_len = app->body_len;
    memcpy(body, app->body_preview, sizeof(body));
    furi_mutex_release(app->mutex);

    /* Header */
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Moon API");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 66, 10, state_label(state));
    canvas_draw_line(canvas, 0, 12, 128, 12);

    /* Endpoint list (3 rows) */
    for(size_t i = 0; i < NUM_ENDPOINTS; i++) {
        int y = 22 + (int)i * 9;
        if(i == sel) canvas_draw_str(canvas, 2, y, ">");
        canvas_draw_str(canvas, 10, y, ENDPOINTS[i]);
    }

    /* Divider + status area */
    canvas_draw_line(canvas, 0, 49, 128, 49);

    if(in_flight) {
        canvas_draw_str(canvas, 2, 58, "Fetching...");
        return;
    }

    if(!have_resp) {
        if(state != MoonConnStateConnected) {
            canvas_draw_str(canvas, 2, 58, "Pair phone first");
        } else {
            canvas_draw_str(canvas, 2, 58, "OK=fetch  Back=exit");
        }
        return;
    }

    if(failed) {
        canvas_draw_str(canvas, 2, 58, "Failed (not paired?)");
        return;
    }

    /* Show status code + body preview across two lines. At FontSecondary
     * ~21 chars fit per line; truncate hard. */
    char line[32];
    snprintf(line, sizeof(line), "HTTP %lu", (unsigned long)status);
    canvas_draw_str(canvas, 2, 58, line);

    if(body_len > 0) {
        /* Body preview — right-align up to ~18 chars on the status line. */
        size_t avail = sizeof(line) - 1;
        size_t n = body_len < 18 ? body_len : 18;
        size_t start = 0;
        if(n > avail) n = avail;
        memcpy(line, body + start, n);
        line[n] = '\0';
        canvas_draw_str(canvas, 46, 58, line);
    }
}

int32_t moon_api_app(void* p) {
    UNUSED(p);

    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(*app));

    app->queue = furi_message_queue_alloc(8, sizeof(Ev));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, draw_callback, app);
    view_port_input_callback_set(app->view_port, on_input, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    app->moon = furi_record_open(RECORD_MOON_COMPANION);

    Ev e;
    bool running = true;
    while(running) {
        if(furi_message_queue_get(app->queue, &e, FuriWaitForever) != FuriStatusOk) continue;

        if(e.kind == EvHttpDone) {
            /* Worker thread posted its done notice. Draw already fired
             * via view_port_update from the worker. Nothing else to do. */
            continue;
        }

        /* EvKey */
        if(e.input.type != InputTypeShort) continue;
        switch(e.input.key) {
        case InputKeyBack:
            running = false;
            break;
        case InputKeyUp:
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            if(app->selected > 0) app->selected--;
            furi_mutex_release(app->mutex);
            view_port_update(app->view_port);
            break;
        case InputKeyDown:
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            if(app->selected < NUM_ENDPOINTS - 1) app->selected++;
            furi_mutex_release(app->mutex);
            view_port_update(app->view_port);
            break;
        case InputKeyOk:
            start_fetch(app);
            break;
        default:
            break;
        }
    }

    /* If we exit while a request is in flight, wait for it — the worker
     * holds a reference to `app` and would otherwise race the free. */
    if(app->worker) {
        furi_thread_join(app->worker);
        furi_thread_free(app->worker);
    }

    furi_record_close(RECORD_MOON_COMPANION);
    gui_remove_view_port(app->gui, app->view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(app->view_port);
    furi_message_queue_free(app->queue);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
