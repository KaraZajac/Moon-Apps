#include "../gatt_fuzzer_app_i.h"

enum { FuzzStartScan, FuzzStartAbout };

static void fuzz_start_cb(void* ctx, uint32_t idx) {
    view_dispatcher_send_custom_event(((GattFuzzerApp*)ctx)->view_dispatcher, idx);
}

void fuzz_scene_start_on_enter(void* context) {
    GattFuzzerApp* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "GATT Fuzzer");
    submenu_add_item(app->submenu, "Scan for Devices", FuzzStartScan, fuzz_start_cb, app);
    submenu_add_item(app->submenu, "About", FuzzStartAbout, fuzz_start_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, FuzzViewSubmenu);
}

bool fuzz_scene_start_on_event(void* context, SceneManagerEvent event) {
    GattFuzzerApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event == FuzzStartScan) {
        scene_manager_next_scene(app->scene_manager, FuzzSceneScan);
        return true;
    } else if(event.event == FuzzStartAbout) {
        widget_reset(app->widget);
        widget_add_text_scroll_element(
            app->widget, 0, 0, 128, 64,
            "GATT Fuzzer v0.1\n\n"
            "BLE GATT protocol fuzzer\n"
            "for security testing.\n\n"
            "Tests:\n"
            "- Write all chars\n"
            "- Oversize writes\n"
            "- Invalid handles\n"
            "- Rapid subscribe\n"
            "- MTU fuzzing\n"
            "- Handle enumeration\n\n"
            "Inspired by Quarkslab\n"
            "BLE GATT Fuzzing\n"
            "(Oct 2024)\n\n"
            "For security research\n"
            "on devices you own.\n\n"
            "@KaraZajac");
        view_dispatcher_switch_to_view(app->view_dispatcher, FuzzViewWidget);
        return true;
    }
    return false;
}

void fuzz_scene_start_on_exit(void* context) {
    GattFuzzerApp* app = context;
    submenu_reset(app->submenu);
    widget_reset(app->widget);
}
