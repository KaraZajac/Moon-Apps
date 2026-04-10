#include "meshtastic_app_i.h"
#include <string.h>

static bool meshtastic_custom_event_callback(void* context, uint32_t event) {
    MeshtasticApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool meshtastic_back_event_callback(void* context) {
    MeshtasticApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

/* BLE scan callback — filter for Meshtastic service UUID in advertising data */
void meshtastic_scan_callback(GapScanResultData* result, void* context) {
    MeshtasticApp* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);

    // Parse name from advertising data
    char name[MESH_DEVICE_NAME_LEN] = "";
    bool has_name = false;
    uint8_t pos = 0;
    while(pos < result->data_len) {
        uint8_t len = result->data[pos];
        if(len == 0 || pos + len >= result->data_len) break;
        uint8_t type = result->data[pos + 1];
        if(type == 0x08 || type == 0x09) {
            uint8_t nl = len - 1;
            if(nl >= sizeof(name)) nl = sizeof(name) - 1;
            memcpy(name, &result->data[pos + 2], nl);
            name[nl] = '\0';
            has_name = true;
        }
        pos += len + 1;
    }

    // Update existing or add new
    for(uint8_t i = 0; i < app->scan_device_count; i++) {
        if(memcmp(app->scan_devices[i].address, result->address, 6) == 0) {
            app->scan_devices[i].rssi = result->rssi;
            if(has_name && !app->scan_devices[i].has_name) {
                strlcpy(app->scan_devices[i].name, name, MESH_DEVICE_NAME_LEN);
                app->scan_devices[i].has_name = true;
            }
            furi_mutex_release(app->mutex);
            return;
        }
    }

    if(app->scan_device_count < MESH_MAX_SCAN_DEVICES) {
        MeshScanDevice* dev = &app->scan_devices[app->scan_device_count];
        memcpy(dev->address, result->address, 6);
        dev->address_type = result->address_type;
        dev->rssi = result->rssi;
        strlcpy(dev->name, name, MESH_DEVICE_NAME_LEN);
        dev->has_name = has_name;
        app->scan_device_count++;
    }

    furi_mutex_release(app->mutex);
}

/* GATT client callback */
static void meshtastic_gatt_callback(BleGattClientEvent* event, void* context) {
    MeshtasticApp* app = context;

    switch(event->type) {
    case BleGattClientEventDiscoverComplete:
        // Find the Meshtastic service
        for(uint8_t i = 0; i < event->discover.count; i++) {
            BleGattService* svc = &event->discover.services[i];
            if(svc->uuid_type == 2 && meshtastic_is_mesh_service(svc->uuid_128)) {
                app->mesh_service_start_handle = svc->start_handle;
                app->mesh_service_end_handle = svc->end_handle;
                // Discover characteristics in this service
                ble_gatt_client_discover_characteristics(app->connection_handle, svc);
                return;
            }
        }
        FURI_LOG_E(TAG, "Meshtastic service not found on device");
        view_dispatcher_send_custom_event(
            app->view_dispatcher, MeshtasticCustomEventGattError);
        break;

    case BleGattClientEventCharDiscoverComplete:
        // Find ToRadio, FromRadio, FromNum handles
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        app->char_handles.all_found = false;
        for(uint8_t i = 0; i < event->char_discover.count; i++) {
            BleGattCharacteristic* chr = &event->char_discover.chars[i];
            if(chr->uuid_type != 2) continue;
            if(memcmp(chr->uuid_128, MESH_TORADIO_UUID, 16) == 0) {
                app->char_handles.toradio_handle = chr->value_handle;
                FURI_LOG_I(TAG, "ToRadio handle: %d", chr->value_handle);
            } else if(memcmp(chr->uuid_128, MESH_FROMRADIO_UUID, 16) == 0) {
                app->char_handles.fromradio_handle = chr->value_handle;
                FURI_LOG_I(TAG, "FromRadio handle: %d", chr->value_handle);
            } else if(memcmp(chr->uuid_128, MESH_FROMNUM_UUID, 16) == 0) {
                app->char_handles.fromnum_handle = chr->value_handle;
                FURI_LOG_I(TAG, "FromNum handle: %d", chr->value_handle);
            }
        }
        if(app->char_handles.toradio_handle && app->char_handles.fromradio_handle &&
           app->char_handles.fromnum_handle) {
            app->char_handles.all_found = true;
        }
        furi_mutex_release(app->mutex);
        view_dispatcher_send_custom_event(
            app->view_dispatcher, MeshtasticCustomEventConnected);
        break;

    case BleGattClientEventReadComplete:
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        app->read_len = (event->read.data_len > MESH_BLE_READ_BUF_SIZE) ?
                            MESH_BLE_READ_BUF_SIZE :
                            event->read.data_len;
        if(app->read_len > 0) {
            memcpy(app->read_buf, event->read.data, app->read_len);
            app->has_read_data = true;
        }
        furi_mutex_release(app->mutex);
        view_dispatcher_send_custom_event(
            app->view_dispatcher, MeshtasticCustomEventFromRadioReady);
        break;

    case BleGattClientEventWriteComplete:
        view_dispatcher_send_custom_event(
            app->view_dispatcher, MeshtasticCustomEventWriteComplete);
        break;

    case BleGattClientEventNotification:
        // FromNum notification — new data available, trigger read
        if(app->char_handles.fromradio_handle) {
            ble_gatt_client_read(app->connection_handle, app->char_handles.fromradio_handle);
        }
        break;

    case BleGattClientEventError:
        FURI_LOG_E(TAG, "GATT error: %d", event->error.error_code);
        view_dispatcher_send_custom_event(
            app->view_dispatcher, MeshtasticCustomEventGattError);
        break;
    }
}

static void meshtastic_timer_callback(void* context) {
    MeshtasticApp* app = context;
    view_dispatcher_send_custom_event(
        app->view_dispatcher, MeshtasticCustomEventTimerTick);
}

/* Process a decoded FromRadio message */
void meshtastic_process_from_radio(MeshtasticApp* app, const uint8_t* data, uint16_t len) {
    if(len == 0) return;

    meshtastic_FromRadio msg = meshtastic_FromRadio_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(data, len);
    if(!pb_decode(&stream, meshtastic_FromRadio_fields, &msg)) {
        FURI_LOG_E(TAG, "Failed to decode FromRadio: %s", PB_GET_ERROR(&stream));
        return;
    }

    furi_mutex_acquire(app->mutex, FuriWaitForever);

    switch(msg.which_payload_variant) {
    case meshtastic_FromRadio_my_info_tag: {
        app->my_node_num = msg.my_info.my_node_num;
        FURI_LOG_I(TAG, "My node num: %lX", app->my_node_num);
        break;
    }
    case meshtastic_FromRadio_node_info_tag: {
        meshtastic_NodeInfo* ni = &msg.node_info;
        MeshNode* node = meshtastic_find_node(app, ni->num);
        if(!node && app->node_count < MESH_MAX_NODES) {
            node = &app->nodes[app->node_count++];
        }
        if(node) {
            node->node_num = ni->num;
            if(ni->has_user) {
                strlcpy(node->short_name, ni->user.short_name, sizeof(node->short_name));
                strlcpy(node->long_name, ni->user.long_name, sizeof(node->long_name));
                // If this is our own node, save our name
                if(ni->num == app->my_node_num) {
                    strlcpy(app->my_short_name, ni->user.short_name, sizeof(app->my_short_name));
                    strlcpy(app->my_long_name, ni->user.long_name, sizeof(app->my_long_name));
                    FURI_LOG_I(TAG, "My name: %s (%s)", app->my_long_name, app->my_short_name);
                }
            }
            node->snr = (int8_t)ni->snr;
            node->last_heard = ni->last_heard;
            if(ni->has_position) {
                node->has_position = true;
                node->latitude = ni->position.latitude_i;
                node->longitude = ni->position.longitude_i;
            }
            if(ni->has_device_metrics) {
                node->battery_level = (uint8_t)ni->device_metrics.battery_level;
            }
        }
        break;
    }
    case meshtastic_FromRadio_channel_tag: {
        meshtastic_Channel* ch = &msg.channel;
        if(ch->index < MESH_MAX_CHANNELS) {
            MeshChannel* mc = &app->channels[ch->index];
            mc->index = ch->index;
            mc->role = ch->role;
            strlcpy(mc->name, ch->settings.name, sizeof(mc->name));
            if(ch->index >= app->channel_count) {
                app->channel_count = ch->index + 1;
            }
        }
        break;
    }
    case meshtastic_FromRadio_config_complete_id_tag: {
        app->config_complete = true;
        FURI_LOG_I(
            TAG,
            "Config complete: %d nodes, %d channels",
            app->node_count,
            app->channel_count);
        break;
    }
    case meshtastic_FromRadio_packet_tag: {
        meshtastic_MeshPacket* pkt = &msg.packet;
        // Handle text messages
        if(pkt->which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
            meshtastic_Data* decoded = &pkt->decoded;
            if(decoded->portnum == meshtastic_PortNum_TEXT_MESSAGE_APP &&
               app->message_count < MESH_MAX_MESSAGES) {
                MeshMessage* m = &app->messages[app->message_count++];
                m->from_node = pkt->from;
                m->to_node = pkt->to;
                m->timestamp = pkt->rx_time;
                m->channel_idx = pkt->channel;
                size_t text_len = decoded->payload.size;
                if(text_len >= MESH_MSG_TEXT_LEN) text_len = MESH_MSG_TEXT_LEN - 1;
                memcpy(m->text, decoded->payload.bytes, text_len);
                m->text[text_len] = '\0';
                FURI_LOG_I(TAG, "Message from %lX: %s", pkt->from, m->text);
            }
            // Handle position
            if(decoded->portnum == meshtastic_PortNum_POSITION_APP) {
                MeshNode* node = meshtastic_find_node(app, pkt->from);
                if(node) {
                    meshtastic_Position pos = meshtastic_Position_init_zero;
                    pb_istream_t pos_stream =
                        pb_istream_from_buffer(decoded->payload.bytes, decoded->payload.size);
                    if(pb_decode(&pos_stream, meshtastic_Position_fields, &pos)) {
                        node->has_position = true;
                        node->latitude = pos.latitude_i;
                        node->longitude = pos.longitude_i;
                    }
                }
            }
            // Handle telemetry
            if(decoded->portnum == meshtastic_PortNum_TELEMETRY_APP) {
                MeshNode* node = meshtastic_find_node(app, pkt->from);
                if(node) {
                    meshtastic_Telemetry tel = meshtastic_Telemetry_init_zero;
                    pb_istream_t tel_stream =
                        pb_istream_from_buffer(decoded->payload.bytes, decoded->payload.size);
                    if(pb_decode(&tel_stream, meshtastic_Telemetry_fields, &tel)) {
                        if(tel.which_variant == meshtastic_Telemetry_device_metrics_tag) {
                            node->battery_level =
                                (uint8_t)tel.variant.device_metrics.battery_level;
                        }
                    }
                }
            }
        }
        break;
    }
    default:
        FURI_LOG_D(TAG, "FromRadio variant: %d", msg.which_payload_variant);
        break;
    }

    furi_mutex_release(app->mutex);
}

bool meshtastic_send_want_config(MeshtasticApp* app) {
    meshtastic_ToRadio msg = meshtastic_ToRadio_init_zero;
    msg.which_payload_variant = meshtastic_ToRadio_want_config_id_tag;
    app->config_request_id = furi_get_tick();
    msg.want_config_id = app->config_request_id;

    uint8_t buf[64];
    pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
    if(!pb_encode(&stream, meshtastic_ToRadio_fields, &msg)) {
        FURI_LOG_E(TAG, "Failed to encode want_config");
        return false;
    }

    return ble_gatt_client_write(
        app->connection_handle, app->char_handles.toradio_handle, buf, stream.bytes_written);
}

bool meshtastic_send_text_message(
    MeshtasticApp* app,
    const char* text,
    uint32_t dest,
    uint8_t channel) {
    meshtastic_ToRadio msg = meshtastic_ToRadio_init_zero;
    msg.which_payload_variant = meshtastic_ToRadio_packet_tag;

    meshtastic_MeshPacket* pkt = &msg.packet;
    pkt->to = dest;
    pkt->want_ack = true;
    pkt->channel = channel;
    pkt->which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    pkt->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;

    size_t text_len = strlen(text);
    if(text_len > sizeof(pkt->decoded.payload.bytes))
        text_len = sizeof(pkt->decoded.payload.bytes);
    memcpy(pkt->decoded.payload.bytes, text, text_len);
    pkt->decoded.payload.size = text_len;

    uint8_t buf[MESH_BLE_READ_BUF_SIZE];
    pb_ostream_t stream = pb_ostream_from_buffer(buf, sizeof(buf));
    if(!pb_encode(&stream, meshtastic_ToRadio_fields, &msg)) {
        FURI_LOG_E(TAG, "Failed to encode text message");
        return false;
    }

    FURI_LOG_I(TAG, "Sending message to %lX: %s (%zu bytes)", dest, text, stream.bytes_written);
    return ble_gatt_client_write(
        app->connection_handle, app->char_handles.toradio_handle, buf, stream.bytes_written);
}

MeshNode* meshtastic_find_node(MeshtasticApp* app, uint32_t node_num) {
    for(uint8_t i = 0; i < app->node_count; i++) {
        if(app->nodes[i].node_num == node_num) return &app->nodes[i];
    }
    return NULL;
}

const char* meshtastic_node_name(MeshtasticApp* app, uint32_t node_num) {
    MeshNode* node = meshtastic_find_node(app, node_num);
    if(node) {
        if(node->long_name[0]) return node->long_name;
        if(node->short_name[0]) return node->short_name;
    }
    static char hex_name[12];
    snprintf(hex_name, sizeof(hex_name), "!%08lx", node_num);
    return hex_name;
}

MeshtasticApp* meshtastic_app_alloc(void) {
    MeshtasticApp* app = malloc(sizeof(MeshtasticApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->storage = furi_record_open(RECORD_STORAGE);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&meshtastic_scene_handlers, app);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, meshtastic_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, meshtastic_back_event_callback);

    // GUI modules
    app->submenu = submenu_alloc();
    view_dispatcher_add_view(app->view_dispatcher, MeshtasticViewSubmenu, submenu_get_view(app->submenu));
    app->widget = widget_alloc();
    view_dispatcher_add_view(app->view_dispatcher, MeshtasticViewWidget, widget_get_view(app->widget));
    app->popup = popup_alloc();
    view_dispatcher_add_view(app->view_dispatcher, MeshtasticViewPopup, popup_get_view(app->popup));
    app->loading = loading_alloc();
    view_dispatcher_add_view(app->view_dispatcher, MeshtasticViewLoading, loading_get_view(app->loading));
    app->text_input = text_input_alloc();
    view_dispatcher_add_view(app->view_dispatcher, MeshtasticViewTextInput, text_input_get_view(app->text_input));
    app->text_box = text_box_alloc();
    view_dispatcher_add_view(app->view_dispatcher, MeshtasticViewTextBox, text_box_get_view(app->text_box));

    // State
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->timer = furi_timer_alloc(meshtastic_timer_callback, FuriTimerTypePeriodic, app);
    app->state = MeshStateIdle;
    app->text_box_store = furi_string_alloc();
    app->scan_device_count = 0;
    app->node_count = 0;
    app->message_count = 0;
    app->channel_count = 0;
    app->config_complete = false;
    app->has_read_data = false;
    memset(&app->char_handles, 0, sizeof(app->char_handles));

    // Init GATT client
    ble_gatt_client_init();
    ble_gatt_client_set_callback(meshtastic_gatt_callback, app);

    return app;
}

void meshtastic_app_free(MeshtasticApp* app) {
    furi_assert(app);

    // BLE cleanup
    gap_set_scan_callback(NULL, NULL);
    ble_gatt_client_set_callback(NULL, NULL);
    if(app->state == MeshStateScanning) {
        gap_stop_scanning();
    }
    if(app->state >= MeshStateConnecting && gap_get_state() == GapStateConnected) {
        gap_disconnect(app->connection_handle);
    }

    furi_timer_free(app->timer);
    furi_string_free(app->text_box_store);
    furi_mutex_free(app->mutex);

    view_dispatcher_remove_view(app->view_dispatcher, MeshtasticViewSubmenu);
    submenu_free(app->submenu);
    view_dispatcher_remove_view(app->view_dispatcher, MeshtasticViewWidget);
    widget_free(app->widget);
    view_dispatcher_remove_view(app->view_dispatcher, MeshtasticViewPopup);
    popup_free(app->popup);
    view_dispatcher_remove_view(app->view_dispatcher, MeshtasticViewLoading);
    loading_free(app->loading);
    view_dispatcher_remove_view(app->view_dispatcher, MeshtasticViewTextInput);
    text_input_free(app->text_input);
    view_dispatcher_remove_view(app->view_dispatcher, MeshtasticViewTextBox);
    text_box_free(app->text_box);

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_STORAGE);

    free(app);
}

int32_t meshtastic_app(void* p) {
    UNUSED(p);

    // Clean BLE state
    gap_set_scan_callback(NULL, NULL);
    if(gap_get_state() == GapStateScanning) gap_stop_scanning();

    MeshtasticApp* app = meshtastic_app_alloc();

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    scene_manager_next_scene(app->scene_manager, MeshtasticSceneStart);
    view_dispatcher_run(app->view_dispatcher);

    meshtastic_app_free(app);
    return 0;
}
