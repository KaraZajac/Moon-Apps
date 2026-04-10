#include "lock_tester_scene.h"

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_enter,
void (*const lock_tester_scene_on_enter_handlers[])(void*) = {
#include "lock_tester_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_event,
bool (*const lock_tester_scene_on_event_handlers[])(void*, SceneManagerEvent) = {
#include "lock_tester_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_exit,
void (*const lock_tester_scene_on_exit_handlers[])(void*) = {
#include "lock_tester_scene_config.h"
};
#undef ADD_SCENE

const SceneManagerHandlers lock_tester_scene_handlers = {
    .on_enter_handlers = lock_tester_scene_on_enter_handlers,
    .on_event_handlers = lock_tester_scene_on_event_handlers,
    .on_exit_handlers = lock_tester_scene_on_exit_handlers,
    .scene_num = LockTesterSceneNum,
};
