#include "../ft_app_i.h"

enum { FtStartSend, FtStartReceive, FtStartAbout };

static void ft_start_cb(void* ctx, uint32_t idx) {
    view_dispatcher_send_custom_event(((FtApp*)ctx)->view_dispatcher, idx);
}

void ft_scene_start_on_enter(void* context) {
    FtApp* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "BLE File Transfer");
    submenu_add_item(app->submenu, "Send File", FtStartSend, ft_start_cb, app);
    submenu_add_item(app->submenu, "Receive File", FtStartReceive, ft_start_cb, app);
    submenu_add_item(app->submenu, "About", FtStartAbout, ft_start_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, FtViewSubmenu);
}

bool ft_scene_start_on_event(void* context, SceneManagerEvent event) {
    FtApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == FtStartSend) {
        app->mode = FtModeSending;
        scene_manager_next_scene(app->scene_manager, FtSceneSendBrowse);
        return true;
    } else if(event.event == FtStartReceive) {
        app->mode = FtModeReceiving;
        scene_manager_next_scene(app->scene_manager, FtSceneReceive);
        return true;
    } else if(event.event == FtStartAbout) {
        widget_reset(app->widget);
        widget_add_text_scroll_element(
            app->widget, 0, 0, 128, 64,
            "BLE File Transfer\n\n"
            "Send and receive files\n"
            "between Flipper Zeros\n"
            "over Bluetooth.\n\n"
            "Uses L2CAP CoC for\n"
            "high-speed transfer\n"
            "(50-100 KB/s).\n\n"
            "v0.1 @KaraZajac");
        view_dispatcher_switch_to_view(app->view_dispatcher, FtViewWidget);
        return true;
    }
    return false;
}

void ft_scene_start_on_exit(void* context) {
    FtApp* app = context;
    submenu_reset(app->submenu);
    widget_reset(app->widget);
}
