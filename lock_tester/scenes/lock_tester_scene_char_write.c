#include "../lock_tester_app_i.h"

typedef enum {
    CharWritePhaseDiscovering,
    CharWritePhaseSelectChar,
    CharWritePhaseInput,
    CharWritePhaseWriting,
} CharWritePhase;

static CharWritePhase write_phase;

enum {
    CharWriteSubmenuBase = 0,
};

static void char_write_submenu_callback(void* context, uint32_t index) {
    LockTesterApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void char_write_byte_input_callback(void* context) {
    LockTesterApp* app = context;
    view_dispatcher_send_custom_event(
        app->view_dispatcher, LockTesterCustomEventWriteComplete + 100); // unique event
}

void lock_tester_scene_char_write_on_enter(void* context) {
    LockTesterApp* app = context;

    write_phase = CharWritePhaseDiscovering;
    app->char_count = 0;
    app->byte_store_len = 6; // default 6 bytes

    view_dispatcher_switch_to_view(app->view_dispatcher, LockTesterViewLoading);

    // Discover characteristics of selected service
    if(app->selected_service_idx < app->service_count) {
        ble_gatt_client_discover_characteristics(
            app->connection_handle, &app->services[app->selected_service_idx]);
    } else {
        scene_manager_previous_scene(app->scene_manager);
    }
}

bool lock_tester_scene_char_write_on_event(void* context, SceneManagerEvent event) {
    LockTesterApp* app = context;
    bool consumed = false;

    if(event.type != SceneManagerEventTypeCustom) return false;

    switch(write_phase) {
    case CharWritePhaseDiscovering:
        if(event.event == LockTesterCustomEventCharsDiscovered) {
            if(app->char_count == 0) {
                popup_reset(app->popup);
                popup_set_header(app->popup, "No characteristics", 64, 26, AlignCenter, AlignCenter);
                popup_set_text(app->popup, "found in this service", 64, 40, AlignCenter, AlignCenter);
                view_dispatcher_switch_to_view(app->view_dispatcher, LockTesterViewPopup);
                write_phase = CharWritePhaseSelectChar; // allow back
                consumed = true;
                break;
            }

            // Show list of characteristics
            write_phase = CharWritePhaseSelectChar;
            Submenu* submenu = app->submenu;
            submenu_reset(submenu);
            submenu_set_header(submenu, "Select Characteristic");

            for(uint8_t i = 0; i < app->char_count; i++) {
                char label[40];
                const char* props = "";
                uint8_t p = app->chars[i].properties;
                if((p & 0x08) && (p & 0x10))
                    props = " [W+N]";
                else if(p & 0x08)
                    props = " [W]";
                else if(p & 0x04)
                    props = " [WNR]";
                else if(p & 0x02)
                    props = " [R]";
                else if(p & 0x10)
                    props = " [N]";

                if(app->chars[i].uuid_type == 1) {
                    snprintf(label, sizeof(label), "0x%04X%s",
                        app->chars[i].uuid_16, props);
                } else {
                    const uint8_t* u = app->chars[i].uuid_128;
                    snprintf(label, sizeof(label), "%02X%02X..%s",
                        u[15], u[14], props);
                }
                submenu_add_item(submenu, label, i,
                    char_write_submenu_callback, app);
            }

            view_dispatcher_switch_to_view(app->view_dispatcher, LockTesterViewSubmenu);
            consumed = true;
        } else if(event.event == LockTesterCustomEventGattError) {
            scene_manager_previous_scene(app->scene_manager);
            consumed = true;
        }
        break;

    case CharWritePhaseSelectChar:
        if(event.event < app->char_count) {
            app->write_char_handle = app->chars[event.event].value_handle;
            write_phase = CharWritePhaseInput;

            // Show byte input
            memset(app->byte_store, 0, sizeof(app->byte_store));
            byte_input_set_header_text(app->byte_input, "Enter bytes to write");
            byte_input_set_result_callback(
                app->byte_input, char_write_byte_input_callback,
                NULL, app, app->byte_store, app->byte_store_len);
            view_dispatcher_switch_to_view(app->view_dispatcher, LockTesterViewByteInput);
            consumed = true;
        }
        break;

    case CharWritePhaseInput:
        if(event.event == LockTesterCustomEventWriteComplete + 100) {
            // User confirmed byte input — send write
            write_phase = CharWritePhaseWriting;
            view_dispatcher_switch_to_view(app->view_dispatcher, LockTesterViewLoading);

            ble_gatt_client_write(
                app->connection_handle,
                app->write_char_handle,
                app->byte_store,
                app->byte_store_len);
            consumed = true;
        }
        break;

    case CharWritePhaseWriting:
        if(event.event == LockTesterCustomEventWriteComplete) {
            popup_reset(app->popup);
            popup_set_header(app->popup, "Write OK", 64, 26, AlignCenter, AlignCenter);
            popup_set_timeout(app->popup, 1500);
            popup_enable_timeout(app->popup);
            view_dispatcher_switch_to_view(app->view_dispatcher, LockTesterViewPopup);
            notification_message(app->notifications, &sequence_success);
            consumed = true;
        } else if(event.event == LockTesterCustomEventGattError) {
            popup_reset(app->popup);
            popup_set_header(app->popup, "Write Failed", 64, 26, AlignCenter, AlignCenter);
            popup_set_timeout(app->popup, 1500);
            popup_enable_timeout(app->popup);
            view_dispatcher_switch_to_view(app->view_dispatcher, LockTesterViewPopup);
            notification_message(app->notifications, &sequence_error);
            consumed = true;
        } else if(event.event == LockTesterCustomEventNotification) {
            // Got response after write
            popup_reset(app->popup);
            FuriString* hex = furi_string_alloc();
            furi_string_printf(hex, "Write OK\nRX: ");
            for(uint16_t i = 0; i < app->notify_len && i < 12; i++) {
                furi_string_cat_printf(hex, "%02X ", app->notify_buf[i]);
            }
            popup_set_header(app->popup, "Response!", 64, 10, AlignCenter, AlignCenter);
            popup_set_text(app->popup, furi_string_get_cstr(hex), 64, 32, AlignCenter, AlignCenter);
            view_dispatcher_switch_to_view(app->view_dispatcher, LockTesterViewPopup);
            notification_message(app->notifications, &sequence_blink_green_10);
            furi_string_free(hex);
            consumed = true;
        }
        break;
    }

    return consumed;
}

void lock_tester_scene_char_write_on_exit(void* context) {
    LockTesterApp* app = context;
    submenu_reset(app->submenu);
    popup_reset(app->popup);
}
