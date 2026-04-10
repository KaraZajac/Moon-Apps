#pragma once

#include <gui/scene_manager.h>

// Generate scene id and total number
#define ADD_SCENE(prefix, name, id) TrackerDetectorScene##id,
typedef enum {
#include "tracker_detector_scene_config.h"
    TrackerDetectorSceneNum,
} TrackerDetectorSceneId;
#undef ADD_SCENE

extern const SceneManagerHandlers tracker_detector_scene_handlers;

// Generate scene on_enter handlers declaration
#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_enter(void*);
#include "tracker_detector_scene_config.h"
#undef ADD_SCENE

// Generate scene on_event handlers declaration
#define ADD_SCENE(prefix, name, id) \
    bool prefix##_scene_##name##_on_event(void*, SceneManagerEvent);
#include "tracker_detector_scene_config.h"
#undef ADD_SCENE

// Generate scene on_exit handlers declaration
#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_exit(void*);
#include "tracker_detector_scene_config.h"
#undef ADD_SCENE
