#include "../ft_app_i.h"

static bool sending_active = false;

static void send_next_chunk(FtApp* app) {
    if(!app->tx_file || !storage_file_is_open(app->tx_file)) return;

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    uint16_t credits = app->tx_credits;
    furi_mutex_release(app->mutex);

    if(credits == 0) return;

    uint8_t buf[FT_MPS];
    uint16_t read = storage_file_read(app->tx_file, &buf[1], FT_DATA_CHUNK);

    if(read > 0) {
        buf[0] = FT_PKT_FILE_DATA;
        if(ble_l2cap_coc_send(app->send_coc_channel, buf, read + 1)) {
            app->bytes_transferred += read;
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            app->tx_credits--;
            furi_mutex_release(app->mutex);
        }
    }

    if(storage_file_eof(app->tx_file)) {
        uint8_t done = FT_PKT_FILE_DONE;
        ble_l2cap_coc_send(app->send_coc_channel, &done, 1);
        storage_file_close(app->tx_file);
        sending_active = false;
        FURI_LOG_I(TAG, "File sent: %lu bytes", app->bytes_transferred);
    }
}

static void start_sending(FtApp* app) {
    const char* path = furi_string_get_cstr(app->file_path);
    if(!storage_file_open(app->tx_file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        FURI_LOG_E(TAG, "Failed to open file: %s", path);
        app->transfer_error = true;
        return;
    }

    app->bytes_transferred = 0;
    app->transfer_complete = false;

    // Send file header: [type][size_le32][filename]
    uint8_t hdr[FT_MPS];
    hdr[0] = FT_PKT_FILE_HEADER;
    hdr[1] = app->file_size & 0xFF;
    hdr[2] = (app->file_size >> 8) & 0xFF;
    hdr[3] = (app->file_size >> 16) & 0xFF;
    hdr[4] = (app->file_size >> 24) & 0xFF;
    uint8_t name_len = strlen(app->filename);
    if(name_len > FT_DATA_CHUNK - 4) name_len = FT_DATA_CHUNK - 4;
    memcpy(&hdr[5], app->filename, name_len);

    if(!ble_l2cap_coc_send(app->send_coc_channel, hdr, 5 + name_len)) {
        FURI_LOG_E(TAG, "Failed to send file header");
        app->transfer_error = true;
        return;
    }

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->tx_credits--;
    furi_mutex_release(app->mutex);

    sending_active = true;
    FURI_LOG_I(TAG, "Sending: %s (%lu bytes)", app->filename, app->file_size);
}

static void update_progress_widget(FtApp* app) {
    widget_reset(app->widget);

    const char* mode_str = app->mode == FtModeSending ? "Sending" : "Receiving";
    widget_add_string_element(app->widget, 64, 5, AlignCenter, AlignTop, FontPrimary, mode_str);
    widget_add_string_element(app->widget, 64, 17, AlignCenter, AlignTop, FontSecondary, app->filename);

    uint8_t progress = 0;
    if(app->file_size > 0) {
        progress = (app->bytes_transferred * 100) / app->file_size;
        if(progress > 100) progress = 100;
    }

    char progress_str[32];
    snprintf(progress_str, sizeof(progress_str), "%lu / %lu bytes (%d%%)",
        app->bytes_transferred, app->file_size, progress);
    widget_add_string_element(app->widget, 64, 30, AlignCenter, AlignTop, FontSecondary, progress_str);

    if(app->transfer_complete) {
        widget_add_string_element(
            app->widget, 64, 48, AlignCenter, AlignTop, FontPrimary, "Complete!");
    } else if(app->transfer_error) {
        widget_add_string_element(
            app->widget, 64, 48, AlignCenter, AlignTop, FontPrimary, "Error!");
    }
}

void ft_scene_transfer_on_enter(void* context) {
    FtApp* app = context;

    app->transfer_complete = false;
    app->transfer_error = false;

    if(app->mode == FtModeSending) {
        start_sending(app);
    }

    update_progress_widget(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, FtViewWidget);

    furi_timer_start(app->timer, 50);
}

bool ft_scene_transfer_on_event(void* context, SceneManagerEvent event) {
    FtApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == FtCustomEventTick) {
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        uint16_t credits = app->tx_credits;
        bool complete = app->transfer_complete;
        bool error = app->transfer_error;
        furi_mutex_release(app->mutex);

        if(app->mode == FtModeSending && sending_active && credits > 0) {
            for(int i = 0; i < 4 && credits > 0 && sending_active; i++) {
                send_next_chunk(app);
                furi_mutex_acquire(app->mutex, FuriWaitForever);
                credits = app->tx_credits;
                furi_mutex_release(app->mutex);
            }
        }

        update_progress_widget(app);

        if(complete) {
            furi_timer_stop(app->timer);
            notification_message(app->notifications, &sequence_success);
        }
        if(error) {
            furi_timer_stop(app->timer);
        }
        return true;
    }

    if(event.event == FtCustomEventCocCredits) {
        return true;
    }

    if(event.event == FtCustomEventCocDataReceived) {
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        bool complete = app->transfer_complete;
        furi_mutex_release(app->mutex);
        if(complete) {
            furi_timer_stop(app->timer);
            notification_message(app->notifications, &sequence_success);
            update_progress_widget(app);
        }
        return true;
    }

    if(event.event == FtCustomEventCocDisconnected || event.event == FtCustomEventCocError) {
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        bool complete = app->transfer_complete;
        furi_mutex_release(app->mutex);
        if(!complete) {
            app->transfer_error = true;
            furi_timer_stop(app->timer);
            update_progress_widget(app);
        }
        return true;
    }

    return false;
}

void ft_scene_transfer_on_exit(void* context) {
    FtApp* app = context;
    furi_timer_stop(app->timer);
    sending_active = false;

    if(storage_file_is_open(app->tx_file)) storage_file_close(app->tx_file);
    if(storage_file_is_open(app->rx_file)) storage_file_close(app->rx_file);

    if(app->send_coc_connected) {
        ble_l2cap_coc_disconnect(app->send_coc_channel);
        app->send_coc_connected = false;
    }
    if(app->send_connected) {
        gap_disconnect(app->send_handle);
        app->send_connected = false;
    }

    // Reset mode to idle so we're ready for next transfer
    app->mode = FtModeIdle;
    widget_reset(app->widget);
}
