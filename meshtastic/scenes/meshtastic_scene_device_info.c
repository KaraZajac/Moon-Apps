#include "../meshtastic_app_i.h"

void meshtastic_scene_device_info_on_enter(void* context) {
    MeshtasticApp* app = context;
    Widget* widget = app->widget;
    widget_reset(widget);

    furi_mutex_acquire(app->mutex, FuriWaitForever);

    FuriString* info = furi_string_alloc();
    furi_string_cat_str(info, "\e#Device Info\n");
    furi_string_cat_printf(info, "Node: !%08lx\n", app->my_node_num);
    furi_string_cat_printf(
        info, "Name: %s\n", app->my_long_name[0] ? app->my_long_name : "(unknown)");
    furi_string_cat_printf(
        info, "Short: %s\n", app->my_short_name[0] ? app->my_short_name : "?");
    furi_string_cat_printf(info, "Nodes: %d\n", app->node_count);
    furi_string_cat_printf(info, "Channels: %d\n", app->channel_count);
    furi_string_cat_printf(info, "Messages: %d\n", app->message_count);
    furi_string_cat_printf(
        info, "State: %s",
        app->state == MeshStateReady     ? "Connected" :
        app->state == MeshStateConnecting ? "Connecting" :
                                            "Idle");

    furi_mutex_release(app->mutex);

    widget_add_text_scroll_element(widget, 0, 0, 128, 64, furi_string_get_cstr(info));
    furi_string_free(info);

    view_dispatcher_switch_to_view(app->view_dispatcher, MeshtasticViewWidget);
}

bool meshtastic_scene_device_info_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void meshtastic_scene_device_info_on_exit(void* context) {
    MeshtasticApp* app = context;
    widget_reset(app->widget);
}
