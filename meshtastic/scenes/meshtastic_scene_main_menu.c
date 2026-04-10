#include "../meshtastic_app_i.h"

enum {
    MeshtasticMainSendMessage,
    MeshtasticMainMessages,
    MeshtasticMainNodes,
    MeshtasticMainChannels,
    MeshtasticMainDeviceInfo,
    MeshtasticMainDisconnect = 0xFF,
};

static void meshtastic_scene_main_menu_callback(void* context, uint32_t index) {
    MeshtasticApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void meshtastic_scene_main_menu_on_enter(void* context) {
    MeshtasticApp* app = context;
    submenu_reset(app->submenu);

    char header[48];
    if(app->my_long_name[0]) {
        snprintf(header, sizeof(header), "Mesh: %s", app->my_long_name);
    } else {
        snprintf(header, sizeof(header), "Mesh: %d nodes", app->node_count);
    }
    submenu_set_header(app->submenu, header);

    submenu_add_item(
        app->submenu, "Send Message", MeshtasticMainSendMessage,
        meshtastic_scene_main_menu_callback, app);
    submenu_add_item(
        app->submenu, "Messages", MeshtasticMainMessages,
        meshtastic_scene_main_menu_callback, app);
    submenu_add_item(
        app->submenu, "Nodes", MeshtasticMainNodes,
        meshtastic_scene_main_menu_callback, app);
    submenu_add_item(
        app->submenu, "Channels", MeshtasticMainChannels,
        meshtastic_scene_main_menu_callback, app);
    submenu_add_item(
        app->submenu, "Device Info", MeshtasticMainDeviceInfo,
        meshtastic_scene_main_menu_callback, app);
    submenu_add_item(
        app->submenu, "Disconnect", MeshtasticMainDisconnect,
        meshtastic_scene_main_menu_callback, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, MeshtasticViewSubmenu);

    // Start polling for incoming messages while on menu
    furi_timer_start(app->timer, 2000);
}

bool meshtastic_scene_main_menu_on_event(void* context, SceneManagerEvent event) {
    MeshtasticApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case MeshtasticMainSendMessage:
            furi_timer_stop(app->timer);
            scene_manager_next_scene(app->scene_manager, MeshtasticSceneSendMessage);
            return true;
        case MeshtasticMainMessages:
            furi_timer_stop(app->timer);
            scene_manager_next_scene(app->scene_manager, MeshtasticSceneMessages);
            return true;
        case MeshtasticMainNodes:
            furi_timer_stop(app->timer);
            scene_manager_next_scene(app->scene_manager, MeshtasticSceneNodes);
            return true;
        case MeshtasticMainChannels:
            furi_timer_stop(app->timer);
            scene_manager_next_scene(app->scene_manager, MeshtasticSceneChannels);
            return true;
        case MeshtasticMainDeviceInfo:
            furi_timer_stop(app->timer);
            scene_manager_next_scene(app->scene_manager, MeshtasticSceneDeviceInfo);
            return true;
        case MeshtasticMainDisconnect:
            furi_timer_stop(app->timer);
            gap_disconnect(app->connection_handle);
            app->state = MeshStateIdle;
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, MeshtasticSceneStart);
            return true;
        case (uint32_t)MeshtasticCustomEventTimerTick:
            // Poll for new messages in background
            if(app->char_handles.fromradio_handle && app->state == MeshStateReady) {
                ble_gatt_client_read(
                    app->connection_handle, app->char_handles.fromradio_handle);
            }
            return true;
        case (uint32_t)MeshtasticCustomEventFromRadioReady: {
            uint8_t* buf = NULL;
            uint16_t buf_len = 0;
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            if(app->has_read_data && app->read_len > 0) {
                buf_len = app->read_len;
                buf = malloc(buf_len);
                if(buf) memcpy(buf, app->read_buf, buf_len);
                app->has_read_data = false;
            }
            uint8_t old_msg_count = app->message_count;
            furi_mutex_release(app->mutex);
            if(buf) {
                meshtastic_process_from_radio(app, buf, buf_len);
                free(buf);
            }
            if(app->message_count > old_msg_count) {
                notification_message(app->notifications, &sequence_blink_green_10);
            }
            return true;
        }
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        furi_timer_stop(app->timer);
        gap_disconnect(app->connection_handle);
        app->state = MeshStateIdle;
        scene_manager_search_and_switch_to_previous_scene(
            app->scene_manager, MeshtasticSceneStart);
        return true;
    }
    return false;
}

void meshtastic_scene_main_menu_on_exit(void* context) {
    MeshtasticApp* app = context;
    furi_timer_stop(app->timer);
    submenu_reset(app->submenu);
}
