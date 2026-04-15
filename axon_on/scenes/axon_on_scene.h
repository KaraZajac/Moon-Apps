#pragma once

#include <gui/scene_manager.h>

// Generate scene id and total number
#define ADD_SCENE(prefix, name, id) AxonOnScene##id,
typedef enum {
#include "axon_on_scene_config.h"
    AxonOnSceneNum,
} AxonOnSceneId;
#undef ADD_SCENE

extern const SceneManagerHandlers axon_on_scene_handlers;

// Generate scene on_enter handlers declaration
#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_enter(void*);
#include "axon_on_scene_config.h"
#undef ADD_SCENE

// Generate scene on_event handlers declaration
#define ADD_SCENE(prefix, name, id) \
    bool prefix##_scene_##name##_on_event(void*, SceneManagerEvent);
#include "axon_on_scene_config.h"
#undef ADD_SCENE

// Generate scene on_exit handlers declaration
#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_exit(void*);
#include "axon_on_scene_config.h"
#undef ADD_SCENE
