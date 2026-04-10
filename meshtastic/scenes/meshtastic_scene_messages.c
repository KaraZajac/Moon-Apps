#include "../meshtastic_app_i.h"

#define MSG_SEND_NEW 0xFD
#define MSG_REFRESH  0xFE

static void meshtastic_scene_messages_callback(void* context, uint32_t index) {
    MeshtasticApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void build_message_view(MeshtasticApp* app) {
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    submenu_set_header(submenu, "Messages");

    submenu_add_item(
        submenu, "[Send Message]", MSG_SEND_NEW,
        meshtastic_scene_messages_callback, app);

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    // Show messages in reverse order (newest first)
    for(int i = (int)app->message_count - 1; i >= 0; i--) {
        MeshMessage* m = &app->messages[i];
        char label[64];
        bool is_me = (m->from_node == app->my_node_num);

        if(is_me) {
            snprintf(label, sizeof(label), "> %s", m->text);
        } else {
            const char* sender = meshtastic_node_name(app, m->from_node);
            snprintf(label, sizeof(label), "%s: %s", sender, m->text);
        }
        // Use index i as event ID, won't collide with 0xFD/0xFE
        submenu_add_item(submenu, label, (uint32_t)i, NULL, NULL);
    }
    furi_mutex_release(app->mutex);

    if(app->message_count == 0) {
        submenu_add_item(submenu, "No messages yet", 0xFF, NULL, NULL);
    }

    submenu_add_item(
        submenu, "[Refresh]", MSG_REFRESH,
        meshtastic_scene_messages_callback, app);
}

void meshtastic_scene_messages_on_enter(void* context) {
    MeshtasticApp* app = context;
    build_message_view(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, MeshtasticViewSubmenu);

    // Poll for new messages
    furi_timer_start(app->timer, 1000);
}

bool meshtastic_scene_messages_on_event(void* context, SceneManagerEvent event) {
    MeshtasticApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == MSG_SEND_NEW) {
            furi_timer_stop(app->timer);
            scene_manager_next_scene(app->scene_manager, MeshtasticSceneSendMessage);
            return true;
        } else if(event.event == MSG_REFRESH) {
            // Force poll
            if(app->char_handles.fromradio_handle && app->state == MeshStateReady) {
                ble_gatt_client_read(
                    app->connection_handle, app->char_handles.fromradio_handle);
            }
            return true;
        } else if(event.event == MeshtasticCustomEventFromRadioReady) {
            uint8_t* buf = NULL;
            uint16_t buf_len = 0;
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            if(app->has_read_data && app->read_len > 0) {
                buf_len = app->read_len;
                buf = malloc(buf_len);
                if(buf) memcpy(buf, app->read_buf, buf_len);
                app->has_read_data = false;
            }
            uint8_t old_count = app->message_count;
            furi_mutex_release(app->mutex);
            if(buf) {
                meshtastic_process_from_radio(app, buf, buf_len);
                free(buf);
            }
            if(app->message_count > old_count) {
                build_message_view(app);
                notification_message(app->notifications, &sequence_blink_cyan_10);
            }
            return true;
        } else if(event.event == MeshtasticCustomEventTimerTick) {
            // Periodic poll
            if(app->char_handles.fromradio_handle && app->state == MeshStateReady) {
                ble_gatt_client_read(
                    app->connection_handle, app->char_handles.fromradio_handle);
            }
            return true;
        }
    }
    return false;
}

void meshtastic_scene_messages_on_exit(void* context) {
    MeshtasticApp* app = context;
    furi_timer_stop(app->timer);
    submenu_reset(app->submenu);
}
