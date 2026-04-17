#include "../ble_connect_app_i.h"

void ble_connect_scene_pairing_pin_on_enter(void* context) {
    BleConnectApp* app = context;

    Widget* widget = app->widget;
    widget_reset(widget);

    widget_add_string_element(widget, 64, 8, AlignCenter, AlignCenter, FontPrimary, "Pairing PIN");

    snprintf(app->text_store, sizeof(app->text_store), "%06lu", app->pairing_pin);
    widget_add_string_element(
        widget, 64, 32, AlignCenter, AlignCenter, FontBigNumbers, app->text_store);

    widget_add_string_element(
        widget, 64, 54, AlignCenter, AlignCenter, FontSecondary, "Enter this PIN on the device");

    view_dispatcher_switch_to_view(app->view_dispatcher, BleConnectViewWidget);
}

bool ble_connect_scene_pairing_pin_on_event(void* context, SceneManagerEvent event) {
    BleConnectApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == BleConnectCustomEventPairingComplete) {
            // Pairing succeeded — continue to connected state
            app->connected = true;
            app->connection_handle = gap_get_connection_handle();
            ble_gatt_client_set_callback(
                app->connection_handle, ble_connect_gatt_callback, app);
            ble_gatt_client_discover_services(app->connection_handle);
            scene_manager_next_scene(app->scene_manager, BleConnectSceneServices);
            consumed = true;
        } else if(event.event == BleConnectCustomEventPairingFailed) {
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, BleConnectSceneDeviceInfo);
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeTick) {
        // Check if connection completed while waiting
        GapState state = gap_get_state();
        if(state == GapStateConnected && !app->connected) {
            app->connected = true;
            app->connection_handle = gap_get_connection_handle();
            ble_gatt_client_set_callback(
                app->connection_handle, ble_connect_gatt_callback, app);
            ble_gatt_client_discover_services(app->connection_handle);
            scene_manager_next_scene(app->scene_manager, BleConnectSceneServices);
            consumed = true;
        } else if(state == GapStateIdle) {
            // Connection lost during pairing
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, BleConnectSceneDeviceInfo);
            consumed = true;
        }
    }
    return consumed;
}

void ble_connect_scene_pairing_pin_on_exit(void* context) {
    BleConnectApp* app = context;
    widget_reset(app->widget);
}
