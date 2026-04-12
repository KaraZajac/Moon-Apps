#include "../ft_app_i.h"

#define SCAN_TIMEOUT_MS 10000
#define CONNECT_TIMEOUT_POLLS 100

static const uint8_t ft_svc_uuid[] = FT_SVC_UUID_128;
static bool found_receiver = false;

static void ft_scan_callback(GapScanResultData* result, void* context) {
    FtApp* app = context;
    if(!result->data || result->data_len == 0 || found_receiver) return;

    // Look for our file transfer service UUID in the advertisement
    uint8_t pos = 0;
    while(pos < result->data_len) {
        uint8_t len = result->data[pos];
        if(len == 0 || pos + len >= result->data_len) break;
        uint8_t type = result->data[pos + 1];

        // 128-bit UUID list (complete=0x07 or incomplete=0x06)
        if((type == 0x06 || type == 0x07) && len >= 17) {
            for(uint8_t i = 0; i + 15 < len - 1; i += 16) {
                if(memcmp(&result->data[pos + 2 + i], ft_svc_uuid, 16) == 0) {
                    // Found a file transfer receiver!
                    memcpy(app->target_addr, result->address, 6);
                    app->target_addr_type = result->address_type;
                    found_receiver = true;
                    gap_stop_scanning();
                    app->scanning = false;
                    return;
                }
            }
        }
        pos += len + 1;
    }
}

void ft_scene_send_scan_on_enter(void* context) {
    FtApp* app = context;

    found_receiver = false;
    app->connected = false;
    app->coc_connected = false;

    popup_reset(app->popup);
    popup_set_header(app->popup, "Scanning...", 64, 10, AlignCenter, AlignTop);
    char info[128];
    snprintf(info, sizeof(info), "Looking for receiver\nFile: %.32s\nSize: %lu bytes",
        app->filename, (unsigned long)app->file_size);
    popup_set_text(app->popup, info, 64, 38, AlignCenter, AlignCenter);
    view_dispatcher_switch_to_view(app->view_dispatcher, FtViewPopup);

    gap_set_scan_callback(ft_scan_callback, app);
    GapScanParams params = {
        .interval = 0x60,
        .window = 0x30,
        .active = true,
        .timeout_ms = SCAN_TIMEOUT_MS,
    };
    app->scanning = true;
    app->tick_count = 0;
    if(!gap_start_scanning(&params)) {
        app->scanning = false;
        popup_set_text(app->popup, "Scan failed", 64, 38, AlignCenter, AlignCenter);
    }

    furi_timer_start(app->timer, 100);
}

bool ft_scene_send_scan_on_event(void* context, SceneManagerEvent event) {
    FtApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == FtCustomEventTick) {
        app->tick_count++;

        if(found_receiver && !app->connected) {
            // Connect to receiver
            popup_set_text(app->popup, "Receiver found!\nConnecting...", 64, 38, AlignCenter, AlignCenter);
            gap_set_scan_callback(NULL, NULL);

            if(!gap_connect(app->target_addr_type, app->target_addr)) {
                popup_set_text(app->popup, "Connect failed", 64, 38, AlignCenter, AlignCenter);
                return true;
            }
        }

        if(!found_receiver && !app->scanning) {
            // Scan timed out
            furi_timer_stop(app->timer);
            popup_set_text(app->popup, "No receiver found.\nMake sure the other\nFlipper is in\nReceive mode.", 64, 38, AlignCenter, AlignCenter);
            return true;
        }

        // Check connection
        if(found_receiver && gap_get_state() == GapStateConnected && !app->connected) {
            app->connected = true;
            app->connection_handle = gap_get_connection_handle();
            furi_timer_stop(app->timer);

            popup_set_text(app->popup, "Connected!\nOpening channel...", 64, 38, AlignCenter, AlignCenter);

            // Open CoC channel
            ble_l2cap_coc_connect(
                app->connection_handle, FT_SPSM, FT_MTU, FT_MPS, FT_CREDITS);
        }

        if(app->tick_count >= CONNECT_TIMEOUT_POLLS && !app->connected) {
            furi_timer_stop(app->timer);
            popup_set_text(app->popup, "Connection timeout", 64, 38, AlignCenter, AlignCenter);
        }

        return true;
    }

    if(event.event == FtCustomEventCocConnected) {
        // CoC channel open — go to transfer scene
        scene_manager_next_scene(app->scene_manager, FtSceneTransfer);
        return true;
    }

    if(event.event == FtCustomEventCocError) {
        popup_set_text(app->popup, "Channel open failed", 64, 38, AlignCenter, AlignCenter);
        return true;
    }

    return false;
}

void ft_scene_send_scan_on_exit(void* context) {
    FtApp* app = context;
    furi_timer_stop(app->timer);
    if(app->scanning) {
        gap_stop_scanning();
        gap_set_scan_callback(NULL, NULL);
        app->scanning = false;
    }
    popup_reset(app->popup);
}
