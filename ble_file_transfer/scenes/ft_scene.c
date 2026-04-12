#include "ft_scene.h"

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_enter,
void (*const ft_scene_on_enter_handlers[])(void*) = {
#include "ft_scene_config.h"
};
#undef ADD_SCENE
#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_event,
bool (*const ft_scene_on_event_handlers[])(void*, SceneManagerEvent) = {
#include "ft_scene_config.h"
};
#undef ADD_SCENE
#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_exit,
void (*const ft_scene_on_exit_handlers[])(void*) = {
#include "ft_scene_config.h"
};
#undef ADD_SCENE

const SceneManagerHandlers ft_scene_handlers = {
    .on_enter_handlers = ft_scene_on_enter_handlers,
    .on_event_handlers = ft_scene_on_event_handlers,
    .on_exit_handlers = ft_scene_on_exit_handlers,
    .scene_num = FtSceneNum,
};
