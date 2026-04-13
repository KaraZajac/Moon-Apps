#include "../coros_auditor_app_i.h"

enum { CorosStartScan, CorosStartAbout };

static void coros_start_cb(void* ctx, uint32_t idx) {
    view_dispatcher_send_custom_event(((CorosAuditorApp*)ctx)->view_dispatcher, idx);
}

void coros_scene_start_on_enter(void* context) {
    CorosAuditorApp* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "COROS Auditor");
    submenu_add_item(app->submenu, "Scan for Watches", CorosStartScan, coros_start_cb, app);
    submenu_add_item(app->submenu, "About", CorosStartAbout, coros_start_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, CorosViewSubmenu);
}

bool coros_scene_start_on_event(void* context, SceneManagerEvent event) {
    CorosAuditorApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event == CorosStartScan) {
        scene_manager_next_scene(app->scene_manager, CorosSceneScan);
        return true;
    } else if(event.event == CorosStartAbout) {
        widget_reset(app->widget);
        widget_add_text_scroll_element(app->widget, 0, 0, 128, 64,
            "COROS Auditor v0.1\n\n"
            "Tests COROS watches for\n"
            "8 CVEs (2025-32875\n"
            "through 2025-48706):\n\n"
            "- No BLE authentication\n"
            "- Unencrypted data\n"
            "- Device info exposure\n"
            "- Command injection\n"
            "- Fake notifications\n\n"
            "Affected: All COROS\n"
            "PACE, VERTIX, APEX,\n"
            "NOMAD, DURA + Decathlon\n"
            "GPS 500/900\n\n"
            "SySS GmbH research\n"
            "(Moritz Abrell, 2025)\n\n"
            "@KaraZajac");
        view_dispatcher_switch_to_view(app->view_dispatcher, CorosViewWidget);
        return true;
    }
    return false;
}

void coros_scene_start_on_exit(void* context) {
    CorosAuditorApp* app = context;
    submenu_reset(app->submenu);
    widget_reset(app->widget);
}
