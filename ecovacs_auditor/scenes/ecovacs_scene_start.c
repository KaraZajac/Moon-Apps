#include "../ecovacs_app_i.h"

enum { EcoStartScan, EcoStartAbout };

static void eco_start_cb(void* ctx, uint32_t idx) {
    view_dispatcher_send_custom_event(((EcovacsAuditorApp*)ctx)->view_dispatcher, idx);
}

void ecovacs_scene_start_on_enter(void* context) {
    EcovacsAuditorApp* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Ecovacs Auditor");
    submenu_add_item(app->submenu, "Scan for Robots", EcoStartScan, eco_start_cb, app);
    submenu_add_item(app->submenu, "About", EcoStartAbout, eco_start_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, EcovacsViewSubmenu);
}

bool ecovacs_scene_start_on_event(void* context, SceneManagerEvent event) {
    EcovacsAuditorApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == EcoStartScan) {
        scene_manager_next_scene(app->scene_manager, EcovacsSceneScan);
        return true;
    } else if(event.event == EcoStartAbout) {
        widget_reset(app->widget);
        widget_add_text_scroll_element(
            app->widget, 0, 0, 128, 64,
            "Ecovacs Auditor v0.1\n\n"
            "Tests Ecovacs robots\n"
            "for CVE-2024-12078:\n"
            "hard-coded static AES\n"
            "key on BLE interface.\n\n"
            "Affected: DEEBOT X1/X2,\n"
            "T10/T20, N8/N10, GOAT\n"
            "G1, AIRBOT Z1, Yeedi\n\n"
            "Checks:\n"
            "1. No BLE auth required\n"
            "2. Service 0x8888 exposed\n"
            "3. Static key accepted\n\n"
            "DEF CON 32 (2024)\n"
            "For security research\n"
            "on devices you own.\n\n"
            "@KaraZajac");
        view_dispatcher_switch_to_view(app->view_dispatcher, EcovacsViewWidget);
        return true;
    }
    return false;
}

void ecovacs_scene_start_on_exit(void* context) {
    EcovacsAuditorApp* app = context;
    submenu_reset(app->submenu);
    widget_reset(app->widget);
}
