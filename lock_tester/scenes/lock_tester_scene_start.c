#include "../lock_tester_app_i.h"

enum {
    LockTesterStartScan,
    LockTesterStartAbout,
};

static void lock_tester_scene_start_submenu_callback(void* context, uint32_t index) {
    LockTesterApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void lock_tester_scene_start_on_enter(void* context) {
    LockTesterApp* app = context;
    Submenu* submenu = app->submenu;

    submenu_reset(submenu);
    submenu_set_header(submenu, "BLE Lock Tester");
    submenu_add_item(
        submenu, "Scan for Locks",
        LockTesterStartScan, lock_tester_scene_start_submenu_callback, app);
    submenu_add_item(
        submenu, "About",
        LockTesterStartAbout, lock_tester_scene_start_submenu_callback, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, LockTesterViewSubmenu);
}

bool lock_tester_scene_start_on_event(void* context, SceneManagerEvent event) {
    LockTesterApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == LockTesterStartScan) {
            scene_manager_next_scene(app->scene_manager, LockTesterSceneScan);
            consumed = true;
        } else if(event.event == LockTesterStartAbout) {
            widget_reset(app->widget);
            widget_add_text_scroll_element(
                app->widget, 0, 0, 128, 64,
                "BLE Lock Tester\n"
                "\n"
                "Tests BLE smart locks\n"
                "for known vulnerabilities:\n"
                "- Default PINs\n"
                "- Known commands\n"
                "- Cleartext passwords\n"
                "\n"
                "Supported locks:\n"
                "- Generic (FEE7)\n"
                "- QuickLock\n"
                "- OKLOK\n"
                "- Nokelock\n"
                "- Tapplock\n"
                "\n"
                "For authorized security\n"
                "testing only.\n"
                "\n"
                "v0.1 @KaraZajac");
            view_dispatcher_switch_to_view(app->view_dispatcher, LockTesterViewWidget);
            consumed = true;
        }
    }
    return consumed;
}

void lock_tester_scene_start_on_exit(void* context) {
    LockTesterApp* app = context;
    submenu_reset(app->submenu);
    widget_reset(app->widget);
}
