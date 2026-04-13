#include "fuzz_scene.h"

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_enter,
void (*const fuzz_scene_on_enter_handlers[])(void*) = {
#include "fuzz_scene_config.h"
};
#undef ADD_SCENE
#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_event,
bool (*const fuzz_scene_on_event_handlers[])(void*, SceneManagerEvent) = {
#include "fuzz_scene_config.h"
};
#undef ADD_SCENE
#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_exit,
void (*const fuzz_scene_on_exit_handlers[])(void*) = {
#include "fuzz_scene_config.h"
};
#undef ADD_SCENE

const SceneManagerHandlers fuzz_scene_handlers = {
    .on_enter_handlers = fuzz_scene_on_enter_handlers,
    .on_event_handlers = fuzz_scene_on_event_handlers,
    .on_exit_handlers = fuzz_scene_on_exit_handlers,
    .scene_num = FuzzSceneNum,
};
