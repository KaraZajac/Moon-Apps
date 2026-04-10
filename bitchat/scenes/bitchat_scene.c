#include "bitchat_scene.h"

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_enter,
void (*const bitchat_scene_on_enter_handlers[])(void*) = {
#include "bitchat_scene_config.h"
};
#undef ADD_SCENE
#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_event,
bool (*const bitchat_scene_on_event_handlers[])(void*, SceneManagerEvent) = {
#include "bitchat_scene_config.h"
};
#undef ADD_SCENE
#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_exit,
void (*const bitchat_scene_on_exit_handlers[])(void*) = {
#include "bitchat_scene_config.h"
};
#undef ADD_SCENE

const SceneManagerHandlers bitchat_scene_handlers = {
    .on_enter_handlers = bitchat_scene_on_enter_handlers,
    .on_event_handlers = bitchat_scene_on_event_handlers,
    .on_exit_handlers = bitchat_scene_on_exit_handlers,
    .scene_num = BitchatSceneNum,
};
