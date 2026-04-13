#pragma once
#include <gui/scene_manager.h>

#define ADD_SCENE(prefix, name, id) FuzzScene##id,
typedef enum {
#include "fuzz_scene_config.h"
    FuzzSceneNum,
} FuzzSceneId;
#undef ADD_SCENE

extern const SceneManagerHandlers fuzz_scene_handlers;

#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_enter(void*);
#include "fuzz_scene_config.h"
#undef ADD_SCENE
#define ADD_SCENE(prefix, name, id) bool prefix##_scene_##name##_on_event(void*, SceneManagerEvent);
#include "fuzz_scene_config.h"
#undef ADD_SCENE
#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_exit(void*);
#include "fuzz_scene_config.h"
#undef ADD_SCENE
