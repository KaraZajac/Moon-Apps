#include "../tracker_detector_app_i.h"

static void format_mac(const uint8_t* addr, char* buf, size_t buf_size) {
    snprintf(
        buf,
        buf_size,
        "%02X:%02X:%02X:%02X:%02X:%02X",
        addr[5],
        addr[4],
        addr[3],
        addr[2],
        addr[1],
        addr[0]);
}

static void format_duration(uint32_t ticks, char* buf, size_t buf_size) {
    uint32_t seconds = ticks / furi_kernel_get_tick_frequency();
    if(seconds < 60) {
        snprintf(buf, buf_size, "%lus", (unsigned long)seconds);
    } else {
        uint32_t minutes = seconds / 60;
        uint32_t secs = seconds % 60;
        snprintf(buf, buf_size, "%lum %lus", (unsigned long)minutes, (unsigned long)secs);
    }
}

static void tracker_detector_scene_detail_widget_callback(
    GuiButtonType result,
    InputType type,
    void* context) {
    TrackerDetectorApp* app = context;
    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->view_dispatcher, result);
    }
}

void tracker_detector_scene_detail_on_enter(void* context) {
    TrackerDetectorApp* app = context;
    Widget* widget = app->widget;

    widget_reset(widget);

    furi_mutex_acquire(app->mutex, FuriWaitForever);

    if(app->selected_tracker_idx >= app->tracker_count) {
        widget_add_string_element(
            widget, 64, 32, AlignCenter, AlignCenter, FontSecondary, "Tracker not found");
        furi_mutex_release(app->mutex);
        view_dispatcher_switch_to_view(app->view_dispatcher, TrackerDetectorViewWidget);
        return;
    }

    TrackerDevice* dev = &app->trackers[app->selected_tracker_idx];

    // Build scrollable info text
    char mac_str[18];
    format_mac(dev->address, mac_str, sizeof(mac_str));

    uint32_t now = furi_get_tick();
    char first_str[16];
    format_duration(now - dev->first_seen, first_str, sizeof(first_str));
    char last_str[16];
    format_duration(now - dev->last_seen, last_str, sizeof(last_str));

    FuriString* info = furi_string_alloc();
    furi_string_printf(
        info,
        "\e#%s\n"
        "%s%s"
        "MAC: %s\n"
        "RSSI: %d dBm (%d/%d)\n"
        "First: %s ago\n"
        "Last: %s ago\n"
        "Hits: %lu",
        tracker_type_get_name(dev->type),
        dev->following ? "!! FOLLOWING !!\n" : "",
        (dev->has_name && dev->name[0]) ? dev->name : "",
        mac_str,
        dev->rssi,
        dev->rssi_min,
        dev->rssi_max,
        first_str,
        last_str,
        (unsigned long)dev->hit_count);

    // Add name on its own line if present
    if(dev->has_name && dev->name[0]) {
        // Already included inline above
    }

    TrackerType type = dev->type;
    furi_mutex_release(app->mutex);

    // Scrollable text area (leave room for button at bottom)
    widget_add_text_scroll_element(widget, 0, 0, 128, 49, furi_string_get_cstr(info));
    furi_string_free(info);

    // Play Sound button if we have a profile for this tracker type
    const TrackerSoundProfile* profile = tracker_sound_get_profile(type);
    if(profile) {
        widget_add_button_element(
            widget,
            GuiButtonTypeCenter,
            "Play Sound",
            tracker_detector_scene_detail_widget_callback,
            app);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, TrackerDetectorViewWidget);
}

bool tracker_detector_scene_detail_on_event(void* context, SceneManagerEvent event) {
    TrackerDetectorApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == GuiButtonTypeCenter) {
            scene_manager_next_scene(app->scene_manager, TrackerDetectorScenePlaySound);
            consumed = true;
        }
    }
    return consumed;
}

void tracker_detector_scene_detail_on_exit(void* context) {
    TrackerDetectorApp* app = context;
    widget_reset(app->widget);
}
