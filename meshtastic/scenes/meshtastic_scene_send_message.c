#include "../meshtastic_app_i.h"

static void meshtastic_scene_send_message_callback(void* context) {
    MeshtasticApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, 0);
}

void meshtastic_scene_send_message_on_enter(void* context) {
    MeshtasticApp* app = context;

    app->text_store[0] = '\0';
    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, "Send Message:");
    text_input_set_result_callback(
        app->text_input,
        meshtastic_scene_send_message_callback,
        app,
        app->text_store,
        MESH_TEXT_STORE_SIZE,
        true);

    view_dispatcher_switch_to_view(app->view_dispatcher, MeshtasticViewTextInput);
}

bool meshtastic_scene_send_message_on_event(void* context, SceneManagerEvent event) {
    MeshtasticApp* app = context;

    if(event.type == SceneManagerEventTypeCustom && event.event == 0) {
        if(app->text_store[0]) {
            // Send broadcast message on channel 0
            meshtastic_send_text_message(app, app->text_store, 0xFFFFFFFF, 0);

            // Add to local message list
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            if(app->message_count < MESH_MAX_MESSAGES) {
                MeshMessage* m = &app->messages[app->message_count++];
                m->from_node = app->my_node_num;
                m->to_node = 0xFFFFFFFF;
                strlcpy(m->text, app->text_store, MESH_MSG_TEXT_LEN);
                m->timestamp = 0;
                m->channel_idx = 0;
            }
            furi_mutex_release(app->mutex);
        }
        scene_manager_previous_scene(app->scene_manager);
        return true;
    }
    return false;
}

void meshtastic_scene_send_message_on_exit(void* context) {
    MeshtasticApp* app = context;
    text_input_reset(app->text_input);
}
