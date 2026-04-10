#include "../lock_tester_app_i.h"

enum {
    ConnectedMenuRunTests = 0,
    ConnectedMenuManualWrite,
    ConnectedMenuDisconnect,
    // Service indices start at 0x10
    ConnectedMenuServiceBase = 0x10,
};

static void lock_tester_scene_connected_callback(void* context, uint32_t index) {
    LockTesterApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void lock_tester_scene_connected_on_enter(void* context) {
    LockTesterApp* app = context;
    Submenu* submenu = app->submenu;

    submenu_reset(submenu);

    const LockProfile* profile = app->current_device.profile;
    if(profile) {
        submenu_set_header(submenu, profile->name);
        submenu_add_item(submenu, "Run Default Tests",
            ConnectedMenuRunTests, lock_tester_scene_connected_callback, app);
    } else {
        submenu_set_header(submenu, "Connected");
    }

    submenu_add_item(submenu, "Manual Write",
        ConnectedMenuManualWrite, lock_tester_scene_connected_callback, app);

    // List discovered services
    for(uint8_t i = 0; i < app->service_count && i < 16; i++) {
        char label[40];
        if(app->services[i].uuid_type == 1) {
            snprintf(label, sizeof(label), "Svc 0x%04X", app->services[i].uuid_16);
        } else {
            const uint8_t* u = app->services[i].uuid_128;
            snprintf(label, sizeof(label), "Svc %02X%02X%02X%02X...",
                u[15], u[14], u[13], u[12]);
        }
        submenu_add_item(submenu, label,
            ConnectedMenuServiceBase + i, lock_tester_scene_connected_callback, app);
    }

    submenu_add_item(submenu, "Disconnect",
        ConnectedMenuDisconnect, lock_tester_scene_connected_callback, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, LockTesterViewSubmenu);
}

bool lock_tester_scene_connected_on_event(void* context, SceneManagerEvent event) {
    LockTesterApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == ConnectedMenuRunTests) {
            app->active_profile = app->current_device.profile;
            scene_manager_next_scene(app->scene_manager, LockTesterSceneTestRun);
            consumed = true;
        } else if(event.event == ConnectedMenuManualWrite) {
            // First discover chars of first service to get a writable handle
            if(app->service_count > 0) {
                app->selected_service_idx = 0;
                // Find first service with non-GAP/GATT UUID (skip 0x1800, 0x1801)
                for(uint8_t i = 0; i < app->service_count; i++) {
                    if(app->services[i].uuid_type == 1 &&
                       app->services[i].uuid_16 != 0x1800 &&
                       app->services[i].uuid_16 != 0x1801) {
                        app->selected_service_idx = i;
                        break;
                    }
                }
            }
            scene_manager_next_scene(app->scene_manager, LockTesterSceneCharWrite);
            consumed = true;
        } else if(event.event == ConnectedMenuDisconnect) {
            if(app->connected) {
                gap_disconnect(app->connection_handle);
                app->connected = false;
            }
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, LockTesterSceneStart);
            consumed = true;
        } else if(event.event >= ConnectedMenuServiceBase &&
                   event.event < (uint32_t)(ConnectedMenuServiceBase + app->service_count)) {
            // Service selected — discover chars and go to manual write
            app->selected_service_idx = event.event - ConnectedMenuServiceBase;
            scene_manager_next_scene(app->scene_manager, LockTesterSceneCharWrite);
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        if(app->connected) {
            gap_disconnect(app->connection_handle);
            app->connected = false;
        }
        scene_manager_search_and_switch_to_previous_scene(
            app->scene_manager, LockTesterSceneStart);
        consumed = true;
    }
    return consumed;
}

void lock_tester_scene_connected_on_exit(void* context) {
    LockTesterApp* app = context;
    submenu_reset(app->submenu);
}
