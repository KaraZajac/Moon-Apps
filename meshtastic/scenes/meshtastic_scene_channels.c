#include "../meshtastic_app_i.h"

void meshtastic_scene_channels_on_enter(void* context) {
    MeshtasticApp* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Channels");

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    for(uint8_t i = 0; i < app->channel_count; i++) {
        MeshChannel* ch = &app->channels[i];
        char label[40];
        const char* role_str = ch->role == 1 ? "Primary" : (ch->role == 2 ? "Secondary" : "Disabled");
        if(ch->name[0]) {
            snprintf(label, sizeof(label), "%d: %s (%s)", ch->index, ch->name, role_str);
        } else {
            snprintf(label, sizeof(label), "%d: [default] (%s)", ch->index, role_str);
        }
        submenu_add_item(app->submenu, label, i, NULL, NULL);
    }
    furi_mutex_release(app->mutex);

    if(app->channel_count == 0) {
        submenu_add_item(app->submenu, "No channels", 0, NULL, NULL);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, MeshtasticViewSubmenu);
}

bool meshtastic_scene_channels_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void meshtastic_scene_channels_on_exit(void* context) {
    MeshtasticApp* app = context;
    submenu_reset(app->submenu);
}
