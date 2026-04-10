#include "../tracker_detector_app_i.h"

#define CONNECT_TIMEOUT_POLLS 100  // 100 * 100ms = 10 seconds
#define DONE_DISPLAY_MS       2000

static void play_sound_update_popup(TrackerDetectorApp* app, const char* text) {
    popup_reset(app->popup);
    popup_set_header(app->popup, "Play Sound", 64, 10, AlignCenter, AlignTop);
    popup_set_text(app->popup, text, 64, 36, AlignCenter, AlignCenter);
    view_dispatcher_switch_to_view(app->view_dispatcher, TrackerDetectorViewPopup);
}

void tracker_detector_scene_play_sound_on_enter(void* context) {
    TrackerDetectorApp* app = context;

    app->play_sound_state = PlaySoundStateConnecting;
    app->connected = false;
    app->connect_poll_count = 0;
    app->service_count = 0;
    app->char_count = 0;

    // Stop scanning if active
    if(app->scanning) {
        gap_stop_scanning();
        gap_set_scan_callback(NULL, NULL);
        app->scanning = false;
    }

    // Disconnect existing connection if any
    GapState state = gap_get_state();
    if(state == GapStateConnected) {
        uint16_t handle = gap_get_connection_handle();
        gap_disconnect(handle);
        for(int i = 0; i < 20; i++) {
            furi_delay_ms(50);
            if(gap_get_state() != GapStateConnected) break;
        }
    }

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    if(app->selected_tracker_idx >= app->tracker_count) {
        furi_mutex_release(app->mutex);
        play_sound_update_popup(app, "Tracker not found");
        app->play_sound_state = PlaySoundStateFailed;
        return;
    }

    TrackerDevice* dev = &app->trackers[app->selected_tracker_idx];
    uint8_t addr_type = dev->address_type;
    uint8_t addr[6];
    memcpy(addr, dev->address, 6);
    furi_mutex_release(app->mutex);

    play_sound_update_popup(app, "Connecting...");

    if(!gap_connect(addr_type, addr)) {
        FURI_LOG_E(TAG, "gap_connect failed");
        play_sound_update_popup(app, "Connect failed");
        app->play_sound_state = PlaySoundStateFailed;
        return;
    }

    // Poll connection state every 100ms
    furi_timer_start(app->tick_timer, 100);
}

bool tracker_detector_scene_play_sound_on_event(void* context, SceneManagerEvent event) {
    TrackerDetectorApp* app = context;
    bool consumed = false;

    if(event.type != SceneManagerEventTypeCustom) return false;

    switch(app->play_sound_state) {
    case PlaySoundStateConnecting:
        if(event.event == TrackerDetectorCustomEventTick) {
            app->connect_poll_count++;
            GapState state = gap_get_state();

            if(state == GapStateConnected) {
                furi_timer_stop(app->tick_timer);
                app->connected = true;
                app->connection_handle = gap_get_connection_handle();
                app->play_sound_state = PlaySoundStateDiscoveringServices;

                play_sound_update_popup(app, "Discovering\nservices...");
                ble_gatt_client_discover_services(app->connection_handle);
                consumed = true;
            } else if(app->connect_poll_count >= CONNECT_TIMEOUT_POLLS) {
                furi_timer_stop(app->tick_timer);
                play_sound_update_popup(app, "Connection\ntimeout");
                app->play_sound_state = PlaySoundStateFailed;
                consumed = true;
            }
        }
        break;

    case PlaySoundStateDiscoveringServices:
        if(event.event == TrackerDetectorCustomEventServicesDiscovered) {
            // Find the sound service
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            TrackerType type = app->trackers[app->selected_tracker_idx].type;
            furi_mutex_release(app->mutex);

            const TrackerSoundProfile* profile = tracker_sound_get_profile(type);
            if(!profile) {
                play_sound_update_popup(app, "No sound profile\nfor this tracker");
                app->play_sound_state = PlaySoundStateFailed;
                consumed = true;
                break;
            }

            // Search for matching service
            int8_t svc_idx = -1;
            for(uint8_t i = 0; i < app->service_count; i++) {
                if(tracker_sound_match_service(
                       profile,
                       app->services[i].uuid_type,
                       app->services[i].uuid_16,
                       app->services[i].uuid_128)) {
                    svc_idx = i;
                    break;
                }
            }

            if(svc_idx < 0) {
                play_sound_update_popup(app, "Sound service\nnot found");
                app->play_sound_state = PlaySoundStateFailed;
                consumed = true;
                break;
            }

            app->play_sound_state = PlaySoundStateDiscoveringChars;
            play_sound_update_popup(app, "Finding\ncharacteristic...");
            ble_gatt_client_discover_characteristics(
                app->connection_handle, &app->services[svc_idx]);
            consumed = true;
        } else if(event.event == TrackerDetectorCustomEventGattError) {
            play_sound_update_popup(app, "GATT discovery\nfailed");
            app->play_sound_state = PlaySoundStateFailed;
            consumed = true;
        }
        break;

    case PlaySoundStateDiscoveringChars:
        if(event.event == TrackerDetectorCustomEventCharsDiscovered) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            TrackerType type = app->trackers[app->selected_tracker_idx].type;
            furi_mutex_release(app->mutex);

            const TrackerSoundProfile* profile = tracker_sound_get_profile(type);
            if(!profile) {
                app->play_sound_state = PlaySoundStateFailed;
                consumed = true;
                break;
            }

            // Find matching characteristic
            int8_t char_idx = -1;
            for(uint8_t i = 0; i < app->char_count; i++) {
                if(tracker_sound_match_char(
                       profile,
                       app->chars[i].uuid_type,
                       app->chars[i].uuid_16,
                       app->chars[i].uuid_128,
                       app->chars[i].properties)) {
                    char_idx = i;
                    break;
                }
            }

            if(char_idx < 0) {
                play_sound_update_popup(app, "Sound characteristic\nnot found");
                app->play_sound_state = PlaySoundStateFailed;
                consumed = true;
                break;
            }

            app->play_sound_state = PlaySoundStateWritingCommand;
            play_sound_update_popup(app, "Playing sound...");

            ble_gatt_client_write(
                app->connection_handle,
                app->chars[char_idx].value_handle,
                profile->command,
                profile->command_len);
            consumed = true;
        } else if(event.event == TrackerDetectorCustomEventGattError) {
            play_sound_update_popup(app, "Characteristic\ndiscovery failed");
            app->play_sound_state = PlaySoundStateFailed;
            consumed = true;
        }
        break;

    case PlaySoundStateWritingCommand:
        if(event.event == TrackerDetectorCustomEventWriteComplete) {
            play_sound_update_popup(app, "Sound playing!");
            notification_message(app->notifications, &sequence_success);
            app->play_sound_state = PlaySoundStateDone;
            // Auto-close after delay
            popup_set_timeout(app->popup, DONE_DISPLAY_MS);
            popup_enable_timeout(app->popup);
            consumed = true;
        } else if(event.event == TrackerDetectorCustomEventGattError) {
            play_sound_update_popup(app, "Write failed");
            app->play_sound_state = PlaySoundStateFailed;
            consumed = true;
        }
        break;

    case PlaySoundStateDone:
    case PlaySoundStateFailed:
        // Any event in these states — just go back
        break;
    }

    return consumed;
}

void tracker_detector_scene_play_sound_on_exit(void* context) {
    TrackerDetectorApp* app = context;
    furi_timer_stop(app->tick_timer);

    // Disconnect if we connected
    if(app->connected) {
        gap_disconnect(app->connection_handle);
        app->connected = false;
    }

    popup_reset(app->popup);
}
