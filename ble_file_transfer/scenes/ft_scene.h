#pragma once
#include <gui/scene_manager.h>

#define ADD_SCENE(prefix, name, id) FtScene##id,
typedef enum {
#include "ft_scene_config.h"
    FtSceneNum,
} FtSceneId;
#undef ADD_SCENE

extern const SceneManagerHandlers ft_scene_handlers;

#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_enter(void*);
#include "ft_scene_config.h"
#undef ADD_SCENE
#define ADD_SCENE(prefix, name, id) bool prefix##_scene_##name##_on_event(void*, SceneManagerEvent);
#include "ft_scene_config.h"
#undef ADD_SCENE
#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_exit(void*);
#include "ft_scene_config.h"
#undef ADD_SCENE
