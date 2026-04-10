#include "../bitchat_app_i.h"

static void nickname_input_cb(void* context) {
    BitchatApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, 0);
}

void bitchat_scene_nickname_on_enter(void* context) {
    BitchatApp* app = context;

    strncpy(app->input_buf, app->nickname, BC_MAX_NICKNAME);
    app->input_buf[BC_MAX_NICKNAME] = '\0';

    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, "Enter Nickname");
    text_input_set_result_callback(
        app->text_input, nickname_input_cb, app, app->input_buf, BC_MAX_NICKNAME, true);

    view_dispatcher_switch_to_view(app->view_dispatcher, BitchatViewTextInput);
}

bool bitchat_scene_nickname_on_event(void* context, SceneManagerEvent event) {
    BitchatApp* app = context;
    if(event.type == SceneManagerEventTypeCustom) {
        if(app->input_buf[0] != '\0') {
            strncpy(app->nickname, app->input_buf, BC_MAX_NICKNAME);
            app->nickname[BC_MAX_NICKNAME] = '\0';
        }
        scene_manager_previous_scene(app->scene_manager);
        return true;
    }
    return false;
}

void bitchat_scene_nickname_on_exit(void* context) {
    BitchatApp* app = context;
    text_input_reset(app->text_input);
}
