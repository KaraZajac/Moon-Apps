#include "../bitchat_app_i.h"

enum { StartJoinChat, StartSetNickname, StartAbout };

static void start_cb(void* ctx, uint32_t idx) {
    view_dispatcher_send_custom_event(((BitchatApp*)ctx)->view_dispatcher, idx);
}

void bitchat_scene_start_on_enter(void* context) {
    BitchatApp* app = context;
    submenu_reset(app->submenu);

    char header[40];
    snprintf(header, sizeof(header), "BitChat [%s]", app->nickname);
    submenu_set_header(app->submenu, header);

    submenu_add_item(app->submenu, "Join Chat", StartJoinChat, start_cb, app);
    submenu_add_item(app->submenu, "Set Nickname", StartSetNickname, start_cb, app);
    submenu_add_item(app->submenu, "About", StartAbout, start_cb, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, BitchatViewSubmenu);
}

bool bitchat_scene_start_on_event(void* context, SceneManagerEvent event) {
    BitchatApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == StartJoinChat) {
        scene_manager_next_scene(app->scene_manager, BitchatSceneScan);
        return true;
    } else if(event.event == StartSetNickname) {
        scene_manager_next_scene(app->scene_manager, BitchatSceneNickname);
        return true;
    } else if(event.event == StartAbout) {
        text_box_reset(app->text_box);
        text_box_set_font(app->text_box, TextBoxFontText);
        text_box_set_text(app->text_box,
            "BitChat\n"
            "BLE Mesh Chat\n\n"
            "Decentralized P2P\n"
            "chat over Bluetooth.\n"
            "No internet needed.\n\n"
            "Phase 1: Central mode\n"
            "- Finds BitChat peers\n"
            "- Public broadcast msgs\n"
            "- No encryption yet\n\n"
            "Based on BitChat\n"
            "protocol (unlicense)\n"
            "bitchat.org\n\n"
            "v0.1 @KaraZajac");
        view_dispatcher_switch_to_view(app->view_dispatcher, BitchatViewTextBox);
        return true;
    }
    return false;
}

void bitchat_scene_start_on_exit(void* context) {
    BitchatApp* app = context;
    submenu_reset(app->submenu);
    text_box_reset(app->text_box);
}
