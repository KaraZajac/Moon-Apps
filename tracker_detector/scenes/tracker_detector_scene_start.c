#include "../tracker_detector_app_i.h"

enum {
    TrackerDetectorStartSubmenuScan,
    TrackerDetectorStartSubmenuAbout,
};

static void tracker_detector_scene_start_submenu_callback(void* context, uint32_t index) {
    TrackerDetectorApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void tracker_detector_scene_start_on_enter(void* context) {
    TrackerDetectorApp* app = context;
    Submenu* submenu = app->submenu;

    submenu_reset(submenu);
    submenu_set_header(submenu, "Tracker Detector");
    submenu_add_item(
        submenu,
        "Start Scanning",
        TrackerDetectorStartSubmenuScan,
        tracker_detector_scene_start_submenu_callback,
        app);
    submenu_add_item(
        submenu,
        "About",
        TrackerDetectorStartSubmenuAbout,
        tracker_detector_scene_start_submenu_callback,
        app);

    view_dispatcher_switch_to_view(app->view_dispatcher, TrackerDetectorViewSubmenu);
}

bool tracker_detector_scene_start_on_event(void* context, SceneManagerEvent event) {
    TrackerDetectorApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == TrackerDetectorStartSubmenuScan) {
            scene_manager_next_scene(app->scene_manager, TrackerDetectorSceneScan);
            consumed = true;
        } else if(event.event == TrackerDetectorStartSubmenuAbout) {
            widget_reset(app->widget);
            widget_add_text_scroll_element(
                app->widget,
                0,
                0,
                128,
                64,
                "Tracker Detector\n"
                "\n"
                "Detects nearby BLE\n"
                "trackers including:\n"
                "- Apple AirTag/FindMy\n"
                "- Samsung SmartTag\n"
                "- Tile\n"
                "- Chipolo\n"
                "- Google FMDN\n"
                "\n"
                "Alerts with vibration\n"
                "if a tracker may be\n"
                "following you (seen\n"
                "for 10+ minutes).\n"
                "\n"
                "v0.1 @KaraZajac");
            view_dispatcher_switch_to_view(app->view_dispatcher, TrackerDetectorViewWidget);
            consumed = true;
        }
    }
    return consumed;
}

void tracker_detector_scene_start_on_exit(void* context) {
    TrackerDetectorApp* app = context;
    submenu_reset(app->submenu);
    widget_reset(app->widget);
}
