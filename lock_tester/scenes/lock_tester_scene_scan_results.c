#include "../lock_tester_app_i.h"

#define RESCAN_IDX   0xFE
#define SHOW_ALL_IDX 0xFD

static bool show_all = false;

static void lock_tester_scene_scan_results_callback(void* context, uint32_t index) {
    LockTesterApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void format_mac(const uint8_t* addr, char* buf, size_t buf_size) {
    snprintf(buf, buf_size, "%02X:%02X:%02X:%02X:%02X:%02X",
        addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
}

static void build_results_list(LockTesterApp* app) {
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);

    furi_mutex_acquire(app->mutex, FuriWaitForever);

    uint8_t lock_count = 0;

    if(app->scan_device_count == 0) {
        submenu_set_header(submenu, "No devices found");
    } else {
        // Count locks
        for(uint8_t i = 0; i < app->scan_device_count; i++) {
            if(app->scan_devices[i].profile) lock_count++;
        }

        if(show_all) {
            submenu_set_header(submenu, "All Devices");
        } else {
            char header[32];
            snprintf(header, sizeof(header), "Locks Found: %d", lock_count);
            submenu_set_header(submenu, header);
        }

        for(uint8_t i = 0; i < app->scan_device_count; i++) {
            LockTesterDevice* dev = &app->scan_devices[i];

            // Filter: only show locks unless "show all" is enabled
            if(!show_all && !dev->profile) continue;

            char label[52];
            if(dev->profile) {
                snprintf(label, sizeof(label), "[LOCK] %s %ddBm",
                    dev->has_name ? dev->name : dev->profile->name, dev->rssi);
            } else if(dev->has_name && dev->name[0]) {
                snprintf(label, sizeof(label), "%s %ddBm", dev->name, dev->rssi);
            } else {
                char mac[18];
                format_mac(dev->address, mac, sizeof(mac));
                snprintf(label, sizeof(label), "%s %ddBm", mac, dev->rssi);
            }

            submenu_add_item(submenu, label, i,
                lock_tester_scene_scan_results_callback, app);
        }
    }

    furi_mutex_release(app->mutex);

    if(!show_all) {
        submenu_add_item(submenu, "[Show All Devices]", SHOW_ALL_IDX,
            lock_tester_scene_scan_results_callback, app);
    }
    submenu_add_item(submenu, "[Rescan]", RESCAN_IDX,
        lock_tester_scene_scan_results_callback, app);
}

void lock_tester_scene_scan_results_on_enter(void* context) {
    LockTesterApp* app = context;
    show_all = false;
    build_results_list(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, LockTesterViewSubmenu);
}

bool lock_tester_scene_scan_results_on_event(void* context, SceneManagerEvent event) {
    LockTesterApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == RESCAN_IDX) {
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, LockTesterSceneStart);
            scene_manager_next_scene(app->scene_manager, LockTesterSceneScan);
            consumed = true;
        } else if(event.event == SHOW_ALL_IDX) {
            show_all = true;
            build_results_list(app);
            view_dispatcher_switch_to_view(app->view_dispatcher, LockTesterViewSubmenu);
            consumed = true;
        } else if(event.event < app->scan_device_count) {
            app->selected_device_idx = event.event;
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            memcpy(&app->current_device, &app->scan_devices[event.event],
                   sizeof(LockTesterDevice));
            furi_mutex_release(app->mutex);
            scene_manager_next_scene(app->scene_manager, LockTesterSceneConnecting);
            consumed = true;
        }
    }
    return consumed;
}

void lock_tester_scene_scan_results_on_exit(void* context) {
    LockTesterApp* app = context;
    submenu_reset(app->submenu);
}
