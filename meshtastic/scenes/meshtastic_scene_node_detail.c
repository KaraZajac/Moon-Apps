#include "../meshtastic_app_i.h"

void meshtastic_scene_node_detail_on_enter(void* context) {
    MeshtasticApp* app = context;
    Widget* widget = app->widget;
    widget_reset(widget);

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    MeshNode* node = &app->nodes[app->selected_node_idx];

    FuriString* info = furi_string_alloc();
    furi_string_printf(
        info,
        "\e#%s\n"
        "Short: %s\n"
        "ID: !%08lx\n"
        "Battery: %d%%\n"
        "SNR: %d dB\n",
        node->long_name[0] ? node->long_name : "Unknown",
        node->short_name[0] ? node->short_name : "?",
        node->node_num,
        node->battery_level,
        node->snr);

    if(node->has_position) {
        int32_t lat_i = node->latitude / 100;
        int32_t lon_i = node->longitude / 100;
        furi_string_cat_printf(
            info, "Lat: %ld.%05ld\nLon: %ld.%05ld",
            (long)(lat_i / 100000), (long)(lat_i >= 0 ? lat_i % 100000 : (-lat_i) % 100000),
            (long)(lon_i / 100000), (long)(lon_i >= 0 ? lon_i % 100000 : (-lon_i) % 100000));
    }
    furi_mutex_release(app->mutex);

    widget_add_text_scroll_element(widget, 0, 0, 128, 64, furi_string_get_cstr(info));
    furi_string_free(info);

    view_dispatcher_switch_to_view(app->view_dispatcher, MeshtasticViewWidget);
}

bool meshtastic_scene_node_detail_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void meshtastic_scene_node_detail_on_exit(void* context) {
    MeshtasticApp* app = context;
    widget_reset(app->widget);
}
