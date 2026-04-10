#pragma once

#include <gui/scene_manager.h>

#define ADD_SCENE(prefix, name, id) LockTesterScene##id,
typedef enum {
#include "lock_tester_scene_config.h"
    LockTesterSceneNum,
} LockTesterSceneId;
#undef ADD_SCENE

extern const SceneManagerHandlers lock_tester_scene_handlers;

#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_enter(void*);
#include "lock_tester_scene_config.h"
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) \
    bool prefix##_scene_##name##_on_event(void*, SceneManagerEvent);
#include "lock_tester_scene_config.h"
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_exit(void*);
#include "lock_tester_scene_config.h"
#undef ADD_SCENE
