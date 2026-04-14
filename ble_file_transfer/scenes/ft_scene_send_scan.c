#include "../ft_app_i.h"

#define SCAN_TIMEOUT_MS 10000

static const uint8_t ft_svc_uuid[] = FT_SVC_UUID_128;

static bool parse_adv_name(const uint8_t* data, uint8_t len, char* name, size_t sz) {
    uint8_t pos = 0;
    while(pos < len) {
        uint8_t l = data[pos];
        if(l == 0 || pos + l >= len) break;
        if(data[pos + 1] == 0x08 || data[pos + 1] == 0x09) {
            uint8_t nl = l - 1;
            if(nl >= sz) nl = sz - 1;
            memcpy(name, &data[pos + 2], nl);
            name[nl] = '\0';
            return true;
        }
        pos += l + 1;
    }
    return false;
}

static bool has_ft_uuid(const uint8_t* data, uint8_t len) {
    uint8_t pos = 0;
    while(pos < len) {
        uint8_t l = data[pos];
        if(l == 0 || pos + l >= len) break;
        uint8_t type = data[pos + 1];
        if((type == 0x06 || type == 0x07) && l >= 17) {
            for(uint8_t i = 0; i + 15 < l - 1; i += 16) {
                if(memcmp(&data[pos + 2 + i], ft_svc_uuid, 16) == 0) {
                    return true;
                }
            }
        }
        pos += l + 1;
    }
    return false;
}

static void ft_scan_callback(GapScanResultData* result, void* context) {
    FtApp* app = context;
    if(!result->data || result->data_len == 0) return;

    // Only show devices advertising our FT service UUID
    if(!has_ft_uuid(result->data, result->data_len)) return;

    furi_mutex_acquire(app->mutex, FuriWaitForever);

    // Update existing or add new
    for(uint8_t i = 0; i < app->scan_device_count; i++) {
        if(memcmp(app->scan_devices[i].address, result->address, 6) == 0) {
            app->scan_devices[i].rssi = result->rssi;
            if(!app->scan_devices[i].has_name) {
                app->scan_devices[i].has_name = parse_adv_name(
                    result->data, result->data_len,
                    app->scan_devices[i].name, sizeof(app->scan_devices[i].name));
            }
            furi_mutex_release(app->mutex);
            return;
        }
    }

    if(app->scan_device_count < FT_MAX_SCAN_DEVICES) {
        FtScanDevice* dev = &app->scan_devices[app->scan_device_count];
        memcpy(dev->address, result->address, 6);
        dev->address_type = result->address_type;
        dev->rssi = result->rssi;
        dev->has_name = parse_adv_name(
            result->data, result->data_len, dev->name, sizeof(dev->name));
        app->scan_device_count++;
    }

    furi_mutex_release(app->mutex);
}

void ft_scene_send_scan_on_enter(void* context) {
    FtApp* app = context;

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->scan_device_count = 0;
    app->scanning = true;
    furi_mutex_release(app->mutex);

    popup_reset(app->popup);
    popup_set_header(app->popup, "Scanning...", 64, 10, AlignCenter, AlignTop);
    char info[128];
    snprintf(info, sizeof(info), "Looking for Flippers\nFile: %.32s\nSize: %lu bytes",
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
    if(!gap_start_scanning(&params)) {
        app->scanning = false;
        popup_set_text(app->popup, "Scan failed", 64, 38, AlignCenter, AlignCenter);
    }

    furi_timer_start(app->timer, 200);
}

bool ft_scene_send_scan_on_event(void* context, SceneManagerEvent event) {
    FtApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == FtCustomEventTick) {
        if(app->scanning && gap_get_state() != GapStateScanning) {
            app->scanning = false;
            furi_timer_stop(app->timer);
            gap_set_scan_callback(NULL, NULL);

            if(app->scan_device_count > 0) {
                scene_manager_next_scene(app->scene_manager, FtSceneSendResults);
            } else {
                popup_set_text(app->popup, "No Flippers found.\nMake sure another\nFlipper has BLE\nFile Transfer open.", 64, 38, AlignCenter, AlignCenter);
            }
        }
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
