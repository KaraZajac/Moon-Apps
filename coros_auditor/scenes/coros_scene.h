#pragma once
#include <gui/scene_manager.h>

#define ADD_SCENE(prefix, name, id) CorosScene##id,
typedef enum {
#include "coros_scene_config.h"
    CorosSceneNum,
} CorosSceneId;
#undef ADD_SCENE

extern const SceneManagerHandlers coros_scene_handlers;

#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_enter(void*);
#include "coros_scene_config.h"
#undef ADD_SCENE
#define ADD_SCENE(prefix, name, id) bool prefix##_scene_##name##_on_event(void*, SceneManagerEvent);
#include "coros_scene_config.h"
#undef ADD_SCENE
#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_exit(void*);
#include "coros_scene_config.h"
#undef ADD_SCENE
