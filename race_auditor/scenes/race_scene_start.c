#include "../race_auditor_app_i.h"

enum { RaceStartScan, RaceStartAbout };

static void race_start_cb(void* ctx, uint32_t idx) {
    view_dispatcher_send_custom_event(((RaceAuditorApp*)ctx)->view_dispatcher, idx);
}

void race_scene_start_on_enter(void* context) {
    RaceAuditorApp* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "RACE Auditor");
    submenu_add_item(app->submenu, "Scan for Devices", RaceStartScan, race_start_cb, app);
    submenu_add_item(app->submenu, "About", RaceStartAbout, race_start_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, RaceViewSubmenu);
}

bool race_scene_start_on_event(void* context, SceneManagerEvent event) {
    RaceAuditorApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == RaceStartScan) {
        scene_manager_next_scene(app->scene_manager, RaceSceneScan);
        return true;
    } else if(event.event == RaceStartAbout) {
        widget_reset(app->widget);
        widget_add_text_scroll_element(
            app->widget, 0, 0, 128, 64,
            "RACE Auditor v0.1\n\n"
            "Tests Airoha-based BLE\n"
            "audio devices for\n"
            "CVE-2025-20700/01/02\n\n"
            "Affected: Sony, JBL,\n"
            "Bose, Marshall, Jabra,\n"
            "Beyerdynamic & more\n\n"
            "RACE debug protocol is\n"
            "exposed over GATT\n"
            "without authentication.\n\n"
            "For security research\n"
            "on devices you own.\n\n"
            "@KaraZajac");
        view_dispatcher_switch_to_view(app->view_dispatcher, RaceViewWidget);
        return true;
    }
    return false;
}

void race_scene_start_on_exit(void* context) {
    RaceAuditorApp* app = context;
    submenu_reset(app->submenu);
    widget_reset(app->widget);
}
