#pragma once

#include <gui/scene_manager.h>

#define ADD_SCENE(prefix, name, id) MeshtasticScene##id,
typedef enum {
#include "meshtastic_scene_config.h"
    MeshtasticSceneNum,
} MeshtasticSceneId;
#undef ADD_SCENE

extern const SceneManagerHandlers meshtastic_scene_handlers;

#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_enter(void*);
#include "meshtastic_scene_config.h"
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) \
    bool prefix##_scene_##name##_on_event(void*, SceneManagerEvent);
#include "meshtastic_scene_config.h"
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_exit(void*);
#include "meshtastic_scene_config.h"
#undef ADD_SCENE
