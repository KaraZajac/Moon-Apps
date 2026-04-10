#include "../whisper_pair_app_i.h"

enum { WpStartScan, WpStartAbout };

static void wp_start_cb(void* ctx, uint32_t idx) {
    view_dispatcher_send_custom_event(((WhisperPairApp*)ctx)->view_dispatcher, idx);
}

void whisper_pair_scene_start_on_enter(void* context) {
    WhisperPairApp* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "WhisperPair");
    submenu_add_item(app->submenu, "Scan for Devices", WpStartScan, wp_start_cb, app);
    submenu_add_item(app->submenu, "About", WpStartAbout, wp_start_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, WpViewSubmenu);
}

bool whisper_pair_scene_start_on_event(void* context, SceneManagerEvent event) {
    WhisperPairApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event == WpStartScan) {
        scene_manager_next_scene(app->scene_manager, WhisperPairSceneScan);
        return true;
    } else if(event.event == WpStartAbout) {
        widget_reset(app->widget);
        widget_add_text_scroll_element(
            app->widget, 0, 0, 128, 64,
            "WhisperPair\n"
            "CVE-2025-36911\n"
            "\n"
            "Fast Pair Key-Based\n"
            "Pairing bypass scanner.\n"
            "\n"
            "Scans for BLE devices\n"
            "advertising Fast Pair\n"
            "(0xFE2C) and tests KBP\n"
            "characteristic for the\n"
            "authentication bypass.\n"
            "\n"
            "4 test strategies:\n"
            "- RAW_KBP\n"
            "- WITH_SEEKER\n"
            "- RETROACTIVE\n"
            "- EXTENDED_RESPONSE\n"
            "\n"
            "Flipper limitation:\n"
            "No BR/EDR (Classic BT),\n"
            "so full exploit chain\n"
            "is not possible. This\n"
            "is a scanner/tester.\n"
            "\n"
            "For authorized security\n"
            "testing only.\n"
            "\n"
            "v0.2 @KaraZajac");
        view_dispatcher_switch_to_view(app->view_dispatcher, WpViewWidget);
        return true;
    }
    return false;
}

void whisper_pair_scene_start_on_exit(void* context) {
    WhisperPairApp* app = context;
    submenu_reset(app->submenu);
    widget_reset(app->widget);
}
