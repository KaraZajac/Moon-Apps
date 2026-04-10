#pragma once
#include <gui/scene_manager.h>

#define ADD_SCENE(prefix, name, id) BitchatScene##id,
typedef enum {
#include "bitchat_scene_config.h"
    BitchatSceneNum,
} BitchatSceneId;
#undef ADD_SCENE

extern const SceneManagerHandlers bitchat_scene_handlers;

#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_enter(void*);
#include "bitchat_scene_config.h"
#undef ADD_SCENE
#define ADD_SCENE(prefix, name, id) \
    bool prefix##_scene_##name##_on_event(void*, SceneManagerEvent);
#include "bitchat_scene_config.h"
#undef ADD_SCENE
#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_exit(void*);
#include "bitchat_scene_config.h"
#undef ADD_SCENE
