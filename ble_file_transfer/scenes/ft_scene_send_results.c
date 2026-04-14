#include "../ft_app_i.h"

#define CONNECT_TIMEOUT_POLLS 100

static void ft_results_cb(void* ctx, uint32_t idx) {
    view_dispatcher_send_custom_event(((FtApp*)ctx)->view_dispatcher, idx);
}

void ft_scene_send_results_on_enter(void* context) {
    FtApp* app = context;

    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Select Flipper");

    static char labels[FT_MAX_SCAN_DEVICES][48];
    for(uint8_t i = 0; i < app->scan_device_count; i++) {
        FtScanDevice* dev = &app->scan_devices[i];
        if(dev->has_name) {
            snprintf(labels[i], sizeof(labels[i]), "%s (%ddBm)", dev->name, dev->rssi);
        } else {
            snprintf(labels[i], sizeof(labels[i]),
                "%02X:%02X:%02X:%02X:%02X:%02X (%ddBm)",
                dev->address[5], dev->address[4], dev->address[3],
                dev->address[2], dev->address[1], dev->address[0],
                dev->rssi);
        }
        submenu_add_item(app->submenu, labels[i], i, ft_results_cb, app);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, FtViewSubmenu);
}

bool ft_scene_send_results_on_event(void* context, SceneManagerEvent event) {
    FtApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event < app->scan_device_count) {
        FtScanDevice* dev = &app->scan_devices[event.event];
        memcpy(app->target_addr, dev->address, 6);
        app->target_addr_type = dev->address_type;

        // Connect to selected Flipper
        popup_reset(app->popup);
        popup_set_header(app->popup, "Connecting...", 64, 10, AlignCenter, AlignTop);
        popup_set_text(app->popup,
            dev->has_name ? dev->name : "Flipper",
            64, 36, AlignCenter, AlignCenter);
        view_dispatcher_switch_to_view(app->view_dispatcher, FtViewPopup);

        app->send_connected = false;
        app->tick_count = 0;

        if(!gap_connect(app->target_addr_type, app->target_addr)) {
            popup_set_text(app->popup, "Connect failed", 64, 36, AlignCenter, AlignCenter);
            return true;
        }

        furi_timer_start(app->timer, 100);
        return true;
    }

    if(event.event == FtCustomEventTick) {
        app->tick_count++;

        if(!app->send_connected && gap_get_state() == GapStateConnected) {
            app->send_connected = true;
            app->send_handle = gap_get_connection_handle_by_role(true);
            if(app->send_handle == 0) {
                app->send_handle = gap_get_connection_handle();
            }
            furi_timer_stop(app->timer);

            // Request 2M PHY for faster transfer
            gap_set_phy_preference(app->send_handle, GapPhy2M, GapPhy2M);

            popup_set_text(app->popup, "Connected!\nOpening channel...", 64, 36, AlignCenter, AlignCenter);

            // Open L2CAP CoC channel
            ble_l2cap_coc_connect(app->send_handle, FT_SPSM, FT_MTU, FT_MPS, FT_CREDITS);
        }

        if(app->tick_count >= CONNECT_TIMEOUT_POLLS && !app->send_connected) {
            furi_timer_stop(app->timer);
            popup_set_text(app->popup, "Connection timeout", 64, 36, AlignCenter, AlignCenter);
        }

        return true;
    }

    if(event.event == FtCustomEventCocConnected) {
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        bool send_ready = app->send_coc_connected;
        furi_mutex_release(app->mutex);

        if(send_ready) {
            app->transfer_complete = false;
            app->transfer_error = false;
            app->bytes_transferred = 0;
            scene_manager_next_scene(app->scene_manager, FtSceneTransfer);
        }
        return true;
    }

    if(event.event == FtCustomEventCocError) {
        popup_set_text(app->popup, "Channel open failed", 64, 36, AlignCenter, AlignCenter);
        return true;
    }

    return false;
}

void ft_scene_send_results_on_exit(void* context) {
    FtApp* app = context;
    furi_timer_stop(app->timer);
    submenu_reset(app->submenu);
    popup_reset(app->popup);
}
