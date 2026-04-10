#include "../ble_connect_app_i.h"

enum {
    BleConnectDeviceInfoConnect = GuiButtonTypeRight,
    BleConnectDeviceInfoSave = GuiButtonTypeLeft,
};

static void ble_connect_scene_device_info_widget_callback(
    GuiButtonType result,
    InputType type,
    void* context) {
    BleConnectApp* app = context;
    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->view_dispatcher, result);
    }
}

void ble_connect_scene_device_info_on_enter(void* context) {
    BleConnectApp* app = context;
    BleConnectDevice* dev = &app->current_device;

    Widget* widget = app->widget;
    widget_reset(widget);

    char mac[18];
    ble_connect_device_mac_to_str(dev->address, mac, sizeof(mac));

    FuriString* info = furi_string_alloc();
    furi_string_printf(
        info,
        "\e#%s\n"
        "MAC: %s\n"
        "RSSI: %d dBm\n"
        "Type: %s",
        (dev->has_name && dev->name[0]) ? dev->name : "Unknown",
        mac,
        dev->rssi,
        dev->address_type == 0 ? "Public" : "Random (privacy)");

    widget_add_text_scroll_element(widget, 0, 0, 128, 42, furi_string_get_cstr(info));
    widget_add_button_element(
        widget, GuiButtonTypeLeft, "Save",
        ble_connect_scene_device_info_widget_callback, app);
    widget_add_button_element(
        widget, GuiButtonTypeRight, "Connect",
        ble_connect_scene_device_info_widget_callback, app);

    furi_string_free(info);
    view_dispatcher_switch_to_view(app->view_dispatcher, BleConnectViewWidget);
}

bool ble_connect_scene_device_info_on_event(void* context, SceneManagerEvent event) {
    BleConnectApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == BleConnectDeviceInfoConnect) {
            scene_manager_next_scene(app->scene_manager, BleConnectSceneConnecting);
            consumed = true;
        } else if(event.event == BleConnectDeviceInfoSave) {
            scene_manager_next_scene(app->scene_manager, BleConnectSceneSaveName);
            consumed = true;
        }
    }
    return consumed;
}

void ble_connect_scene_device_info_on_exit(void* context) {
    BleConnectApp* app = context;
    widget_reset(app->widget);
}
