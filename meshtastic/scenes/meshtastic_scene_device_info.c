#include "../meshtastic_app_i.h"

void meshtastic_scene_device_info_on_enter(void* context) {
    MeshtasticApp* app = context;
    Widget* widget = app->widget;
    widget_reset(widget);

    FuriString* info = furi_string_alloc();
    furi_string_printf(
        info,
        "\e#Device Info\n"
        "My Node: !%08lx\n"
        "Name: %s\n"
        "Short: %s\n"
        "Nodes: %d\n"
        "Channels: %d\n"
        "Messages: %d",
        app->my_node_num,
        app->my_long_name[0] ? app->my_long_name : "(unknown)",
        app->my_short_name[0] ? app->my_short_name : "?",
        app->node_count,
        app->channel_count,
        app->message_count);

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
