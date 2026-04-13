#include "ecovacs_scene.h"

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_enter,
void (*const ecovacs_scene_on_enter_handlers[])(void*) = {
#include "ecovacs_scene_config.h"
};
#undef ADD_SCENE
#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_event,
bool (*const ecovacs_scene_on_event_handlers[])(void*, SceneManagerEvent) = {
#include "ecovacs_scene_config.h"
};
#undef ADD_SCENE
#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_exit,
void (*const ecovacs_scene_on_exit_handlers[])(void*) = {
#include "ecovacs_scene_config.h"
};
#undef ADD_SCENE

const SceneManagerHandlers ecovacs_scene_handlers = {
    .on_enter_handlers = ecovacs_scene_on_enter_handlers,
    .on_event_handlers = ecovacs_scene_on_event_handlers,
    .on_exit_handlers = ecovacs_scene_on_exit_handlers,
    .scene_num = EcovacsSceneNum,
};
