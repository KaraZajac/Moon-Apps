#include "meshtastic_scene.h"

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_enter,
void (*const meshtastic_scene_on_enter_handlers[])(void*) = {
#include "meshtastic_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_event,
bool (*const meshtastic_scene_on_event_handlers[])(void*, SceneManagerEvent) = {
#include "meshtastic_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_exit,
void (*const meshtastic_scene_on_exit_handlers[])(void*) = {
#include "meshtastic_scene_config.h"
};
#undef ADD_SCENE

const SceneManagerHandlers meshtastic_scene_handlers = {
    .on_enter_handlers = meshtastic_scene_on_enter_handlers,
    .on_event_handlers = meshtastic_scene_on_event_handlers,
    .on_exit_handlers = meshtastic_scene_on_exit_handlers,
    .scene_num = MeshtasticSceneNum,
};
