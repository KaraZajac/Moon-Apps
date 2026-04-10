#include "../meshtastic_app_i.h"

static void meshtastic_scene_nodes_callback(void* context, uint32_t index) {
    MeshtasticApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void meshtastic_scene_nodes_on_enter(void* context) {
    MeshtasticApp* app = context;
    submenu_reset(app->submenu);

    char header[32];
    snprintf(header, sizeof(header), "Nodes (%d)", app->node_count);
    submenu_set_header(app->submenu, header);

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    for(uint8_t i = 0; i < app->node_count; i++) {
        MeshNode* node = &app->nodes[i];
        char label[48];
        const char* name = node->long_name[0] ? node->long_name :
                          (node->short_name[0] ? node->short_name : "???");
        snprintf(label, sizeof(label), "%s [%d%%]", name, node->battery_level);
        submenu_add_item(app->submenu, label, i, meshtastic_scene_nodes_callback, app);
    }
    furi_mutex_release(app->mutex);

    if(app->node_count == 0) {
        submenu_add_item(app->submenu, "No nodes discovered", 0xFF, NULL, NULL);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, MeshtasticViewSubmenu);
}

bool meshtastic_scene_nodes_on_event(void* context, SceneManagerEvent event) {
    MeshtasticApp* app = context;
    if(event.type == SceneManagerEventTypeCustom && event.event < app->node_count) {
        app->selected_node_idx = event.event;
        scene_manager_next_scene(app->scene_manager, MeshtasticSceneNodeDetail);
        return true;
    }
    return false;
}

void meshtastic_scene_nodes_on_exit(void* context) {
    MeshtasticApp* app = context;
    submenu_reset(app->submenu);
}
