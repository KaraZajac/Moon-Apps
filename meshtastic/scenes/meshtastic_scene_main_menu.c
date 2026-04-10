#include "../meshtastic_app_i.h"

enum {
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
    snprintf(header, sizeof(header), "Mesh: %d nodes", app->node_count);
    submenu_set_header(app->submenu, header);

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
}

bool meshtastic_scene_main_menu_on_event(void* context, SceneManagerEvent event) {
    MeshtasticApp* app = context;

    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case MeshtasticMainMessages:
            scene_manager_next_scene(app->scene_manager, MeshtasticSceneMessages);
            return true;
        case MeshtasticMainNodes:
            scene_manager_next_scene(app->scene_manager, MeshtasticSceneNodes);
            return true;
        case MeshtasticMainChannels:
            scene_manager_next_scene(app->scene_manager, MeshtasticSceneChannels);
            return true;
        case MeshtasticMainDeviceInfo:
            scene_manager_next_scene(app->scene_manager, MeshtasticSceneDeviceInfo);
            return true;
        case MeshtasticMainDisconnect:
            gap_disconnect(app->connection_handle);
            app->state = MeshStateIdle;
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, MeshtasticSceneStart);
            return true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        // Back = disconnect
        gap_disconnect(app->connection_handle);
        app->state = MeshStateIdle;
        scene_manager_search_and_switch_to_previous_scene(
            app->scene_manager, MeshtasticSceneStart);
        return true;
    }

    // Handle incoming messages while on menu
    if(event.type == SceneManagerEventTypeCustom &&
       event.event == MeshtasticCustomEventFromRadioReady) {
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        if(app->has_read_data && app->read_len > 0) {
            meshtastic_process_from_radio(app, app->read_buf, app->read_len);
            app->has_read_data = false;
        }
        furi_mutex_release(app->mutex);
        return true;
    }

    return false;
}

void meshtastic_scene_main_menu_on_exit(void* context) {
    MeshtasticApp* app = context;
    submenu_reset(app->submenu);
}
