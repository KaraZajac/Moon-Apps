#include "../meshtastic_app_i.h"

#define MSG_SEND_IDX 0xFE

static void build_message_list(MeshtasticApp* app) {
    furi_string_reset(app->text_box_store);

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    if(app->message_count == 0) {
        furi_string_cat_str(app->text_box_store, "No messages yet.\nSend one to get started!");
    } else {
        for(uint8_t i = 0; i < app->message_count; i++) {
            MeshMessage* m = &app->messages[i];
            const char* sender = meshtastic_node_name(app, m->from_node);
            bool is_me = (m->from_node == app->my_node_num);

            if(is_me) {
                furi_string_cat_printf(app->text_box_store, "\e#> %s\n", m->text);
            } else {
                furi_string_cat_printf(app->text_box_store, "\e#%s:\n%s\n", sender, m->text);
            }
        }
    }
    furi_mutex_release(app->mutex);
}

void meshtastic_scene_messages_on_enter(void* context) {
    MeshtasticApp* app = context;

    build_message_list(app);
    text_box_reset(app->text_box);
    text_box_set_text(app->text_box, furi_string_get_cstr(app->text_box_store));
    text_box_set_focus(app->text_box, TextBoxFocusEnd);

    view_dispatcher_switch_to_view(app->view_dispatcher, MeshtasticViewTextBox);

    // Start polling for new messages
    furi_timer_start(app->timer, 500);
}

bool meshtastic_scene_messages_on_event(void* context, SceneManagerEvent event) {
    MeshtasticApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == MeshtasticCustomEventFromRadioReady) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            if(app->has_read_data && app->read_len > 0) {
                uint8_t old_count = app->message_count;
                meshtastic_process_from_radio(app, app->read_buf, app->read_len);
                app->has_read_data = false;
                if(app->message_count > old_count) {
                    // New message — refresh view
                    furi_mutex_release(app->mutex);
                    build_message_list(app);
                    text_box_reset(app->text_box);
                    text_box_set_text(app->text_box, furi_string_get_cstr(app->text_box_store));
                    text_box_set_focus(app->text_box, TextBoxFocusEnd);
                    notification_message(app->notifications, &sequence_blink_cyan_10);
                    return true;
                }
            }
            furi_mutex_release(app->mutex);
            return true;
        } else if(event.event == MeshtasticCustomEventTimerTick) {
            // Poll for new data periodically
            if(app->char_handles.fromradio_handle) {
                ble_gatt_client_read(app->connection_handle, app->char_handles.fromradio_handle);
            }
            return true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        furi_timer_stop(app->timer);
        // Long press back = send message, short = go back
        scene_manager_previous_scene(app->scene_manager);
        return true;
    }
    return false;
}

void meshtastic_scene_messages_on_exit(void* context) {
    MeshtasticApp* app = context;
    furi_timer_stop(app->timer);
    text_box_reset(app->text_box);
}
