#include "../ft_app_i.h"

static const uint8_t ft_svc_uuid[] = FT_SVC_UUID_128;

void ft_scene_receive_on_enter(void* context) {
    FtApp* app = context;

    app->transfer_complete = false;
    app->transfer_error = false;
    app->bytes_transferred = 0;
    app->coc_connected = false;

    // Start extended advertising with file transfer UUID
    // so senders can discover us
    gap_ext_adv_configure(
        FT_ADV_HANDLE,
        GAP_EXT_ADV_PROP_CONNECTABLE | GAP_EXT_ADV_PROP_SCANNABLE,
        0x0080, // 80ms min
        0x00A0, // 100ms max
        0x01,   // 1M PHY
        0x01);  // SID

    // Build AD data with our service UUID
    uint8_t adv_data[20];
    adv_data[0] = 17; // length: 1 + 16
    adv_data[1] = 0x07; // AD type: complete 128-bit UUID list
    memcpy(&adv_data[2], ft_svc_uuid, 16);
    gap_ext_adv_set_data(FT_ADV_HANDLE, adv_data, 18);

    gap_ext_adv_start(FT_ADV_HANDLE, 0); // indefinite

    popup_reset(app->popup);
    popup_set_header(app->popup, "Waiting...", 64, 10, AlignCenter, AlignTop);
    popup_set_text(app->popup, "Ready to receive.\nOther Flipper can\nsend a file now.", 64, 36, AlignCenter, AlignCenter);
    view_dispatcher_switch_to_view(app->view_dispatcher, FtViewPopup);

    FURI_LOG_I(TAG, "Receiver ready, advertising");
}

bool ft_scene_receive_on_event(void* context, SceneManagerEvent event) {
    FtApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == FtCustomEventCocConnected) {
        // Sender connected and opened CoC channel
        // Accept the incoming connection
        ble_l2cap_coc_accept(
            app->connection_handle, FT_MTU, FT_MPS, FT_CREDITS, 0x0000);

        popup_set_header(app->popup, "Connected!", 64, 10, AlignCenter, AlignTop);
        popup_set_text(app->popup, "Receiving file...", 64, 36, AlignCenter, AlignCenter);

        // Stop advertising
        gap_ext_adv_stop(FT_ADV_HANDLE);

        // Go to transfer scene
        scene_manager_next_scene(app->scene_manager, FtSceneTransfer);
        return true;
    }
    return false;
}

void ft_scene_receive_on_exit(void* context) {
    FtApp* app = context;
    gap_ext_adv_stop(FT_ADV_HANDLE);
    popup_reset(app->popup);
}
