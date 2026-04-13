#include "coros_scene.h"

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_enter,
void (*const coros_scene_on_enter_handlers[])(void*) = {
#include "coros_scene_config.h"
};
#undef ADD_SCENE
#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_event,
bool (*const coros_scene_on_event_handlers[])(void*, SceneManagerEvent) = {
#include "coros_scene_config.h"
};
#undef ADD_SCENE
#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_exit,
void (*const coros_scene_on_exit_handlers[])(void*) = {
#include "coros_scene_config.h"
};
#undef ADD_SCENE

const SceneManagerHandlers coros_scene_handlers = {
    .on_enter_handlers = coros_scene_on_enter_handlers,
    .on_event_handlers = coros_scene_on_event_handlers,
    .on_exit_handlers = coros_scene_on_exit_handlers,
    .scene_num = CorosSceneNum,
};
