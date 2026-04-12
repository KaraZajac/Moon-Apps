#include "../ft_app_i.h"

void ft_scene_send_browse_on_enter(void* context) {
    FtApp* app = context;

    // Open file browser
    DialogsFileBrowserOptions browser_options;
    dialog_file_browser_set_basic_options(&browser_options, "*", NULL);
    browser_options.base_path = "/ext";
    browser_options.skip_assets = true;

    furi_string_set(app->file_path, "/ext");

    if(dialog_file_browser_show(app->dialogs, app->file_path, app->file_path, &browser_options)) {
        // File selected — get filename and size
        const char* full_path = furi_string_get_cstr(app->file_path);

        // Extract just the filename
        const char* slash = strrchr(full_path, '/');
        const char* name = slash ? slash + 1 : full_path;
        strncpy(app->filename, name, FT_MAX_FILENAME - 1);
        app->filename[FT_MAX_FILENAME - 1] = '\0';

        // Get file size
        File* f = storage_file_alloc(app->storage);
        if(storage_file_open(f, full_path, FSAM_READ, FSOM_OPEN_EXISTING)) {
            app->file_size = storage_file_size(f);
            storage_file_close(f);
        }
        storage_file_free(f);

        FURI_LOG_I(TAG, "Selected: %s (%lu bytes)", app->filename, app->file_size);

        // Go to scan for receivers
        scene_manager_next_scene(app->scene_manager, FtSceneSendScan);
    } else {
        // User cancelled
        scene_manager_previous_scene(app->scene_manager);
    }
}

bool ft_scene_send_browse_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void ft_scene_send_browse_on_exit(void* context) {
    UNUSED(context);
}
