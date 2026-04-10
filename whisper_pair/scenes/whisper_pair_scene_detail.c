#include "../whisper_pair_app_i.h"

static void wp_detail_button_cb(GuiButtonType result, InputType type, void* context) {
    if(type == InputTypeShort)
        view_dispatcher_send_custom_event(((WhisperPairApp*)context)->view_dispatcher, result);
}

void whisper_pair_scene_detail_on_enter(void* context) {
    WhisperPairApp* app = context;
    widget_reset(app->widget);

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    if(app->selected_idx >= app->device_count) {
        furi_mutex_release(app->mutex);
        scene_manager_previous_scene(app->scene_manager);
        return;
    }

    WpDevice* d = &app->devices[app->selected_idx];

    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
        d->address[5], d->address[4], d->address[3],
        d->address[2], d->address[1], d->address[0]);

    FuriString* info = furi_string_alloc();
    furi_string_printf(info, "\e#%s\nMAC: %s\nRSSI: %d dBm\n",
        d->has_name ? d->name : "Unknown Device", mac, d->rssi);

    if(d->has_model_id) {
        furi_string_cat_printf(info, "Model ID: %06lX\n", (unsigned long)d->model_id);
        furi_string_cat_printf(info, "Mode: %s\n",
            d->in_pairing_mode ? "Pairing" : "Idle (testable)");

        if(d->known) {
            furi_string_cat_printf(info, "DB: %s\n",
                d->known->vulnerable ? "Known VULNERABLE" : "Known PATCHED");
        } else {
            furi_string_cat_str(info, "DB: Unknown model\n");
        }

        // Show test result if already tested
        switch(d->vuln_status) {
        case WpVulnVulnerable:
            furi_string_cat_str(info, "\nResult: VULNERABLE");
            break;
        case WpVulnPatched:
            furi_string_cat_str(info, "\nResult: PATCHED");
            break;
        case WpVulnError:
            furi_string_cat_str(info, "\nResult: Error");
            break;
        default:
            break;
        }
    } else {
        furi_string_cat_str(info, "Not a Fast Pair device");
    }

    bool is_fp = d->has_model_id;
    furi_mutex_release(app->mutex);

    widget_add_text_scroll_element(app->widget, 0, 0, 128, 49, furi_string_get_cstr(info));
    furi_string_free(info);

    if(is_fp) {
        widget_add_button_element(
            app->widget, GuiButtonTypeCenter, "Test KBP",
            wp_detail_button_cb, app);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, WpViewWidget);
}

bool whisper_pair_scene_detail_on_event(void* context, SceneManagerEvent event) {
    WhisperPairApp* app = context;
    if(event.type == SceneManagerEventTypeCustom && event.event == GuiButtonTypeCenter) {
        scene_manager_next_scene(app->scene_manager, WhisperPairSceneTest);
        return true;
    }
    return false;
}

void whisper_pair_scene_detail_on_exit(void* context) {
    widget_reset(((WhisperPairApp*)context)->widget);
}
