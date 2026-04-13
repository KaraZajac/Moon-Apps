#pragma once
#include <gui/scene_manager.h>

#define ADD_SCENE(prefix, name, id) EcovacsScene##id,
typedef enum {
#include "ecovacs_scene_config.h"
    EcovacsSceneNum,
} EcovacsSceneId;
#undef ADD_SCENE

extern const SceneManagerHandlers ecovacs_scene_handlers;

#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_enter(void*);
#include "ecovacs_scene_config.h"
#undef ADD_SCENE
#define ADD_SCENE(prefix, name, id) bool prefix##_scene_##name##_on_event(void*, SceneManagerEvent);
#include "ecovacs_scene_config.h"
#undef ADD_SCENE
#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_exit(void*);
#include "ecovacs_scene_config.h"
#undef ADD_SCENE
