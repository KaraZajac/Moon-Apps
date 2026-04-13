#include "../gatt_fuzzer_app_i.h"

#define FUZZ_MENU_RUN_ALL (FuzzTestNum)

static void fuzz_menu_cb(void* ctx, uint32_t idx) {
    view_dispatcher_send_custom_event(((GattFuzzerApp*)ctx)->view_dispatcher, idx);
}

void fuzz_scene_test_menu_on_enter(void* context) {
    GattFuzzerApp* app = context;
    submenu_reset(app->submenu);

    char header[48];
    snprintf(header, sizeof(header), "Fuzz (%d chars)", app->all_char_total);
    submenu_set_header(app->submenu, header);

    submenu_add_item(app->submenu, ">>> Run All Tests <<<", FUZZ_MENU_RUN_ALL, fuzz_menu_cb, app);
    for(uint8_t i = 0; i < FuzzTestNum; i++) {
        submenu_add_item(app->submenu, fuzz_test_info[i].name, i, fuzz_menu_cb, app);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, FuzzViewSubmenu);
}

bool fuzz_scene_test_menu_on_event(void* context, SceneManagerEvent event) {
    GattFuzzerApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == FUZZ_MENU_RUN_ALL) {
        app->current_test = 0; // Will run all sequentially
        scene_manager_next_scene(app->scene_manager, FuzzSceneRunTest);
        return true;
    } else if(event.event < FuzzTestNum) {
        app->current_test = event.event;
        scene_manager_next_scene(app->scene_manager, FuzzSceneRunTest);
        return true;
    }
    return false;
}

void fuzz_scene_test_menu_on_exit(void* context) {
    GattFuzzerApp* app = context;
    submenu_reset(app->submenu);
}
