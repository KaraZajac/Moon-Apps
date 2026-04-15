#include "../axon_on_app_i.h"

void axon_on_scene_about_on_enter(void* context) {
    AxonOnApp* app = context;

    widget_reset(app->widget);
    widget_add_text_scroll_element(
        app->widget,
        0,
        0,
        128,
        64,
        "Axon On v2.0\n"
        "\n"
        "Detect and activate\n"
        "Axon body cameras\n"
        "via BLE.\n"
        "\n"
        "Scan: finds cameras\n"
        "by OUI 00:25:DF and\n"
        "service UUID 0xFE6C.\n"
        "\n"
        "OK: toggle broadcast\n"
        "of the Axon recording\n"
        "command packet.\n"
        "\n"
        "@KaraZajac");

    view_dispatcher_switch_to_view(app->view_dispatcher, AxonOnViewWidget);
}

bool axon_on_scene_about_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void axon_on_scene_about_on_exit(void* context) {
    AxonOnApp* app = context;
    widget_reset(app->widget);
}
